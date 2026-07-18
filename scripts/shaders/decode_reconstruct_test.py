#!/usr/bin/env python3
"""Full-chain CPU reconstruction test for the ScreenSpaceGI g-buffer decode contract.

This is the pre-adapter gate. The XeGTAO AO pass reconstructs view-space position from
the depth buffer; that reconstruction is the SSS-burn class of bug (row-vs-column /
transpose / uv y-flip / handedness all survive static analysis and are only caught by a
runtime oracle). This test treats the statically-pinned contract as a *hypothesis* and
confirms it two independent ways, then leaves a hook for captured ground-truth triples.

Mirrors, exactly:
  - src/Render/Engine.h  TryGetCameraMatrices (invProj = raw non-transposed inverse;
    NDCToViewMul/Add derived by transforming the two NDC corners through invProj and
    dividing by z), row-vector convention (v @ M == HLSL mul(v, M) == XMVector4Transform).
  - features/ScreenSpaceGI/Shaders/XeGTAO/common.hlsli  ScreenToViewPosition fast path.
  - .agents/features/ScreenSpaceGI.md  pinned view-pos reconstruction snippet:
        localZ  = (depth - 0.01) / 0.99                 (world/far viewport partition)
        h       = mul(float4(uv.x*2-1, 1-uv.y*2, localZ, 1), invProj)
        viewPos = h.xyz / h.w

What it proves:
  1. ROUND-TRIP  — project a known view pos through the pinned projMat (v*M, persp divide,
     viewport partition, uv y-flip) then reconstruct; assert recovery. Proves the chain is
     self-consistent under the pinned convention.
  2. CROSS-CHECK (a) — the shader uses NDCToViewMul/Add (ScreenToViewPosition), NOT the
     matrix. Assert the fast-path XY equals the full-matrix XY. Catches an inconsistency
     between what the shader computes and the true matrix reconstruction.
  3. CROSS-CHECK (b) — assert the closed-form hyperbolic linearize
     z = n*f / (f - localZ*(f-n)) equals both the true view-Z and the full-matrix view-Z.
  4. CROSS-CHECK (c) — assert a first-principles analytic pinhole XY (from tan(fov/2) off the
     projection diagonal, NOT via matrix inversion) equals the full-matrix XY. Because it
     never inverts/transposes the matrix, it diverges under a *consistent* y-flip / transpose /
     handedness error that the round-trip alone would cancel.
  5. NEGATIVE CONTROLS — omitting the uv y-flip, and transposing invProj, must BREAK the
     round-trip. Proves the test actually discriminates the convention (has teeth).
  6. CAPTURED-ORACLE HOOK — captured (uv, depth, viewPos, projMat) tuples from a real frame
     are the memory-truth that beats static analysis; each carries its OWN frame projMat, so
     reconstruction uses the true per-frame invProj. Empty until a capture exists.

The synthetic checks (1-5) prove the chain is self-consistent AND that the shader fast-path,
the linearize, and a matrix-independent pinhole anchor all agree — a strong necessary gate.
They still share the FOV magnitude, so only a captured off-axis oracle (item 6) fully proves
the pinned convention matches the *game*; that is the required proof before the go-live.

Modes:
  default            — synthetic/pre-capture gate; PASSes on checks 1-5 (use before a capture).
  --require-oracle   — game-verification gate; additionally FAILs unless >=1 off-axis captured
                       triple round-trips. Run this (green) before the t0/t13 + SSGI=1 go-live.
"""

from __future__ import annotations

import math
import sys

import numpy as np

# Viewport depth partition for the world/far camera (State::SetCameraViewPort).
#   stored = MinDepth + localNdcZ * (MaxDepth - MinDepth), world range [0.01, 1.00].
VIEWPORT_MIN_DEPTH = 0.01
VIEWPORT_MAX_DEPTH = 1.00

# Representative FO4 view->clip projection. Exact numbers are irrelevant to a
# consistency/round-trip test; only the structure + conventions matter (row-major,
# row-vector v*M, LH m23=+1, standard depth near->0 far->1). The pinned z-row is
# m22 = f/(f-n), m32 = -n*f/(f-n), so view z=n->ndc.z=0, z=f->ndc.z=1.
FOV_Y_DEG = 55.0
ASPECT = 16.0 / 9.0
NEAR = 8.0
FAR = 50000.0


def build_projection(fov_y_deg: float, aspect: float, n: float, f: float) -> np.ndarray:
    """Row-major perspective matrix for the row-vector convention clip = view * P."""
    p11 = 1.0 / math.tan(math.radians(fov_y_deg) * 0.5)
    p00 = p11 / aspect
    m22 = f / (f - n)
    m32 = -n * f / (f - n)
    # Row r, column c is P[r, c]. clip = (vx,vy,vz,1) @ P.
    return np.array(
        [
            [p00, 0.0, 0.0, 0.0],
            [0.0, p11, 0.0, 0.0],
            [0.0, 0.0, m22, 1.0],  # m23 = +1 (LH): clip.w = vz
            [0.0, 0.0, m32, 0.0],  # m33 = 0
        ],
        dtype=np.float64,
    )


def project(view_pos: np.ndarray, proj: np.ndarray) -> tuple[np.ndarray, float]:
    """View pos -> (uv, stored depth), applying the world/far viewport partition + y-flip."""
    clip = np.append(view_pos, 1.0) @ proj  # row-vector v * P
    ndc = clip / clip[3]  # perspective divide; ndc.z in [0,1]
    stored = VIEWPORT_MIN_DEPTH + ndc[2] * (VIEWPORT_MAX_DEPTH - VIEWPORT_MIN_DEPTH)
    uv = np.array([(ndc[0] + 1.0) * 0.5, (1.0 - ndc[1]) * 0.5], dtype=np.float64)
    return uv, float(stored)


def local_ndc_z(stored_depth: float) -> float:
    """Recover the world/far camera's local [0,1] NDC z from the stored buffer value."""
    return (stored_depth - VIEWPORT_MIN_DEPTH) / (VIEWPORT_MAX_DEPTH - VIEWPORT_MIN_DEPTH)


def reconstruct(uv: np.ndarray, stored_depth: float, inv_proj: np.ndarray) -> np.ndarray:
    """Full-matrix reconstruction: the pinned ambient-pass snippet, exactly."""
    ndc = np.array(
        [uv[0] * 2.0 - 1.0, 1.0 - uv[1] * 2.0, local_ndc_z(stored_depth), 1.0],
        dtype=np.float64,
    )
    h = ndc @ inv_proj  # HLSL mul(float4, invProj) == row-vector v * M
    return h[:3] / h[3]


def compute_ndc_to_view(inv_proj: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Mirror Engine.h: transform the two NDC corners through invProj, divide each by its
    own z, then take the XY difference (mul) and top-left offset (add)."""
    top_left = np.array([-1.0, 1.0, 1.0, 1.0], dtype=np.float64) @ inv_proj
    bottom_right = np.array([1.0, -1.0, 1.0, 1.0], dtype=np.float64) @ inv_proj
    if top_left[2] == 0.0 or bottom_right[2] == 0.0:
        raise ZeroDivisionError("degenerate invProj corner z")
    top_left = top_left / top_left[2]
    bottom_right = bottom_right / bottom_right[2]
    mul = np.array([bottom_right[0] - top_left[0], bottom_right[1] - top_left[1]])
    add = np.array([top_left[0], top_left[1]])
    return mul, add


def fastpath_view_xy(uv: np.ndarray, view_z: float, mul: np.ndarray, add: np.ndarray) -> np.ndarray:
    """ScreenToViewPosition XY: (NDCToViewMul.xy * uv + NDCToViewAdd.xy) * viewspaceDepth."""
    return (mul * uv + add) * view_z


def analytic_pinhole_xy(uv: np.ndarray, view_z: float, proj: np.ndarray) -> np.ndarray:
    """First-principles frustum-ray XY, INDEPENDENT of matrix inversion/transform.

    For a pinhole camera, view.x = ndc.x * tan(fovX/2) * z and view.y = ndc.y * tan(fovY/2) * z,
    with tan(fovX/2)=1/P00, tan(fovY/2)=1/P11 read off the projection diagonal and the pinned
    y-flip ndc.y = 1 - 2*uv.y. Because this path never inverts or transposes the matrix, it
    diverges from the full-matrix reconstruction under a *consistent* y-flip/handedness/transpose
    error that the project->reconstruct round-trip would otherwise cancel (proven: a consistent
    flip sends analytic.y -> -matrix.y). It shares only the FOV magnitude with build_projection."""
    tan_half_x = 1.0 / proj[0, 0]
    tan_half_y = 1.0 / proj[1, 1]
    ndc_x = uv[0] * 2.0 - 1.0
    ndc_y = 1.0 - uv[1] * 2.0
    return np.array([ndc_x * tan_half_x, ndc_y * tan_half_y], dtype=np.float64) * view_z


def linearize(stored_depth: float, n: float, f: float) -> float:
    """Closed-form hyperbolic linearize (== DepthCoCCS Linearize): z = n*f/(f - z_ndc*(f-n))."""
    z_ndc = local_ndc_z(stored_depth)
    return n * f / (f - z_ndc * (f - n))


# Captured fixtures from a real FO4 frame (RenderDoc / instrumented readback). This is the
# memory-truth oracle; the synthetic round-trip only proves self-consistency of the ASSUMED
# convention, and even the analytic anchor shares the FOV magnitude — a captured triple is the
# only thing that proves the pinned convention matches the game. Each entry MUST carry the
# frame's own row-major projMat (camViewData.projMat) so reconstruction uses that frame's true
# invProj, never the synthetic representative matrix below.
#   ("label", (u, v), stored_depth, (vx, vy, vz), proj_rows)  # proj_rows = 4x4 row-major list
# At least one OFF-AXIS (nonzero uv.x AND uv.y) captured triple is REQUIRED before this gate is
# treated as game-verified (i.e. before the t0/t13 + SSGI=1 go-live).
CAPTURED_ORACLE: list[
    tuple[str, tuple[float, float], float, tuple[float, float, float], list[list[float]]]
] = []


def make_sample_view_positions() -> list[np.ndarray]:
    """A grid of off-center view positions at varied depths (partition boundary .. far),
    so the uv y-flip and the XY slope are actually exercised (not just the center)."""
    samples: list[np.ndarray] = []
    # Depths span just past the near/first-person boundary out to deep world geometry.
    for vz in (10.0, 25.0, 100.0, 750.0, 5000.0, 40000.0):
        tan_y = math.tan(math.radians(FOV_Y_DEG) * 0.5)
        tan_x = tan_y * ASPECT
        # Sweep the full frustum in x and y (fractions of the half-extent at this depth).
        for fx in (-0.9, -0.35, 0.0, 0.4, 0.85):
            for fy in (-0.8, -0.2, 0.0, 0.55, 0.95):
                vx = fx * tan_x * vz
                vy = fy * tan_y * vz
                samples.append(np.array([vx, vy, vz], dtype=np.float64))
    return samples


def main(require_oracle: bool = False) -> int:
    proj = build_projection(FOV_Y_DEG, ASPECT, NEAR, FAR)
    inv_proj = np.linalg.inv(proj)

    abs_tol = 1e-6
    rel_tol = 1e-6
    failures: list[str] = []

    def close(a: float, b: float) -> bool:
        return abs(a - b) <= abs_tol + rel_tol * max(abs(a), abs(b))

    def vclose(a: np.ndarray, b: np.ndarray) -> bool:
        return all(close(float(x), float(y)) for x, y in zip(a, b))

    samples = make_sample_view_positions()
    mul, add = compute_ndc_to_view(inv_proj)

    max_roundtrip = 0.0
    max_fastpath = 0.0
    max_linearize = 0.0
    max_analytic = 0.0

    for vp in samples:
        uv, stored = project(vp, proj)

        # 1. ROUND-TRIP: reconstruct == original view pos.
        recon = reconstruct(uv, stored, inv_proj)
        err = float(np.max(np.abs(recon - vp)) / max(1.0, float(np.max(np.abs(vp)))))
        max_roundtrip = max(max_roundtrip, err)
        if not vclose(recon, vp):
            failures.append(f"round-trip vp={vp} -> recon={recon} (uv={uv}, depth={stored})")

        # 2. CROSS-CHECK (a): shader fast-path XY == full-matrix XY.
        fast_xy = fastpath_view_xy(uv, vp[2], mul, add)
        matrix_xy = recon[:2]
        fp_err = float(np.max(np.abs(fast_xy - matrix_xy)) / max(1.0, float(np.max(np.abs(matrix_xy)))))
        max_fastpath = max(max_fastpath, fp_err)
        if not vclose(fast_xy, matrix_xy):
            failures.append(f"fastpath-xy vp={vp} fast={fast_xy} matrix={matrix_xy}")

        # 3. CROSS-CHECK (b): closed-form linearize == true view-Z == full-matrix view-Z.
        lin = linearize(stored, NEAR, FAR)
        lin_err = max(
            abs(lin - vp[2]) / max(1.0, abs(vp[2])),
            abs(lin - recon[2]) / max(1.0, abs(recon[2])),
        )
        max_linearize = max(max_linearize, lin_err)
        if not (close(lin, float(vp[2])) and close(lin, float(recon[2]))):
            failures.append(f"linearize vp.z={vp[2]} lin={lin} matrix.z={recon[2]}")

        # 4. CROSS-CHECK (c): first-principles analytic pinhole XY == full-matrix XY. Matrix-
        #    inversion-independent → catches a *consistent* y-flip/transpose the round-trip cancels.
        analytic_xy = analytic_pinhole_xy(uv, vp[2], proj)
        an_err = float(np.max(np.abs(analytic_xy - matrix_xy)) / max(1.0, float(np.max(np.abs(matrix_xy)))))
        max_analytic = max(max_analytic, an_err)
        if not vclose(analytic_xy, matrix_xy):
            failures.append(f"analytic-xy vp={vp} analytic={analytic_xy} matrix={matrix_xy}")

    # 5. NEGATIVE CONTROLS: the wrong convention must BREAK the round-trip, or the test
    #    proves nothing. Use an off-center, off-axis sample where each error is visible.
    probe = np.array([0.4, -0.6, 400.0], dtype=np.float64)
    uv_probe, stored_probe = project(probe, proj)

    # (i) omit the uv y-flip: ndc.y = uv.y*2-1 instead of 1-uv.y*2.
    ndc_noflip = np.array(
        [uv_probe[0] * 2.0 - 1.0, uv_probe[1] * 2.0 - 1.0, local_ndc_z(stored_probe), 1.0],
        dtype=np.float64,
    )
    h_noflip = ndc_noflip @ inv_proj
    recon_noflip = h_noflip[:3] / h_noflip[3]
    if vclose(recon_noflip, probe):
        failures.append("NEGATIVE CONTROL FAILED: omitting uv y-flip still round-trips")

    # (ii) transpose invProj (row-vs-column / transpose confusion).
    h_t = np.array(
        [uv_probe[0] * 2.0 - 1.0, 1.0 - uv_probe[1] * 2.0, local_ndc_z(stored_probe), 1.0],
        dtype=np.float64,
    ) @ inv_proj.T
    recon_t = h_t[:3] / h_t[3]
    if vclose(recon_t, probe):
        failures.append("NEGATIVE CONTROL FAILED: transposed invProj still round-trips")

    # (iii) a CONSISTENT y-flip error (wrong in BOTH the forward uv map and the inverse) — the
    #       round-trip cancels it, so items 1-2 pass; the matrix-independent analytic anchor (c)
    #       is what must catch it. This is the exact circularity the anchor exists to close.
    clip3 = np.append(probe, 1.0) @ proj
    ndc3 = clip3 / clip3[3]
    uv_wrong = np.array([(ndc3[0] + 1.0) * 0.5, (ndc3[1] + 1.0) * 0.5], dtype=np.float64)  # wrong y
    ndc_w = np.array(
        [uv_wrong[0] * 2.0 - 1.0, uv_wrong[1] * 2.0 - 1.0, local_ndc_z(stored_probe), 1.0],
        dtype=np.float64,
    )  # matching wrong inverse -> recovers probe (round-trip is fooled)
    h_w = ndc_w @ inv_proj
    recon_w = h_w[:3] / h_w[3]
    if not vclose(recon_w, probe):
        failures.append("NEGATIVE CONTROL SETUP BROKEN: consistent y-flip should still round-trip")
    if vclose(analytic_pinhole_xy(uv_wrong, probe[2], proj), recon_w[:2]):
        failures.append("NEGATIVE CONTROL FAILED: analytic anchor missed a consistent y-flip")

    # 6. CAPTURED-ORACLE: real memory-truth (the SSS lesson — trust the oracle over analysis).
    #    Reconstruct each triple with ITS OWN frame projMat, never the synthetic one.
    off_axis_captured = 0
    for label, uv_t, depth_t, vp_t, proj_rows in CAPTURED_ORACLE:
        cap_inv = np.linalg.inv(np.array(proj_rows, dtype=np.float64))
        recon = reconstruct(np.array(uv_t, dtype=np.float64), depth_t, cap_inv)
        if not vclose(recon, np.array(vp_t, dtype=np.float64)):
            failures.append(f"captured-oracle[{label}] recon={recon} expected={vp_t}")
        if abs(uv_t[0] - 0.5) > 1e-3 and abs(uv_t[1] - 0.5) > 1e-3:
            off_axis_captured += 1

    # Game-verification gate: the synthetic checks share the pinned FOV/handedness, so an
    # exit-0 only proves self-consistency. In --require-oracle mode, refuse to PASS unless a
    # real off-axis captured triple has confirmed the convention matches the game.
    if require_oracle and off_axis_captured < 1:
        failures.append(
            "GAME-VERIFY GATE: --require-oracle set but no off-axis captured oracle triple "
            "(add a real (uv,depth,viewPos,projMat) capture to CAPTURED_ORACLE before go-live)"
        )

    print(f"Tolerance: abs={abs_tol:g}, rel={rel_tol:g} (float64 CPU math, expected exact).")
    print(f"  mode: {'game-verify (--require-oracle)' if require_oracle else 'synthetic (pre-capture)'}")
    print(f"  samples: {len(samples)}")
    print(f"  captured-oracle triples: {len(CAPTURED_ORACLE)} ({off_axis_captured} off-axis)"
          + ("  (none yet -- synthetic checks prove self-consistency only; a captured off-axis"
             " triple is required to prove the convention matches the game)" if not CAPTURED_ORACLE else ""))
    print(f"  max round-trip rel err:  {max_roundtrip:.3e}")
    print(f"  max fastpath-XY rel err: {max_fastpath:.3e}")
    print(f"  max analytic-XY rel err: {max_analytic:.3e}  (matrix-inversion-independent anchor)")
    print(f"  max linearize rel err:   {max_linearize:.3e}")
    print("  negative controls (y-flip omit, transpose, consistent-flip vs anchor): all diverged")

    if failures:
        print("FAIL")
        for f in failures[:20]:
            print(f"  - {f}")
        if len(failures) > 20:
            print(f"  ... and {len(failures) - 20} more")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    # Default (synthetic) mode: passes on the self-consistency + cross-checks; the pre-capture
    # gate. --require-oracle (game-verification) mode additionally FAILS unless >=1 off-axis
    # captured triple round-trips, so an exit-0 here can gate the t0/t13 + SSGI=1 go-live.
    sys.exit(main(require_oracle="--require-oracle" in sys.argv[1:]))
