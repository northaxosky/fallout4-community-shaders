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
  6. CAPTURED-ORACLE HOOK — captured raw camera transforms, world positions, uv, and depth
     from a real frame independently prove the scene projection and expose a stale/wrong invProj.

The synthetic checks (1-5) prove the chain is self-consistent AND that the shader fast-path,
the linearize, and a matrix-independent pinhole anchor all agree — a strong necessary gate.
They still share the FOV magnitude, so only a captured off-axis oracle (item 6) fully proves
the pinned convention matches the *game*; shipping also requires decode to use the proven basis.

Modes:
  default            — synthetic/pre-capture gate; PASSes on checks 1-5 + the oracle self-test
                       teeth (use before a capture; reports any JSON oracle but does not gate on it).
  --require-oracle   — game-verification gate; additionally FAILs unless a real JSON capture
                       proves the corrected scene-projection basis, camera convention, depth anchor,
                       and off-axis coverage. The current decode basis is diagnosed separately.
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
# The in-game hook dumps raw persistent-camera ingredients. This module independently derives:
#   worldPos_r = worldPos_game - posAdjust
#   ndcZ_oracle = (worldToCam @ worldPos_r_h).z / w
#   viewPos_node = Bethesda camera-node rows @ (worldPos_r - camWorldTranslate), axis-remapped
#   corrected scene projection = worldToCam @ inverse(node world-to-view)
#   viewPos_chain = reconstruct(captured uv, oracle ndc.z, inverse(corrected projection))
# The node oracle never uses depth/invProj, while worldToCam independently anchors clip-space depth.
LIVE_LINEARIZER = "A:(d-0.01)/0.99"
# Current-basis A/B residuals remain diagnostics and feed only the stricter B-win guardrail.
# Guardrail #3 blocks only a systematic, >=2x B win; it can never turn a failure into a pass.
CORRECTED_REL_TOL = 0.01
CORRECTED_ABS_TOL = 0.02
CURRENT_BASIS_REL_TOL = 0.05
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
MIN_AXIS_COVERAGE = 2
OFFAXIS_UV = 0.2             # |uv-0.5| beyond this counts as off-axis on that axis.
DEPTH_ANCHOR_NDC_CAP = 0.02
DEPTH_ANCHOR_SIGN_EPS = 1e-9
UV_MATCH_TOL = 0.002         # |uv_json - uv_recomputed|; a mismatch means a projection-convention
#                              disagreement (e.g. y-flip) and the sample is not trusted. In a real
#                              capture uv_json and uv_recomputed both come from worldToCam so
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
NDC_Z_ORACLE_TOL = 2e-3      # Measured +9.26e-4 frustum/near-plane delta; TAA jitter is xy-only.
AXIS_ORDER_NDC_GAP = 0.1
_SWAP_XZ = np.array([[0.0, 0.0, 1.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0]])

_LINEARIZERS = ("A:(d-0.01)/0.99", "B:d*1.01-0.01")


def _mat(flat: list[float]) -> np.ndarray:
    """16 floats in raw memory order -> 4x4; callers choose the engine's vector convention."""
    return np.array(flat, dtype=np.float64).reshape(4, 4)


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


def _xy_relative_error(actual: np.ndarray, expected: np.ndarray) -> float:
    return float(np.linalg.norm(actual[:2] - expected[:2])
                 / max(float(np.linalg.norm(expected[:2])), 1e-6))


def _horizontal_fov(proj: np.ndarray) -> float:
    scale = abs(float(proj[0, 0]))
    if not math.isfinite(scale) or scale <= 1e-12:
        raise ValueError("invalid horizontal projection scale")
    return math.degrees(2.0 * math.atan(1.0 / scale))


def evaluate_oracle(data: dict) -> tuple[dict, list[str]]:
    """Independent recompute over a parsed capture. Returns (report, failures). Structural / non-finite
    problems become FAILURES. The corrected chain derives its projection from worldToCam and the
    camera-node transform; the current cam.invProj and A/B depth chains remain diagnostics."""
    failures: list[str] = []
    rows: list[dict] = []
    unresolved: list[dict] = []

    if not isinstance(data, dict):
        return {"rows": rows}, ["capture is not a JSON object"]
    if data.get("schema") != "ssgi-oracle-capture/2":
        return {"rows": rows}, ["capture schema must be ssgi-oracle-capture/2"]
    snaps = data.get("snapshots")
    if not isinstance(snaps, list) or not snaps:
        return {"rows": rows}, ["capture has no snapshots[]"]

    for si, snap in enumerate(snaps):
        if not isinstance(snap, dict):
            failures.append(f"snapshot[{si}]: not an object")
            continue
        for key in ("proj", "invProj", "viewPerPass", "viewProjPerPass", "worldToCam"):
            if not _finite_mat(snap.get(key)):
                failures.append(f"snapshot[{si}]: missing/non-finite {key}")
        vectors: dict[str, np.ndarray] = {}
        for key in ("posAdjust", "camWorldTranslate"):
            try:
                value = np.asarray(snap.get(key), dtype=np.float64)
                valid = value.shape == (3,) and bool(np.all(np.isfinite(value)))
            except (TypeError, ValueError):
                valid = False
            if not valid:
                failures.append(f"snapshot[{si}]: missing/non-finite/mis-shaped {key}")
            else:
                vectors[key] = value
        try:
            rotate_rows = np.asarray(snap.get("camWorldRotateRows"), dtype=np.float64)
            rotate_ok = rotate_rows.shape == (3, 3) and bool(np.all(np.isfinite(rotate_rows)))
        except (TypeError, ValueError):
            rotate_ok = False
        if not rotate_ok:
            failures.append(f"snapshot[{si}]: missing/non-finite/mis-shaped camWorldRotateRows")
        # If any structural piece is bad, we cannot trust this snapshot at all.
        if any(f.startswith(f"snapshot[{si}]:") for f in failures):
            continue

        pa = vectors["posAdjust"]
        cam_world = vectors["camWorldTranslate"]
        wtc = _mat(snap["worldToCam"])
        invp = _mat(snap["invProj"])
        proj = _mat(snap["proj"])
        try:
            if not np.allclose(invp, np.linalg.inv(proj), atol=1e-3, rtol=1e-3):
                failures.append(f"snapshot[{si}]: invProj != inverse(proj) (C++ inversion mismatch)")
        except np.linalg.LinAlgError:
            failures.append(f"snapshot[{si}]: proj not invertible")

        try:
            node_world_to_view = np.eye(4, dtype=np.float64)
            node_world_to_view[:3, :3] = _SWAP_XZ @ rotate_rows
            node_world_to_view[:3, 3] = -(_SWAP_XZ @ rotate_rows) @ cam_world
            scene_proj_col = wtc @ np.linalg.inv(node_world_to_view)
            corrected_proj = scene_proj_col.T
            invp_corrected = np.linalg.inv(corrected_proj)
            if not (np.all(np.isfinite(scene_proj_col)) and np.all(np.isfinite(invp_corrected))):
                raise ValueError("non-finite corrected projection")
            current_hfov = _horizontal_fov(proj)
            scene_hfov = _horizontal_fov(corrected_proj)
        except (ValueError, np.linalg.LinAlgError) as exc:
            failures.append(f"snapshot[{si}]: cannot derive corrected scene projection: {exc}")
            continue

        samples = snap.get("samples")
        if not isinstance(samples, list):
            failures.append(f"snapshot[{si}]: samples is not a list")
            continue
        for sj, s in enumerate(samples):
            if not isinstance(s, dict):
                failures.append(f"snapshot[{si}].sample[{sj}]: not an object")
                continue
            resolved = s.get("resolved")
            reason = s.get("culledReason")
            if not isinstance(resolved, bool) or not isinstance(reason, str):
                failures.append(f"snapshot[{si}].sample[{sj}]: missing/malformed resolved/culledReason")
                continue
            if not resolved:
                if reason not in {
                    "no_ref", "no_3d", "behind_camera", "offscreen", "depth_read_failed"
                }:
                    failures.append(f"snapshot[{si}].sample[{sj}]: invalid culledReason '{reason}'")
                    continue
                unresolved.append({
                    "snap": si, "sample": sj, "formId": s.get("formId"),
                    "reason": reason, "worldPos": s.get("worldPos"),
                })
                continue
            if reason:
                failures.append(f"snapshot[{si}].sample[{sj}]: resolved sample has culledReason")
                continue
            try:
                wp = np.asarray(s["worldPos"], dtype=np.float64)
                uv_json = np.asarray(s["uv"], dtype=np.float64)
                pixel = np.asarray(s["pixel"], dtype=np.float64)
                stored = float(s["storedDepth"])
            except (KeyError, TypeError, ValueError):
                failures.append(
                    f"snapshot[{si}].sample[{sj}]: missing/malformed worldPos/uv/pixel/storedDepth")
                continue
            if not (wp.shape == (3,) and uv_json.shape == (2,) and np.all(np.isfinite(wp))
                    and pixel.shape == (2,) and np.all(np.isfinite(uv_json))
                    and np.all(np.isfinite(pixel)) and math.isfinite(stored)):
                failures.append(
                    f"snapshot[{si}].sample[{sj}]: non-finite/mis-shaped worldPos/uv/pixel/storedDepth")
                continue

            try:
                wr = wp - pa
                clip = wtc @ np.append(wr, 1.0)           # column-vector engine convention
                if not np.all(np.isfinite(clip)) or clip[3] <= 0.0:
                    failures.append(f"snapshot[{si}].sample[{sj}]: resolved sample has invalid clip")
                    continue
                ndc = clip[:3] / clip[3]
                ndc_z_oracle = float(ndc[2])
                uv_calc = np.array([(ndc[0] + 1.0) * 0.5, (1.0 - ndc[1]) * 0.5])
                uv_gap = float(np.max(np.abs(uv_calc - uv_json)))
                view_bsda = rotate_rows @ (wr - cam_world)
                oracle = np.array([view_bsda[2], view_bsda[1], view_bsda[0]], dtype=np.float64)

                node_clip = np.append(oracle, 1.0) @ proj
                ndc_z_node = (float(node_clip[2] / node_clip[3])
                              if np.all(np.isfinite(node_clip)) and abs(node_clip[3]) > 1e-12
                              else math.inf)
                oracle_ndc_gap = abs(ndc_z_node - ndc_z_oracle)
                oracle_ndc_signed = ndc_z_node - ndc_z_oracle

                linearized = linearize_variants(stored)
                depth_anchor_delta = linearized[LIVE_LINEARIZER] - ndc_z_oracle
                depth_anchor_sample_ok = (
                    -DEPTH_ANCHOR_NDC_CAP <= depth_anchor_delta <= DEPTH_ANCHOR_SIGN_EPS)
                first_person = stored < VIEWPORT_MIN_DEPTH_FLOOR
                in_frame = bool(np.all(uv_json >= IN_FRAME_BORDER)
                                and np.all(uv_json < IN_FRAME_MAX - IN_FRAME_BORDER))

                res: dict[str, float] = {}
                resvec: dict[str, np.ndarray] = {}
                chains: dict[str, np.ndarray] = {}
                for name, lz in linearized.items():
                    chain = _reconstruct_from_uv(uv_json, lz, invp)
                    if chain is None:
                        res[name] = math.inf
                        continue
                    res[name] = float(np.linalg.norm(chain - oracle))
                    resvec[name] = chain - oracle
                    chains[name] = chain

                chain_current = _reconstruct_from_uv(uv_json, ndc_z_oracle, invp)
                chain_corrected = _reconstruct_from_uv(uv_json, ndc_z_oracle, invp_corrected)
                if chain_corrected is not None:
                    chain_clip = np.append(chain_corrected, 1.0) @ corrected_proj
                    ndc_z_chain = (float(chain_clip[2] / chain_clip[3])
                                     if np.all(np.isfinite(chain_clip)) and abs(chain_clip[3]) > 1e-12
                                     else math.inf)
                else:
                    ndc_z_chain = math.inf
                ndc_chain_gap = abs(ndc_z_chain - ndc_z_oracle)

                corrected_abs = (float(np.linalg.norm(chain_corrected - oracle))
                                 if chain_corrected is not None else math.inf)
                corrected_rel = (_xy_relative_error(chain_corrected, oracle)
                                 if chain_corrected is not None else math.inf)
                current_abs = (float(np.linalg.norm(chain_current - oracle))
                               if chain_current is not None else math.inf)
                current_rel = (_xy_relative_error(chain_current, oracle)
                               if chain_current is not None else math.inf)
                current_scale = (float(np.linalg.norm(oracle[:2]))
                                 / max(float(np.linalg.norm(chain_current[:2])), 1e-6)
                                 if chain_current is not None else math.inf)
                eligible = ((not first_person) and in_frame and (uv_gap < UV_MATCH_TOL)
                            and (stored < 1.0) and chain_corrected is not None)

                rows.append({
                    "snap": si, "sample": sj, "formId": s.get("formId"),
                    "worldPos": wp, "posAdjust": pa, "uv": uv_calc, "uv_json": uv_json,
                    "pixel": pixel, "stored": stored, "ndcZ_oracle": ndc_z_oracle,
                    "ndcZ_node": ndc_z_node, "ndcZ_chain": ndc_z_chain,
                    "oracle_ndc_gap": oracle_ndc_gap, "oracle_ndc_signed": oracle_ndc_signed,
                    "ndc_chain_gap": ndc_chain_gap, "depth_anchor_delta": depth_anchor_delta,
                    "depth_anchor_sample_ok": depth_anchor_sample_ok,
                    "oracle": oracle, "chains": chains, "chain_current": chain_current,
                    "chain_corrected": chain_corrected,
                    "corrected_abs": corrected_abs, "corrected_rel": corrected_rel,
                    "current_abs": current_abs, "current_rel": current_rel,
                    "current_scale": current_scale,
                    "current_hfov": current_hfov, "scene_hfov": scene_hfov,
                    "uv_gap": uv_gap,
                    "oracle_dist": float(np.linalg.norm(oracle)),
                    "first_person": first_person, "in_frame": in_frame, "eligible": bool(eligible),
                    "oax": abs(uv_json[0] - 0.5) > OFFAXIS_UV, "oay": abs(uv_json[1] - 0.5) > OFFAXIS_UV,
                    "res": res, "resvec": resvec,
                })
            except Exception as exc:  # any residual numeric surprise -> structural failure, never a crash
                failures.append(f"snapshot[{si}].sample[{sj}]: reconstruction error: {exc}")
                continue

    return {"rows": rows, "unresolved": unresolved}, failures


def oracle_verdict(report: dict, structural_failures: list[str]) -> dict:
    """Gate the corrected scene basis while retaining the current decode and A/B paths as diagnostics."""
    rows = report.get("rows", [])
    non_near = [r for r in rows if not r["first_person"]]
    eligible = [r for r in rows if r["eligible"]]
    subset = [r for r in eligible if r["oax"] or r["oay"]]
    n_oax = sum(1 for r in eligible if r["oax"])
    n_oay = sum(1 for r in eligible if r["oay"])

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
    ndc_chain_max = stat([r["ndc_chain_gap"] for r in eligible], np.max)
    oracle_ndc_max = stat([r["oracle_ndc_gap"] for r in non_near], np.max)
    oracle_ndc_mean = stat([r["oracle_ndc_signed"] for r in non_near], np.mean)
    oracle_ndc_std = stat([r["oracle_ndc_signed"] for r in non_near], np.std)
    corrected_abs_max = stat([r["corrected_abs"] for r in eligible], np.max)
    corrected_rel_max = stat([r["corrected_rel"] for r in eligible], np.max)
    current_abs_max = stat([r["current_abs"] for r in eligible], np.max)
    current_rel_max = stat([r["current_rel"] for r in eligible], np.max)
    current_rel_median = stat([r["current_rel"] for r in eligible], np.median)
    current_scale_median = stat([r["current_scale"] for r in eligible], np.median)
    current_hfov = stat([r["current_hfov"] for r in eligible], np.median)
    scene_hfov = stat([r["scene_hfov"] for r in eligible], np.median)
    depth_anchor_min = stat([r["depth_anchor_delta"] for r in eligible], np.min)
    depth_anchor_max = stat([r["depth_anchor_delta"] for r in eligible], np.max)
    linearizer_gap_median = stat([
        abs(linearize_variants(r["stored"])[_LINEARIZERS[0]]
            - linearize_variants(r["stored"])[_LINEARIZERS[1]])
        for r in eligible
    ], np.median)
    linearizer_gap_max = stat([
        abs(linearize_variants(r["stored"])[_LINEARIZERS[0]]
            - linearize_variants(r["stored"])[_LINEARIZERS[1]])
        for r in eligible
    ], np.max)
    finite_medians = {k: v for k, v in rel_medians.items() if math.isfinite(v)}
    winner = min(finite_medians, key=finite_medians.get) if finite_medians else None

    coverage_ok = (n_oax >= MIN_AXIS_COVERAGE) and (n_oay >= MIN_AXIS_COVERAGE)
    depth_anchor_ok = bool(eligible) and all(r["depth_anchor_sample_ok"] for r in eligible)
    corrected_rel_ok = math.isfinite(corrected_rel_max) and corrected_rel_max < CORRECTED_REL_TOL
    corrected_abs_ok = math.isfinite(corrected_abs_max) and corrected_abs_max < CORRECTED_ABS_TOL
    corrected_within_tol = corrected_rel_ok and corrected_abs_ok
    current_basis_ok = math.isfinite(current_rel_max) and current_rel_max < CURRENT_BASIS_REL_TOL
    ndc_chain_ok = math.isfinite(ndc_chain_max) and ndc_chain_max < NDC_Z_ORACLE_TOL
    oracle_axes_ok = math.isfinite(oracle_ndc_max) and oracle_ndc_max < NDC_Z_ORACLE_TOL
    b = _LINEARIZERS[1]

    # Guardrail #3 blocks only a systematic, meaningful, >=2x B win.
    ab = [(r["res"].get(LIVE_LINEARIZER, math.inf), r["res"].get(b, math.inf)) for r in subset]
    ab = [(av, bv) for av, bv in ab if math.isfinite(av) and math.isfinite(bv)]
    b_win_frac = (sum(1 for av, bv in ab if bv < av) / len(ab)) if ab else 0.0
    med_a, med_b = medians[LIVE_LINEARIZER], medians[b]
    b_advantage = (med_a - med_b) if (math.isfinite(med_a) and math.isfinite(med_b)) else -math.inf
    b_better = ((b_win_frac >= B_WIN_FRAC_FLOOR) and math.isfinite(med_a) and math.isfinite(med_b)
                and (med_a > B_MEANINGFUL_FLOOR) and (med_b <= med_a / B_ACCURACY_RATIO))

    ok = ((not structural_failures) and coverage_ok and depth_anchor_ok and corrected_within_tol
          and ndc_chain_ok and oracle_axes_ok and (not b_better))

    diagnostic_rows = [r for r in non_near if LIVE_LINEARIZER in r["resvec"]]
    a_vecs = np.array([r["resvec"][LIVE_LINEARIZER] for r in diagnostic_rows], dtype=np.float64)
    mean_vec = a_vecs.mean(axis=0) if a_vecs.size else np.zeros(3)
    mean_norm = float(np.linalg.norm(mean_vec))
    indiv = float(np.mean(np.linalg.norm(a_vecs, axis=1))) if a_vecs.size else 0.0
    translation_like = (indiv > 1e-3) and (mean_norm / max(indiv, 1e-9) > 0.8)

    reasons: list[str] = []
    if structural_failures:
        reasons.append(f"{len(structural_failures)} structural/finiteness failure(s)")
    if not coverage_ok:
        reasons.append(f"insufficient off-axis non-near coverage (X={n_oax}, Y={n_oay}, "
                       f"need >={MIN_AXIS_COVERAGE} each)")
    if non_near and not oracle_axes_ok:
        if oracle_ndc_max >= AXIS_ORDER_NDC_GAP:
            reasons.append(f"Oracle (i) node-transform and Oracle (ii) worldToCam differ by O(1) in "
                           f"ndc.z (max {oracle_ndc_max:.3e}) -- node axis order is wrong")
        else:
            reasons.append(f"Oracle (i)/(ii) ndc.z gap {oracle_ndc_max:.3e} exceeds the measured "
                           f"frustum tolerance {NDC_Z_ORACLE_TOL:.1e}; inspect camera/frustum timing")
    if eligible and not ndc_chain_ok:
        reasons.append(f"corrected chain ndc.z round-trip max {ndc_chain_max:.3e} exceeds "
                       f"{NDC_Z_ORACLE_TOL:.1e}")
    if not depth_anchor_ok:
        reasons.append(f"surface/pivot depth anchor must stay in [-{DEPTH_ANCHOR_NDC_CAP:.2f}, 0): "
                       f"observed [{depth_anchor_min:.4f}, {depth_anchor_max:.4f}] after near filtering")
    if not corrected_rel_ok:
        reasons.append(f"CORRECTED scene basis max XY relative residual {corrected_rel_max:.4%} >= "
                       f"{CORRECTED_REL_TOL:.1%}")
    if not corrected_abs_ok:
        reasons.append(f"CORRECTED scene basis max absolute residual {corrected_abs_max:.4f}u >= "
                       f"{CORRECTED_ABS_TOL:.2f}u")
    if b_better:
        reasons.append(f"B ({b}) reconstructs the oracle SYSTEMATICALLY better than the shipped A "
                       f"(B beats A on {b_win_frac:.0%} of samples, median A={med_a:.4f} vs B={med_b:.4f} "
                       f"game-units, >={B_ACCURACY_RATIO:.0f}x more accurate) -- the game may store depth "
                       "under B; STOP and understand before switching decode.cs.hlsl to B")

    advisory: list[str] = []
    if corrected_within_tol and not current_basis_ok:
        if abs(current_hfov - scene_hfov) > 1.0:
            advisory.append(
                f"DECODE BUG: decode's cam.proj is the WRONG FOV ({current_hfov:.0f}deg vs scene "
                f"{scene_hfov:.0f}deg); view-space XY reconstructs ~2.25x too small; FIX: source the "
                "correct scene projection (worldToCam-derived) for decode's InvProj/NDCToView. "
                f"The CORRECTED basis is proven convention-correct (residual max rel "
                f"{corrected_rel_max:.2e}, max abs {corrected_abs_max:.4f}u). GO-LIVE additionally "
                "requires decode.cs.hlsl / TryGetCameraMatrices to source that basis.")
        else:
            advisory.append("DECODE BUG: cam.invProj disagrees with the proven scene basis; "
                            "source worldToCam-derived InvProj/NDCToView before go-live.")
    if (not corrected_within_tol) and translation_like:
        advisory.append(f"DIAGNOSIS: A residual is a CONSTANT view-space offset {mean_vec.round(3)} "
                        f"(|mean|={mean_norm:.3f} ~ per-sample {indiv:.3f}) -> likely posAdjust current-"
                        "vs-previous latency/sign (guardrail #4), not a convention bug -- re-capture "
                        "stationary to remove it, or confirm benign.")
    advisory.append(f"NOTE: A/B local-ndc separation is only median {linearizer_gap_median:.2e}, "
                    f"max {linearizer_gap_max:.2e}; pivots cannot distinguish them. Keep shipped A; "
                    "the B-win guardrail remains stricter-only.")
    return {
        "ok": ok, "winner": winner, "medians": medians, "maxes": maxes,
        "rel_medians": rel_medians, "rel_maxes": rel_maxes,
        "ndc_chain_max": ndc_chain_max, "oracle_ndc_max": oracle_ndc_max,
        "oracle_ndc_mean": oracle_ndc_mean, "oracle_ndc_std": oracle_ndc_std,
        "ndc_chain_ok": ndc_chain_ok, "oracle_axes_ok": oracle_axes_ok,
        "depth_anchor_ok": depth_anchor_ok,
        "depth_anchor_min": depth_anchor_min, "depth_anchor_max": depth_anchor_max,
        "corrected_abs_max": corrected_abs_max, "corrected_rel_max": corrected_rel_max,
        "corrected_abs_ok": corrected_abs_ok, "corrected_rel_ok": corrected_rel_ok,
        "corrected_within_tol": corrected_within_tol,
        "current_abs_max": current_abs_max, "current_rel_max": current_rel_max,
        "current_rel_median": current_rel_median, "current_scale_median": current_scale_median,
        "current_basis_ok": current_basis_ok,
        "current_hfov": current_hfov, "scene_hfov": scene_hfov,
        "linearizer_gap_median": linearizer_gap_median, "linearizer_gap_max": linearizer_gap_max,
        "b_better": b_better, "b_advantage": b_advantage, "translation_like": translation_like,
        "mean_offset": mean_vec.tolist(),
        "n_non_near": len(non_near), "n_eligible": len(eligible),
        "n_subset": len(subset), "n_oax": n_oax, "n_oay": n_oay,
        "subset": subset, "reasons": reasons, "advisory": advisory,
    }


def summarize_oracle(report: dict, verdict: dict) -> list[str]:
    rows = report.get("rows", [])
    unresolved = report.get("unresolved", [])
    lines = [f"    {len(rows)} resolved, {len(unresolved)} unresolved, "
             f"{len(rows) - verdict['n_non_near']} near-filtered, {verdict['n_eligible']} eligible, "
             f"{verdict['n_subset']} off-axis (X={verdict['n_oax']}, Y={verdict['n_oay']})"]
    lines.append(f"    world->view convention, node vs worldToCam ndc.z: "
                 f"signed mean={verdict['oracle_ndc_mean']:+.3e} std={verdict['oracle_ndc_std']:.3e} "
                 f"max abs={verdict['oracle_ndc_max']:.3e}, tol={NDC_Z_ORACLE_TOL:.1e} "
                 f"({'PASS' if verdict['oracle_axes_ok'] else 'FAIL'})")
    lines.append(f"    y-flip/depth anchor linA(stored)-pivot ndc.z: "
                 f"range=[{verdict['depth_anchor_min']:.4f},{verdict['depth_anchor_max']:.4f}], "
                 f"expected [-{DEPTH_ANCHOR_NDC_CAP:.2f},0) "
                 f"({'PASS' if verdict['depth_anchor_ok'] else 'FAIL'})")
    lines.append(f"    CORRECTED basis (worldToCam-derived): max abs={verdict['corrected_abs_max']:.6f}u "
                 f"(tol {CORRECTED_ABS_TOL:.2f}u), max XY rel={verdict['corrected_rel_max']:.6%} "
                 f"(tol {CORRECTED_REL_TOL:.1%}) "
                 f"({'PASS' if verdict['corrected_within_tol'] else 'FAIL'})")
    lines.append(f"    CURRENT decode basis (cam.invProj): median XY rel={verdict['current_rel_median']:.2%}, "
                 f"max={verdict['current_rel_max']:.2%}, max abs={verdict['current_abs_max']:.3f}u "
                 f"({'PASS' if verdict['current_basis_ok'] else 'FAIL -- diagnosed, not convention-gating'})")
    lines.append(f"    projection diagnosis: cam.proj hfov={verdict['current_hfov']:.1f}deg, "
                 f"scene hfov={verdict['scene_hfov']:.1f}deg, "
                 f"median oracle/current XY scale={verdict['current_scale_median']:.3f}x")
    lines.append(f"    corrected-chain ndc.z round-trip: max={verdict['ndc_chain_max']:.3e}, "
                 f"tol={NDC_Z_ORACLE_TOL:.1e} ({'PASS' if verdict['ndc_chain_ok'] else 'FAIL'})")
    lines.append(f"    A/B local-ndc separation: median={verdict['linearizer_gap_median']:.3e}, "
                 f"max={verdict['linearizer_gap_max']:.3e} (pivot capture cannot resolve; ship A)")
    for k in _LINEARIZERS:
        med, mx = verdict["medians"][k], verdict["maxes"][k]
        rmed, rmx = verdict["rel_medians"][k], verdict["rel_maxes"][k]
        lines.append(f"    current-basis {k}: median={med:.4f} max={mx:.4f}u | "
                     f"rel median={rmed:.4%} max={rmx:.4%} (diagnostic only)")
    winner = verdict["winner"]
    if winner:
        lines.append(f"    A/B diagnostic winner (min median) = {winner}")
    for note in verdict.get("advisory", []):
        lines.append(f"    {note}")
    if rows:
        lines.append("    per-sample raw ingredients and derived oracles:")
        for r in rows:
            fid = r["formId"]
            fid_s = f"0x{fid:X}" if isinstance(fid, int) else str(fid)
            oracle = r["oracle"]
            chain_a = r["chains"].get(_LINEARIZERS[0], np.full(3, math.nan))
            chain_b = r["chains"].get(_LINEARIZERS[1], np.full(3, math.nan))
            chain_current = (r["chain_current"] if r["chain_current"] is not None
                             else np.full(3, math.nan))
            chain_corrected = (r["chain_corrected"] if r["chain_corrected"] is not None
                               else np.full(3, math.nan))
            lines.append(
                f"      {fid_s} uv={np.array2string(r['uv_json'], precision=6)} "
                f"storedDepth={r['stored']:.9f} worldPos={np.array2string(r['worldPos'], precision=3)} "
                f"posAdjust={np.array2string(r['posAdjust'], precision=3)} "
                f"near={int(r['first_person'])} eligible={int(r['eligible'])}")
            lines.append(
                f"        viewPos_oracle_i={np.array2string(oracle, precision=6)} "
                f"current={np.array2string(chain_current, precision=6)} "
                f"corrected={np.array2string(chain_corrected, precision=6)}")
            lines.append(
                f"        chain_A={np.array2string(chain_a, precision=6)} "
                f"chain_B={np.array2string(chain_b, precision=6)} "
                f"ndcZ_oracle={r['ndcZ_oracle']:.9f} "
                f"anchorDelta={r['depth_anchor_delta']:+.6f} "
                f"currentXYrel={r['current_rel']:.4%} correctedXYrel={r['corrected_rel']:.6%}")
    if unresolved:
        lines.append("    unresolved refs:")
        for r in unresolved:
            fid = r["formId"]
            fid_s = f"0x{fid:X}" if isinstance(fid, int) else str(fid)
            lines.append(f"      {fid_s} culledReason={r['reason']} worldPos={r['worldPos']}")
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
    inv_view = np.linalg.inv(view)
    pa = np.array([100.0, -50.0, 20.0], dtype=np.float64)
    view_rot = view[:3, :3]
    rotate_rows = np.stack((view_rot[:, 2], view_rot[:, 1], view_rot[:, 0]))
    per_pass = np.array(
        [[1.0, 0.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0],
         [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
        dtype=np.float64,
    )

    samples = []
    for i, vpv in enumerate(make_sample_view_positions()):
        vpv = np.asarray(vpv, dtype=np.float64)
        if inject == "offscreen":
            # Keep the control resolved to exercise the evaluator's in-frame guard.
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
            "resolved": True,
            "culledReason": "",
            "worldPos": (world_r + pa).tolist(),
            "uv": uv,
            "pixel": [
                int(np.clip(math.floor(uv[0] * 2560), 0, 2559)),
                int(np.clip(math.floor(uv[1] * 1440), 0, 1439)),
            ],
            "storedDepth": stored,
        })

    rotate_out = rotate_rows.copy()
    pa_emit = pa.copy()
    proj_out = proj
    if inject == "transpose_view":
        rotate_out = rotate_rows.T
    elif inject == "translation":
        # Emit stale posAdjust to test constant-offset diagnosis.
        pa_emit = pa + np.array([8.0, 8.0, 8.0])
    elif inject == "wrong_fov":
        proj_out = build_projection(28.0, ASPECT, NEAR, FAR)
    elif inject == "nan":
        rotate_out[1, 1] = float("nan")
    invp_out = np.linalg.inv(proj_out)

    return {
        "schema": "ssgi-oracle-capture/2",
        "allocDim": [2560, 1440],
        "snapshots": [{
            "frame": 512, "frameDim": [2560, 1440], "posAdjust": pa_emit.tolist(),
            "proj": proj_out.flatten().tolist(),
            "invProj": invp_out.flatten().tolist(),
            "viewPerPass": per_pass.flatten().tolist(),
            "viewProjPerPass": (per_pass @ proj_out).flatten().tolist(),
            "worldToCam": viewproj.T.flatten().tolist(),
            "camWorldTranslate": cam_world.tolist(),
            "camWorldRotateRows": rotate_out.tolist(),
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
            and verdict["rel_maxes"][LIVE_LINEARIZER] < 1e-6
            and verdict["corrected_rel_max"] < 1e-5):
        fails.append(f"oracle self-test: clean synthetic should PASS with winner A ~0 "
                     f"(ok={verdict['ok']}, winner={verdict['winner']}, "
                     f"relMaxA={verdict['rel_maxes'].get(LIVE_LINEARIZER)}, reasons={verdict['reasons']})")

    # Every bug class must be REJECTED by the gate, each via its intended branch: transpose = node rows
    # inconsistent with worldToCam; yflip/firstperson/offscreen lose coverage; occlusion fails the
    # depth anchor; nan is structural; b_encoding trips guardrail #3; translation stays diagnostic.
    controls = {
        "transpose_view": "transposed camera-node rows disagree with worldToCam",
        "yflip": "uv y-flip -> off-axis-Y coverage cannot be met",
        "firstperson": "stored<0.01 first-person partition the shader masks -> no coverage",
        "occlusion": "surface too far in front of pivot -> depth-anchor rejection",
        "offscreen": "uv off-screen (edge-clamped depth) -> in-frame check rejects -> no coverage",
        "nan": "non-finite matrix element -> structural failure, no crash / no false PASS",
        "b_encoding": "game stores depth under B, not A -> B reconstructs meaningfully better -> STOP",
        "translation": "constant posAdjust-latency offset -> corrected/coverage rejection",
    }
    for inj, why in controls.items():
        rep, ofail = evaluate_oracle(synth_oracle_capture(inj))
        verdict = oracle_verdict(rep, ofail)
        if verdict["ok"]:
            fails.append(f"oracle self-test: control '{inj}' ({why}) PASSED the gate -> NO teeth "
                         f"(winner={verdict['winner']}, relMaxA={verdict['rel_maxes'].get(LIVE_LINEARIZER)})")

    # Keep B-encoding and translation diagnoses distinct.
    rep_be, f_be = evaluate_oracle(synth_oracle_capture("b_encoding"))
    v_be = oracle_verdict(rep_be, f_be)
    if not v_be.get("b_better"):
        fails.append(f"oracle self-test: b_encoding must trip the guardrail-#3 systematic-B block "
                     f"(b_advantage={v_be.get('b_advantage')}, need frac>={B_WIN_FRAC_FLOOR} & "
                     f">={B_ACCURACY_RATIO}x more accurate)")
    rep_axis, f_axis = evaluate_oracle(synth_oracle_capture("transpose_view"))
    v_axis = oracle_verdict(rep_axis, f_axis)
    if v_axis.get("oracle_axes_ok"):
        fails.append("oracle self-test: transposed node rows must fail the Oracle (i)/(ii) ndc.z cross-check")
    rep_fov, f_fov = evaluate_oracle(synth_oracle_capture("wrong_fov"))
    v_fov = oracle_verdict(rep_fov, f_fov)
    if not (v_fov.get("ok") and v_fov.get("corrected_within_tol")
            and not v_fov.get("current_basis_ok")
            and any("WRONG FOV" in note for note in v_fov.get("advisory", []))):
        fails.append(f"oracle self-test: wrong_fov must prove the corrected basis while diagnosing "
                     f"the current basis (ok={v_fov.get('ok')}, corrected={v_fov.get('corrected_within_tol')}, "
                     f"current={v_fov.get('current_basis_ok')})")
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
    #    ground-truth viewPos comes from the camera node, sharing only the pixel uv.
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
    oracle_basis_fix_required = False
    oracle_requested = oracle_json is not None
    oracle_data = load_oracle_json(oracle_json)
    if oracle_data is not None:
        rep, ofail = evaluate_oracle(oracle_data)
        verdict = oracle_verdict(rep, ofail)
        failures.extend(ofail)
        oracle_lines = ["  captured JSON oracle:"] + summarize_oracle(rep, verdict)
        oracle_ok = verdict["ok"]
        oracle_basis_fix_required = oracle_ok and not verdict["current_basis_ok"]
        if oracle_ok:
            oracle_lines.append("    VERDICT: convention PROVEN on the CORRECTED scene basis; "
                                "pivot depth cannot distinguish A/B, so KEEP shipped A.")
            if oracle_basis_fix_required:
                oracle_lines.append("    SHIPPED DECODE STATUS: BLOCKED -- cam.invProj still carries "
                                    "the wrong FOV; this PASS proves the replacement basis, not the "
                                    "currently sourced decode basis.")
        else:
            for r in verdict["reasons"]:
                oracle_lines.append(f"    oracle gate NOT satisfied: {r}")
    elif oracle_requested:
        oracle_lines = ["  captured JSON oracle: explicit --oracle-json path could not be loaded; "
                        "NOT falling back to any default (gate stays unsatisfied)"]
    else:
        oracle_lines = ["  captured JSON oracle: none found (run the in-game [capture] hook, then "
                        "place oracle_capture.json in scripts/shaders/ or pass --oracle-json)"]

    # Game verification gates the corrected basis; current decode readiness is reported separately.
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

    if oracle_basis_fix_required:
        print("PASS (CONVENTION PROVEN; SHIPPED DECODE BASIS FIX REQUIRED)")
    else:
        print("PASS")
    return 0


if __name__ == "__main__":
    # --require-oracle proves the corrected convention but separately reports current decode readiness.
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
