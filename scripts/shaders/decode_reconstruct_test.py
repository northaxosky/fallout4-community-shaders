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
  default            — synthetic/pre-capture gate; PASSes on checks 1-5 + the oracle self-test
                       teeth (use before a capture; reports any JSON oracle but does not gate on it).
  --require-oracle   — game-verification gate; additionally FAILs unless a real JSON capture
                       satisfies the full verdict: off-axis on-surface coverage in BOTH uv axes, the
                       LIVE linearizer A within (relative) tolerance, all finite. A B-win is an
                       advisory NOTE, not a block (we validate A, never switch to B). No legacy-triple
                       bypass. Run this (green) before the t0/t13 + SSGI=1 go-live.
  --oracle-json PATH — evaluate this capture instead of scripts/shaders/oracle_capture.json. If the
                       explicit path is unreadable the gate stays unsatisfied (no silent fallback).
"""

from __future__ import annotations

import json
import math
import os
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


# --- Independent JSON-capture oracle -----------------------------------------------------------
# The in-game capture hook (ScreenSpaceGI [capture] toml) dumps, per snapshot: the row-major
# view/proj/viewProj/invProj matrices, currentPosAdjust, and per configured static ref a
# (formId, worldPos_game, uv, storedDepth) tuple. This module recomputes EVERYTHING derived here
# (different author from the C++ writer) so the gate is an honest independent check:
#   worldPos_r     = worldPos_game - posAdjust            (rebase into renderer/view space)
#   viewPos_oracle = (worldPos_r, 1) @ view               (GROUND TRUTH; no depth, no invProj)
#   ndc            = ((worldPos_r,1) @ viewProj) / w       (locate the pixel; shares only uv)
#   viewPos_chain  = reconstruct(ndc.xy, linearize(storedDepth), invProj)   (the path under test)
# viewPos_oracle uses the view matrix + a game-sim world position and NEVER the depth->invProj
# chain, so agreement is non-circular. CRITICAL (issue found in review): the chain is reconstructed
# from the CAPTURED uv (uv -> ndc.xy with the y-flip, the actual convention under test) + storedDepth
# + invProj -- NOT from worldPos@viewProj-derived ndc (that trivially round-trips and never consults
# uv or the depth encoding, so it cannot catch a y-flip / handedness / linearization bug).
LIVE_LINEARIZER = "A:(d-0.01)/0.99"  # the encoding the shipped decode.cs.hlsl uses. THE gate proves
#   exactly one thing: this A-chain reconstructs the TRUE oracle viewPos within tolerance across the
#   off-axis on-surface subset. That is the whole go-live question -- "does the shipped decode recover
#   ground-truth view positions" -- so the PASS decision rests ONLY on A-vs-oracle, never on A-vs-B or a
#   fraction threshold (those are diagnostics, below). Earlier revisions tried to auto-classify B-wins
#   and translations with fraction/mean thresholds; an opposite-model review showed every such threshold
#   was gameable (an A-biased surface filter could hide a B-encoding; equal-and-opposite per-snapshot
#   offsets cancelled in a global mean; allowed origin-vs-surface slop tripped a mean-translation block).
#   The robust design is caps-only: A must land within BOTH tolerances vs the independent oracle.
# TWO caps, BOTH evaluated as a MAX over the subset (a max, never a mean -> equal-and-opposite per-
# snapshot errors cannot cancel):
#   - RELATIVE (|chain-oracle|/|oracle|) < RESID_REL_TOL: FO4's projection is extreme (near~8, far~50000)
#     so hyperbolic storedDepth is jammed near 1.0 past ~15m and view-Z is very depth-sensitive; a fixed
#     unit tolerance alone would false-FAIL a correct convention on a mid-depth ref. Relative catches the
#     O(100%) convention bugs (transpose/handedness/y-flip) robustly.
#   - ABSOLUTE (|chain-oracle| game-units) < RESID_ABS_TOL: a backstop for the far field, where a large
#     absolute displacement can hide behind a tiny relative denominator (|oracle| is huge). Catches a
#     constant view translation / a per-snapshot offset that the relative gate would wave through.
# B (the game's approximate linearizer) is REPORTED and enters the decision only to make the gate
# STRICTER: if B reconstructs the oracle SYSTEMATICALLY better than A (B beats A on ~every subset sample
# AND is >=B_ACCURACY_RATIO x more accurate in the median) the gate BLOCKS with "the game may store depth
# under B -- STOP" (guardrail #3). It is scale-invariant on purpose: the A-vs-B intrinsic gap is tiny
# (the two linearizers agree to ~1e-4 in ndc.z), so a fixed unit floor could never fire; a RATIO fires on
# a clean systematic B-win of any magnitude yet a real capture's shared pixel-snap noise (res_A ~ res_B,
# mixed signs) never trips it. Because it can only turn a green into a red (never red->green) it cannot
# introduce a false-PASS. A pure translation shifts A and B EQUALLY -> A still wins -> caught by the
# ABSOLUTE cap instead and LABELLED translation-like (posAdjust latency, guardrail #4) vs rotational (a
# real convention bug). Those labels are advisory; they never flip ok.
RESID_REL_TOL = 0.05         # max RELATIVE residual over the off-axis on-surface subset for "A proven".
#   Robust separation, not knife-edge: a convention bug is O(100%) (transpose ~113%), a correct convention
#   carries only pixel-snap slop (~1-2%), so any tol in ~3-20% gives the same verdict. If a real capture
#   lands A uniformly in ~5-15% (not 100%+), that is pixel-snap -> widen; it is not a convention bug.
RESID_ABS_TOL = 5.0          # max ABSOLUTE residual (game units) over the subset. Above the inherent slop
#   of a well-chosen STATIONARY capture (origin-vs-visible-surface for a flat static + sub-texel pixel-
#   snap + ~0 posAdjust latency when still, all <~2u) and far below a bad translation / convention
#   displacement (tens+ of units). PROVISIONAL like the relative tol -- the reason string reports the
#   actual max; if a correct stationary capture lands here at, say, 3-4u, widen with justification.
MIN_ORACLE_DIST = 16.0       # floor on |oracle| (game units, ~first-person) so a near sample can't
#                              divide-by-tiny and explode the relative residual.
B_WIN_FRAC_FLOOR = 0.9       # B must beat A on this fraction of the subset to count as SYSTEMATIC (not a
#                              coin-flip). Combined with the ratio below => the game likely encodes depth
#                              under B => STOP (guardrail #3). A stricter-only tripwire, so gaming it just
#                              defers to the caps (no false-PASS); shared-noise real captures sit ~0.5.
B_ACCURACY_RATIO = 2.0       # ...AND B's median residual must be <= med_A / this (B >=2x more accurate),
#                              so a systematic-but-negligible tie doesn't fire. Scale-invariant: catches a
#                              clean B-encoding (res_A ~1e-2, res_B ~0) yet not real shared pixel-snap.
B_MEANINGFUL_FLOOR = 1e-4    # ...AND med_A above this (game units), so two bit-exact linearizers whose
#                              LSB noise makes B "win" cannot trip the block. Real captures are >> this.
MIN_AXIS_ONSURFACE = 2       # off-axis on-surface samples required IN EACH of X and Y. Requiring both
#                              axes is what makes a y-flip (invisible only on uv.y==0.5) undetectable-
#                              proof: a real y-flip cannot satisfy the Y-axis requirement.
OFFAXIS_UV = 0.2             # |uv-0.5| beyond this counts as off-axis on that axis.
REL_SURFACE_TOL = 0.02       # on-surface test in VIEW-Z: |vzSurface(stored,proj) - vzOrigin(oracle)| /
#                              |vzOrigin| below this => the ref origin IS the visible surface at its
#                              pixel. View-Z (not raw stored depth) so it rejects a far origin whose
#                              hyperbolic stored value happens to sit within 0.01 of a near surface.
UV_MATCH_TOL = 0.002         # |uv_json - uv_recomputed|; a mismatch means a projection-convention
#                              disagreement (e.g. y-flip) and the sample is not trusted. In a real
#                              capture uv_json and uv_recomputed both come from worldPos@viewProj so
#                              they agree to ~1e-6; 0.002 (~5px @2560w) rejects a corrupted/hand-edited
#                              uv while never false-flagging float noise. (Was 0.01 ~= 23px -- too loose.)
IN_FRAME_MAX = 1.0           # a ref only counts if its captured uv is ON-screen in
#                              [IN_FRAME_BORDER, IN_FRAME_MAX - IN_FRAME_BORDER): the C++ hook writes
#                              refs out to |ndc|<1.2 (uv up to ~1.1) and CLAMPS the depth fetch to the
#                              frame edge, so an off-screen ref reads a neighbour's depth -- garbage.
IN_FRAME_BORDER = 0.003      # reject a border margin (~8px @2560w) so a degenerate edge uv (exactly 0,
#                              or a ref whose floored depth texel is the clamped frame edge) cannot
#                              count -- the shader samples texel CENTRES, never the uv==0 border ray.
VIEWPORT_MIN_DEPTH_FLOOR = VIEWPORT_MIN_DEPTH  # stored < this is the first-person near partition the
#                              live decode.cs.hlsl MASKS (rawDepth<0.01) -> must not count as evidence.

_LINEARIZERS = ("A:(d-0.01)/0.99", "B:d*1.01-0.01")


def _mat(flat: list[float]) -> np.ndarray:
    """16 row-major floats (XMFLOAT4X4 memory order) -> 4x4, applied as row-vector v @ M."""
    return np.array(flat, dtype=np.float64).reshape(4, 4)


def _rowmul(v3: np.ndarray, M: np.ndarray, w: float = 1.0) -> np.ndarray:
    return np.array([v3[0], v3[1], v3[2], w], dtype=np.float64) @ M


def linearize_variants(stored: float) -> dict[str, float]:
    """The two candidate stored-depth -> local NDC z maps. A is the exact inverse of the viewport
    remap stored = 0.01 + ndc.z*0.99; B is the game composite's far-path approximation."""
    return {
        "A:(d-0.01)/0.99": (stored - 0.01) / 0.99,
        "B:d*1.01-0.01": stored * 1.01 - 0.01,
    }


def load_oracle_json(path: str | None = None) -> dict | None:
    """Load a capture. If an EXPLICIT path is supplied it must load or we return None (never silently
    fall back to the default file -- a typo'd --oracle-json must not let the gate pass on stale data)."""
    if path:
        if not os.path.isfile(path):
            print(f"  (oracle json not found at explicit path {path})")
            return None
        try:
            with open(path, "r", encoding="utf-8") as fh:
                return json.load(fh)
        except Exception as exc:
            print(f"  (failed to read oracle json {path}: {exc})")
            return None
    default = os.path.join(os.path.dirname(os.path.abspath(__file__)), "oracle_capture.json")
    if os.path.isfile(default):
        try:
            with open(default, "r", encoding="utf-8") as fh:
                return json.load(fh)
        except Exception as exc:  # pragma: no cover - diagnostic only
            print(f"  (failed to read oracle json {default}: {exc})")
    return None


def _reconstruct_from_uv(uv: np.ndarray, local_z: float, invp: np.ndarray) -> np.ndarray | None:
    """The shader's exact convention: uv -> ndc (y-flip) -> homogeneous @ invProj -> perspective
    divide. This is what puts the uv->ndc convention UNDER TEST (vs. worldPos-derived ndc, which
    bypasses uv entirely and trivially round-trips)."""
    ndc = np.array([uv[0] * 2.0 - 1.0, 1.0 - uv[1] * 2.0, local_z, 1.0], dtype=np.float64)
    h = ndc @ invp
    if not np.isfinite(h[3]) or abs(h[3]) < 1e-12:
        return None
    v = h[:3] / h[3]
    return v if np.all(np.isfinite(v)) else None


def _finite_mat(flat, n=16) -> bool:
    try:
        a = np.asarray(flat, dtype=np.float64)
    except (TypeError, ValueError):
        return False
    return a.size == n and bool(np.all(np.isfinite(a)))


def evaluate_oracle(data: dict) -> tuple[dict, list[str]]:
    """Independent recompute over a parsed capture. Returns (report, failures). Structural / non-finite
    problems become FAILURES (never crashes, never a silent false-PASS). The chain is reconstructed
    from the CAPTURED uv + storedDepth + invProj so the uv->ndc y-flip and the depth encoding are
    actually exercised; the oracle (worldPos_rebased @ view) shares only the pixel, never the depth."""
    failures: list[str] = []
    rows: list[dict] = []

    if not isinstance(data, dict):
        return {"rows": rows}, ["capture is not a JSON object"]
    snaps = data.get("snapshots")
    if not isinstance(snaps, list) or not snaps:
        return {"rows": rows}, ["capture has no snapshots[]"]

    for si, snap in enumerate(snaps):
        if not isinstance(snap, dict):
            failures.append(f"snapshot[{si}]: not an object")
            continue
        for key in ("view", "viewProj", "invProj", "proj"):
            if not _finite_mat(snap.get(key)):
                failures.append(f"snapshot[{si}]: missing/non-finite {key}")
        pa_raw = snap.get("posAdjust")
        try:
            pa = np.asarray(pa_raw, dtype=np.float64)
            pa_ok = pa.shape == (3,) and bool(np.all(np.isfinite(pa)))
        except (TypeError, ValueError):
            pa_ok = False
        if not pa_ok:
            failures.append(f"snapshot[{si}]: missing/non-finite/mis-shaped posAdjust")
        # If any structural piece is bad, we cannot trust this snapshot at all.
        if any(f.startswith(f"snapshot[{si}]:") for f in failures):
            continue

        view = _mat(snap["view"])
        vproj = _mat(snap["viewProj"])
        invp = _mat(snap["invProj"])
        proj = _mat(snap["proj"])
        m22, m32 = float(proj[2, 2]), float(proj[3, 2])
        try:
            if not np.allclose(invp, np.linalg.inv(proj), atol=1e-3, rtol=1e-3):
                failures.append(f"snapshot[{si}]: invProj != inverse(proj) (C++ inversion mismatch)")
        except np.linalg.LinAlgError:
            failures.append(f"snapshot[{si}]: proj not invertible")
        # The oracle rebases worldPos through `view`; the chain reconstructs through `invProj`(=inv(proj));
        # the C++ hook derives each sample's uv through `viewProj`. Those three must be a single coherent
        # snapshot, i.e. viewProj == view @ proj (row-vector). A capture that corrupts `view` alone (a
        # bad/stale camera translation) while leaving viewProj clean would otherwise pass every other
        # check with a silently WRONG oracle. Relative-Frobenius so sub-pixel TAA jitter (which perturbs a
        # couple of entries by ~1/width) is tolerated while a gross translation (orders of magnitude
        # larger) is rejected. Provisional tolerance -- confirm against the first real capture.
        vp_expect = view @ proj
        vp_den = float(np.linalg.norm(vproj)) or 1.0
        if float(np.linalg.norm(vproj - vp_expect)) / vp_den > 0.02:
            failures.append(f"snapshot[{si}]: viewProj != view @ proj (row-vector; rel-Frobenius "
                            f"{float(np.linalg.norm(vproj - vp_expect)) / vp_den:.3g}) -- the oracle's "
                            "view is inconsistent with the chain's proj; capture is internally corrupt")

        samples = snap.get("samples")
        if not isinstance(samples, list):
            failures.append(f"snapshot[{si}]: samples is not a list")
            continue
        for sj, s in enumerate(samples):
            if not isinstance(s, dict):
                failures.append(f"snapshot[{si}].sample[{sj}]: not an object")
                continue
            try:
                wp = np.asarray(s["worldPos"], dtype=np.float64)
                uv_json = np.asarray(s["uv"], dtype=np.float64)
                stored = float(s["storedDepth"])
            except (KeyError, TypeError, ValueError):
                failures.append(f"snapshot[{si}].sample[{sj}]: missing/malformed worldPos/uv/storedDepth")
                continue
            # STRICT 1-D shapes (not just .size): a rank-2 [[x],[y],[z]] has size 3 but would crash the
            # row-vector matmul; reject it as a structural failure instead of tracebacking.
            if not (wp.shape == (3,) and uv_json.shape == (2,) and np.all(np.isfinite(wp))
                    and np.all(np.isfinite(uv_json)) and math.isfinite(stored)):
                failures.append(f"snapshot[{si}].sample[{sj}]: non-finite/mis-shaped worldPos/uv/storedDepth")
                continue

            try:
                wr = wp - pa
                oracle = _rowmul(wr, view)[:3]            # GROUND TRUTH, independent of depth/invProj
                clip = _rowmul(wr, vproj)
                if not np.all(np.isfinite(clip)) or clip[3] <= 0.0:
                    continue                               # behind camera / degenerate; not usable
                ndc = clip[:3] / clip[3]
                uv_calc = np.array([(ndc[0] + 1.0) * 0.5, (1.0 - ndc[1]) * 0.5])
                uv_gap = float(np.max(np.abs(uv_calc - uv_json)))
                vz_origin = float(oracle[2])               # view-space forward Z of the true origin

                # On-surface via VIEW-Z consistency: convert storedDepth (under the LIVE encoding A) to a
                # view-Z through the projection, and compare to the origin's own view-Z. Depth-only: it
                # does NOT consult the xy convention, so it cannot tautologically bless the residual test.
                ndc_z_live = (stored - 0.01) / 0.99
                denom = ndc_z_live - m22
                vz_surface = (m32 / denom) if abs(denom) > 1e-12 else math.inf
                surf_ok = (math.isfinite(vz_surface) and abs(vz_origin) > 1e-6
                           and abs(vz_surface - vz_origin) <= REL_SURFACE_TOL * abs(vz_origin))
                first_person = stored < VIEWPORT_MIN_DEPTH_FLOOR
                in_frame = bool(np.all(uv_json >= IN_FRAME_BORDER)
                                and np.all(uv_json < IN_FRAME_MAX - IN_FRAME_BORDER))

                res: dict[str, float] = {}
                resvec: dict[str, np.ndarray] = {}
                for name, lz in linearize_variants(stored).items():
                    chain = _reconstruct_from_uv(uv_json, lz, invp)
                    if chain is None:
                        res[name] = math.inf
                        continue
                    res[name] = float(np.linalg.norm(chain - oracle))
                    resvec[name] = chain - oracle

                # onsurf gates COVERAGE and the residual subset together, so a row can only count if it is
                # a real on-screen surface AND the LIVE (A) reconstruction is finite -- otherwise an
                # unreconstructable off-axis row could satisfy coverage while being dropped from the max.
                live_ok = math.isfinite(res.get(_LINEARIZERS[0], math.inf))
                onsurf = (surf_ok and (not first_person) and in_frame and (uv_gap < UV_MATCH_TOL)
                          and (stored < 1.0) and live_ok)

                rows.append({
                    "snap": si, "sample": sj, "formId": s.get("formId"),
                    "uv": uv_calc, "uv_json": uv_json, "stored": stored, "ndcZ": float(ndc[2]),
                    "vz_origin": vz_origin, "vz_surface": vz_surface, "uv_gap": uv_gap,
                    "oracle_dist": float(np.linalg.norm(oracle)),
                    "first_person": first_person, "in_frame": in_frame, "onsurf": bool(onsurf),
                    "oax": abs(uv_json[0] - 0.5) > OFFAXIS_UV, "oay": abs(uv_json[1] - 0.5) > OFFAXIS_UV,
                    "res": res, "resvec": resvec,
                })
            except Exception as exc:  # any residual numeric surprise -> structural failure, never a crash
                failures.append(f"snapshot[{si}].sample[{sj}]: reconstruction error: {exc}")
                continue

    return {"rows": rows}, failures


def oracle_verdict(report: dict, structural_failures: list[str]) -> dict:
    """THE gate (used by both --require-oracle and the self-test, so the test exercises the real
    predicate). oracle_ok requires: no structural failures; off-axis on-surface coverage in BOTH uv axes;
    the LIVE linearizer (A) reconstructs the true oracle within BOTH a relative and an absolute MAX cap
    over that subset; and B does not reconstruct the oracle meaningfully better than A (guardrail #3).
    Relative AND absolute (not one alone): relative is depth-robust for the O(100%) convention bugs, the
    absolute cap backstops a far-field displacement that hides behind a huge |oracle| denominator."""
    rows = report.get("rows", [])
    onsurf = [r for r in rows if r["onsurf"]]
    subset = [r for r in onsurf if r["oax"] or r["oay"]]         # off-axis (either axis) on-surface
    n_oax = sum(1 for r in onsurf if r["oax"])
    n_oay = sum(1 for r in onsurf if r["oay"])

    def rel(r, name):
        v = r["res"].get(name, math.inf)
        return v / max(r["oracle_dist"], MIN_ORACLE_DIST) if math.isfinite(v) else math.inf

    def stat(vals, agg):
        vals = [v for v in vals if math.isfinite(v)]
        return agg(vals) if vals else math.inf

    medians = {k: stat([r["res"][k] for r in subset], np.median) for k in _LINEARIZERS}      # absolute
    maxes = {k: stat([r["res"][k] for r in subset], np.max) for k in _LINEARIZERS}            # absolute
    rel_medians = {k: stat([rel(r, k) for r in subset], np.median) for k in _LINEARIZERS}
    rel_maxes = {k: stat([rel(r, k) for r in subset], np.max) for k in _LINEARIZERS}
    finite_medians = {k: v for k, v in rel_medians.items() if math.isfinite(v)}
    winner = min(finite_medians, key=finite_medians.get) if finite_medians else None

    coverage_ok = (n_oax >= MIN_AXIS_ONSURFACE) and (n_oay >= MIN_AXIS_ONSURFACE)
    b = _LINEARIZERS[1]

    # THE decision: the shipped linearizer A reconstructs the TRUE oracle within BOTH caps (each a MAX
    # over the off-axis on-surface subset, so equal-and-opposite per-snapshot errors cannot cancel).
    live_rel_ok = math.isfinite(rel_maxes[LIVE_LINEARIZER]) and rel_maxes[LIVE_LINEARIZER] < RESID_REL_TOL
    live_abs_ok = math.isfinite(maxes[LIVE_LINEARIZER]) and maxes[LIVE_LINEARIZER] < RESID_ABS_TOL
    live_within_tol = live_rel_ok and live_abs_ok

    # Guardrail #3: if B reconstructs the oracle SYSTEMATICALLY better than A, STOP -- the game may encode
    # depth under B. Scale-invariant: B must beat A on >=B_WIN_FRAC_FLOOR of the subset AND be >=
    # B_ACCURACY_RATIO x more accurate in the median (med_B <= med_A / ratio), with med_A meaningfully
    # nonzero. This fires on a clean systematic B-win of ANY magnitude (the A-vs-B gap is only ~1e-4 ndc,
    # far below any fixed unit floor) yet a real capture's shared pixel-snap noise (res_A ~ res_B, mixed
    # signs) sits at frac ~0.5 and never trips. Stricter-only: it can BLOCK but never turn red->green, so
    # it cannot false-PASS. A pure translation shifts A and B equally -> A wins -> caught by the abs cap.
    ab = [(r["res"].get(LIVE_LINEARIZER, math.inf), r["res"].get(b, math.inf)) for r in subset]
    ab = [(av, bv) for av, bv in ab if math.isfinite(av) and math.isfinite(bv)]
    b_win_frac = (sum(1 for av, bv in ab if bv < av) / len(ab)) if ab else 0.0
    med_a, med_b = medians[LIVE_LINEARIZER], medians[b]
    b_advantage = (med_a - med_b) if (math.isfinite(med_a) and math.isfinite(med_b)) else -math.inf
    b_better = ((b_win_frac >= B_WIN_FRAC_FLOOR) and math.isfinite(med_a) and math.isfinite(med_b)
                and (med_a > B_MEANINGFUL_FLOOR) and (med_b <= med_a / B_ACCURACY_RATIO))

    ok = (not structural_failures) and coverage_ok and live_within_tol and (not b_better)

    # Diagnostic character of A's residual (advisory only, never flips ok): a mean-aligned constant
    # offset => posAdjust latency (guardrail #4, re-capture stationary); a mean ~0 => rotational/scaling
    # convention bug. Distinguishes the two FAIL causes for triage; on PASS the residual is ~0.
    a_vecs = np.array([r["resvec"][LIVE_LINEARIZER] for r in subset if LIVE_LINEARIZER in r["resvec"]],
                      dtype=np.float64)
    mean_vec = a_vecs.mean(axis=0) if a_vecs.size else np.zeros(3)
    mean_norm = float(np.linalg.norm(mean_vec))
    indiv = float(np.mean(np.linalg.norm(a_vecs, axis=1))) if a_vecs.size else 0.0
    translation_like = (indiv > 1e-3) and (mean_norm / max(indiv, 1e-9) > 0.8)

    reasons: list[str] = []
    if structural_failures:
        reasons.append(f"{len(structural_failures)} structural/finiteness failure(s)")
    if not coverage_ok:
        reasons.append(f"insufficient off-axis on-surface coverage (X={n_oax}, Y={n_oay}, "
                       f"need >={MIN_AXIS_ONSURFACE} each)")
    if not live_rel_ok:
        reasons.append(f"LIVE linearizer {LIVE_LINEARIZER} max RELATIVE residual "
                       f"{rel_maxes[LIVE_LINEARIZER]:.4%} >= tol {RESID_REL_TOL:.1%} -- A does NOT "
                       "reconstruct the true oracle (convention bug or off-surface refs); do NOT go live")
    elif not live_abs_ok:
        reasons.append(f"LIVE linearizer {LIVE_LINEARIZER} max ABSOLUTE residual "
                       f"{maxes[LIVE_LINEARIZER]:.3f} game-units >= cap {RESID_ABS_TOL:.1f} (relative "
                       f"stayed small behind a far |oracle|) -- a displaced reconstruction "
                       f"({'constant offset ~ posAdjust latency' if translation_like else 'rotational'})"
                       "; do NOT go live")
    if b_better:
        reasons.append(f"B ({b}) reconstructs the oracle SYSTEMATICALLY better than the shipped A "
                       f"(B beats A on {b_win_frac:.0%} of samples, median A={med_a:.4f} vs B={med_b:.4f} "
                       f"game-units, >={B_ACCURACY_RATIO:.0f}x more accurate) -- the game may store depth "
                       "under B; STOP and understand before switching decode.cs.hlsl to B")

    advisory: list[str] = []
    if (not ok) and translation_like and (not live_rel_ok or not live_abs_ok):
        advisory.append(f"DIAGNOSIS: A residual is a CONSTANT view-space offset {mean_vec.round(3)} "
                        f"(|mean|={mean_norm:.3f} ~ per-sample {indiv:.3f}) -> likely posAdjust current-"
                        "vs-previous latency/sign (guardrail #4), not a convention bug -- re-capture "
                        "stationary to remove it, or confirm benign.")
    elif (not ok) and (indiv > 1e-3) and (not live_rel_ok):
        advisory.append(f"DIAGNOSIS: A residual is ROTATIONAL/scaling (|mean|={mean_norm:.3f} << per-"
                        f"sample {indiv:.3f}) -> a genuine convention bug (transpose/handedness/y-flip).")
    if ok and (b_advantage > 0.0):
        advisory.append(f"NOTE: A passed both caps; B won the median by a non-systematic margin "
                        f"(B better on {b_win_frac:.0%} of samples, advantage {b_advantage:.4f}u) -- "
                        "expected benign posAdjust/pixel-snap; informational only.")
    return {
        "ok": ok, "winner": winner, "medians": medians, "maxes": maxes,
        "rel_medians": rel_medians, "rel_maxes": rel_maxes,
        "b_better": b_better, "b_advantage": b_advantage, "translation_like": translation_like,
        "mean_offset": mean_vec.tolist(),
        "n_onsurface": len(onsurf), "n_subset": len(subset), "n_oax": n_oax, "n_oay": n_oay,
        "subset": subset, "reasons": reasons, "advisory": advisory,
    }


def summarize_oracle(report: dict, verdict: dict) -> list[str]:
    rows = report.get("rows", [])
    lines = [f"    {len(rows)} projected, {verdict['n_onsurface']} on-surface, "
             f"{verdict['n_subset']} off-axis on-surface (X={verdict['n_oax']}, Y={verdict['n_oay']})"]
    for k in _LINEARIZERS:
        med, mx = verdict["medians"][k], verdict["maxes"][k]
        rmed, rmx = verdict["rel_medians"][k], verdict["rel_maxes"][k]
        lines.append(f"    {k}: median={med:.4f} max={mx:.4f} game-units | "
                     f"rel median={rmed:.4%} max={rmx:.4%} (over off-axis on-surface)")
    winner = verdict["winner"]
    if winner:
        lines.append(f"    winner (min median) = {winner}")
        vecs = np.array([r["resvec"][winner] for r in verdict["subset"]
                         if winner in r["resvec"]], dtype=np.float64)
        if vecs.size:
            mean_vec = vecs.mean(axis=0)
            mean_norm = float(np.linalg.norm(mean_vec))
            indiv = float(np.mean(np.linalg.norm(vecs, axis=1)))
            if indiv > 1e-3:  # only diagnose when there IS a residual to explain
                if mean_norm / max(indiv, 1e-9) > 0.8:
                    lines.append(f"    residual character: TRANSLATION-like (offset {mean_vec.round(3)} "
                                 f"~ per-sample {indiv:.3f}) -> suspect posAdjust current-vs-previous "
                                 "latency/sign, NOT a convention bug")
                else:
                    lines.append(f"    residual character: ROTATIONAL/scaling (mean offset norm "
                                 f"{mean_norm:.3f} << per-sample {indiv:.3f}) -> genuine convention bug")
    for note in verdict.get("advisory", []):
        lines.append(f"    {note}")
    # Per-sample raw dump so the numbers can be eyeballed (requested at checkpoint).
    if rows:
        lines.append("    per-sample (formId uv stored | vzOrigin vzSurf | onsurf oax oay | resA resB):")
        for r in rows[:24]:
            fid = r["formId"]
            fid_s = f"0x{fid:X}" if isinstance(fid, int) else str(fid)
            lines.append(
                f"      {fid_s} uv=({r['uv_json'][0]:.4f},{r['uv_json'][1]:.4f}) d={r['stored']:.5f} | "
                f"vzO={r['vz_origin']:.1f} vzS={r['vz_surface']:.1f} | "
                f"{int(r['onsurf'])} {int(r['oax'])} {int(r['oay'])} | "
                f"A={r['res'].get(_LINEARIZERS[0]):.3f} B={r['res'].get(_LINEARIZERS[1]):.3f}")
    return lines


def _synth_view(cam_world: np.ndarray, yaw: float, pitch: float) -> np.ndarray:
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    ry = np.array([[cy, 0.0, -sy], [0.0, 1.0, 0.0], [sy, 0.0, cy]], dtype=np.float64)
    rx = np.array([[1.0, 0.0, 0.0], [0.0, cp, sp], [0.0, -sp, cp]], dtype=np.float64)
    rot = rx @ ry  # world->view rotation (row-vector: v_view = v_world @ rot)
    view = np.eye(4, dtype=np.float64)
    view[:3, :3] = rot
    view[3, :3] = -cam_world @ rot
    return view


def synth_oracle_capture(inject: str | None = None) -> dict:
    """Build a synthetic capture whose ground truth we control, then optionally inject a specific
    bug. World points are the inverse-mapped in-frustum view grid, so coverage is off-axis by
    construction and storedDepth uses the exact viewport remap (so linearization A is truth)."""
    proj = build_projection(FOV_Y_DEG, ASPECT, NEAR, FAR)
    cam_world = np.array([1200.0, -3400.0, 780.0], dtype=np.float64)
    view = _synth_view(cam_world, yaw=0.7, pitch=-0.2)
    viewproj = view @ proj                      # kept CLEAN (the projection the pixels came from)
    invp = np.linalg.inv(proj)
    inv_view = np.linalg.inv(view)
    pa = np.array([100.0, -50.0, 20.0], dtype=np.float64)

    samples = []
    for i, vpv in enumerate(make_sample_view_positions()):
        vpv = np.asarray(vpv, dtype=np.float64)
        if inject == "offscreen":
            # Force this ref off the right edge (ndc.x = 1.08) with a fully CONSISTENT worldPos, so
            # uv_calc == uv_json (uv_gap ~ 0) and ONLY the in-frame check can reject it -- this proves
            # the in-frame gate has independent teeth (a real off-screen ref keeps a matching uv because
            # the C++ hook writes worldPos@viewProj regardless of on-screen-ness).
            vpv = vpv.copy()
            vpv[0] = 1.08 * float(vpv[2]) / proj[0, 0]   # ndc.x = vx*p00/vz = 1.08
        world_r = (np.append(vpv, 1.0) @ inv_view)[:3]
        clip = np.append(vpv, 1.0) @ proj
        ndc = clip[:3] / clip[3]
        if inject == "offscreen":
            if max(abs(ndc[0]), abs(ndc[1])) <= 1.0:
                continue                                 # guarantee the control ref really is off-screen
        elif max(abs(ndc[0]), abs(ndc[1])) > 1.0:
            continue  # keep on-screen
        uv = [float((ndc[0] + 1.0) * 0.5), float((1.0 - ndc[1]) * 0.5)]
        stored = float(0.01 + ndc[2] * 0.99)         # exact viewport remap => linearization A is truth
        if inject == "yflip":
            uv[1] = 1.0 - uv[1]                       # wrong y in the capture; recompute disagrees
        elif inject == "b_encoding":
            stored = float((ndc[2] + 0.01) / 1.01)    # only B recovers ndc.z => B wins => must NOT pass
        elif inject == "firstperson":
            stored = 0.005                            # first-person near partition the shader masks
        elif inject == "occlusion":
            vz_near = max(NEAR * 2.0, float(vpv[2]) * 0.3)      # a much nearer surface at this pixel
            near_ndc_z = (proj[2, 2] * vz_near + proj[3, 2]) / vz_near
            stored = float(0.01 + near_ndc_z * 0.99)
        samples.append({
            "formId": 0x1000 + i,
            "worldPos": (world_r + pa).tolist(),
            "uv": uv,
            "storedDepth": stored,
        })

    view_out = view.copy()
    pa_emit = pa.copy()
    if inject == "transpose_view":
        view_out = view.T                       # convention bug: oracle uses a transposed view
    elif inject == "translation":
        # posAdjust latency: worldPos was rebased in-engine with the TRUE pa, but the capture emits a
        # STALE pa (current-vs-previous frame). view/viewProj/proj stay a coherent snapshot (no
        # structural trip); the Python rebases with pa_emit -> a CONSTANT view-space offset of |delta|
        # (~13.9u here) that the ABSOLUTE cap must reject and the report must label translation-like.
        pa_emit = pa + np.array([8.0, 8.0, 8.0])
    elif inject == "nan":
        view_out = view.copy()
        view_out[1, 1] = float("nan")           # non-finite element => structural rejection, no crash

    return {
        "schema": "ssgi-oracle-capture/1",
        "allocDim": [2560, 1440],
        "snapshots": [{
            "frame": 512, "frameDim": [2560, 1440], "posAdjust": pa_emit.tolist(),
            "view": view_out.flatten().tolist(),
            "proj": proj.flatten().tolist(),
            "viewProj": viewproj.flatten().tolist(),
            "invProj": invp.flatten().tolist(),
            "samples": samples,
        }],
    }


def oracle_selftest() -> list[str]:
    """Offline teeth check driving the REAL gate (oracle_verdict), so a self-test pass means the gate
    itself has teeth. Clean synthetic must PASS (winner A ~0); every injected bug class -- convention,
    coverage, structural, a B-encoding, AND a constant posAdjust-latency translation -- must make the gate
    return ok=False, and the two 'looks-fine-but-must-STOP' controls must be triaged correctly (b_better
    for a B-encoding, translation_like for a posAdjust offset). Runs every invocation, no game data."""
    fails: list[str] = []

    rep, ofail = evaluate_oracle(synth_oracle_capture(None))
    verdict = oracle_verdict(rep, ofail)
    if not (verdict["ok"] and verdict["winner"] == LIVE_LINEARIZER
            and verdict["rel_maxes"][LIVE_LINEARIZER] < 1e-6):
        fails.append(f"oracle self-test: clean synthetic should PASS with winner A ~0 "
                     f"(ok={verdict['ok']}, winner={verdict['winner']}, "
                     f"relMaxA={verdict['rel_maxes'].get(LIVE_LINEARIZER)}, reasons={verdict['reasons']})")

    # Every bug class must be REJECTED by the gate, each via its intended branch: transpose = a view
    # inconsistent with the clean viewProj (structural); yflip/firstperson/occlusion/offscreen = no valid
    # off-axis on-surface coverage; nan = structural; b_encoding = B reconstructs the oracle meaningfully
    # better -> guardrail-#3 block; translation = a constant posAdjust offset caught by the ABSOLUTE cap.
    controls = {
        "transpose_view": "transposed view != clean viewProj -> structural rejection",
        "yflip": "uv y-flip -> off-axis-Y coverage cannot be met",
        "firstperson": "stored<0.01 first-person partition the shader masks -> no coverage",
        "occlusion": "ref origin occluded (surface nearer than origin) -> view-Z mismatch, no coverage",
        "offscreen": "uv off-screen (edge-clamped depth) -> in-frame check rejects -> no coverage",
        "nan": "non-finite matrix element -> structural failure, no crash / no false PASS",
        "b_encoding": "game stores depth under B, not A -> B reconstructs meaningfully better -> STOP",
        "translation": "constant posAdjust-latency offset -> absolute cap exceeded -> STOP",
    }
    for inj, why in controls.items():
        rep, ofail = evaluate_oracle(synth_oracle_capture(inj))
        verdict = oracle_verdict(rep, ofail)
        if verdict["ok"]:
            fails.append(f"oracle self-test: control '{inj}' ({why}) PASSED the gate -> NO teeth "
                         f"(winner={verdict['winner']}, relMaxA={verdict['rel_maxes'].get(LIVE_LINEARIZER)})")

    # ...and the DIAGNOSIS on the two "looks-fine-but-must-STOP" controls must be right and unambiguous:
    # b_encoding trips the guardrail-#3 B-advantage block (NOT waved through); a constant translation is
    # caught by the absolute cap and LABELLED translation-like (posAdjust), NOT misreported as a B-win.
    rep_be, f_be = evaluate_oracle(synth_oracle_capture("b_encoding"))
    v_be = oracle_verdict(rep_be, f_be)
    if not v_be.get("b_better"):
        fails.append(f"oracle self-test: b_encoding must trip the guardrail-#3 systematic-B block "
                     f"(b_advantage={v_be.get('b_advantage')}, need frac>={B_WIN_FRAC_FLOOR} & "
                     f">={B_ACCURACY_RATIO}x more accurate)")
    rep_tr, f_tr = evaluate_oracle(synth_oracle_capture("translation"))
    v_tr = oracle_verdict(rep_tr, f_tr)
    if not (v_tr.get("translation_like") and not v_tr.get("b_better")):
        fails.append(f"oracle self-test: translation must diagnose translation_like ONLY "
                     f"(transL={v_tr.get('translation_like')}, b_better={v_tr.get('b_better')})")
    return fails


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


def main(require_oracle: bool = False, oracle_json: str | None = None) -> int:
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

    # 6. CAPTURED-ORACLE (memory-truth; the SSS lesson - trust the oracle over analysis).
    #    Two sources: (legacy) hardcoded CAPTURED_ORACLE triples, and (primary) a JSON capture from
    #    the in-game oracle hook. The JSON path is fully INDEPENDENT of the depth->invProj chain:
    #    ground-truth viewPos = (worldPos - posAdjust) @ view, sharing only the pixel uv.
    off_axis_captured = 0
    for label, uv_t, depth_t, vp_t, proj_rows in CAPTURED_ORACLE:
        cap_inv = np.linalg.inv(np.array(proj_rows, dtype=np.float64))
        recon = reconstruct(np.array(uv_t, dtype=np.float64), depth_t, cap_inv)
        if not vclose(recon, np.array(vp_t, dtype=np.float64)):
            failures.append(f"captured-oracle[{label}] recon={recon} expected={vp_t}")
        if abs(uv_t[0] - 0.5) > 1e-3 and abs(uv_t[1] - 0.5) > 1e-3:
            off_axis_captured += 1

    # Self-test: prove the JSON oracle gate has TEETH (discriminates convention / posAdjust-latency /
    # uv bugs) against synthetic captures, offline, every run. If it loses teeth, fail loudly.
    failures.extend(oracle_selftest())

    # Primary oracle: a real in-game JSON capture (scripts/shaders/oracle_capture.json or
    # --oracle-json). Always evaluated + reported; only GATES under --require-oracle.
    oracle_lines: list[str] = []
    oracle_ok = False
    oracle_requested = oracle_json is not None
    oracle_data = load_oracle_json(oracle_json)
    if oracle_data is not None:
        rep, ofail = evaluate_oracle(oracle_data)
        verdict = oracle_verdict(rep, ofail)
        failures.extend(ofail)
        oracle_lines = ["  captured JSON oracle:"] + summarize_oracle(rep, verdict)
        oracle_ok = verdict["ok"]
        if oracle_ok:
            oracle_lines.append(f"    VERDICT: convention PROVEN; linearization winner "
                                f"{verdict['winner']} -> KEEP decode.cs.hlsl at (depth-0.01)/0.99")
        else:
            for r in verdict["reasons"]:
                oracle_lines.append(f"    oracle gate NOT satisfied: {r}")
    elif oracle_requested:
        oracle_lines = ["  captured JSON oracle: explicit --oracle-json path could not be loaded; "
                        "NOT falling back to any default (gate stays unsatisfied)"]
    else:
        oracle_lines = ["  captured JSON oracle: none found (run the in-game [capture] hook, then "
                        "place oracle_capture.json in scripts/shaders/ or pass --oracle-json)"]

    # Game-verification gate: solely oracle_ok (no legacy-triple bypass). The synthetic checks share
    # the pinned FOV/handedness, so exit-0 without --require-oracle only proves self-consistency;
    # --require-oracle refuses to PASS unless a real capture satisfies the full verdict (off-axis
    # coverage in BOTH uv axes, the LIVE linearizer A within tolerance, all finite, and neither a
    # systematic B-encoding nor a constant translation; a NON-systematic B-win is an advisory note).
    if require_oracle and not oracle_ok:
        failures.append(
            "GAME-VERIFY GATE: --require-oracle set but the JSON oracle gate is not satisfied "
            "(capture oracle_capture.json in-game via the [capture] hook with several off-axis refs) "
            "- required before the t0/t13 + SSGI=1 go-live"
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
    for ln in oracle_lines:
        print(ln)

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
    # Default (synthetic) mode: passes on the self-consistency + cross-checks + the offline oracle
    # self-test (teeth); the pre-capture gate. --require-oracle (game-verification) mode additionally
    # FAILS unless a real off-axis JSON capture confirms the convention, so an exit-0 there gates the
    # t0/t13 + SSGI=1 go-live. --oracle-json <path> (or --oracle-json=<path>) points at the capture.
    # STRICT parsing: any unrecognized token (a misspelled --require-orcale, a --require-oracle=true
    # form, a stray arg) is a hard FAIL (exit 2) -- it must NEVER silently downgrade to the synthetic
    # exit-0 that would falsely authorize go-live.
    _args = sys.argv[1:]
    _require = False
    _oj: str | None = None
    _err: str | None = None
    _i = 0
    while _i < len(_args):
        _tok = _args[_i]
        if _tok == "--require-oracle":
            _require = True
        elif _tok == "--oracle-json":
            _nxt = _args[_i + 1] if _i + 1 < len(_args) else None
            if not _nxt or _nxt.startswith("--"):
                _err = "--oracle-json requires a non-empty path value"
                break
            _oj = _nxt
            _i += 1
        elif _tok.startswith("--oracle-json="):
            _oj = _tok[len("--oracle-json="):]
            if not _oj:
                _err = "--oracle-json= requires a non-empty path value"
                break
        else:
            _err = (f"unrecognized argument '{_tok}' (accepted: --require-oracle, "
                    "--oracle-json <path>, --oracle-json=<path>)")
            break
        _i += 1
    if _err:
        print("FAIL")
        print(f"  - {_err}")
        sys.exit(2)
    sys.exit(main(require_oracle=_require, oracle_json=_oj))
