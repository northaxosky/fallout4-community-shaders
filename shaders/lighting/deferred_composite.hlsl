// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction stub for the FO4 DeferredComposite pixel shader. This is a
// reference reconstruction of Bethesda's compiled shader binary; see
// `shaders/lighting/README.md` and the runbook at
// `Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md`.
//
// Status: WIP - BLOB MISIDENTIFIED
// See `shaders/lighting/shader-2122-analysis.md` for the full negative
// findings. Three independent confirmations from the 2026-05-18 campaign:
//
//   1. Shaders011 blob 2122 (sha1 af996dd590c2...) is NOT a deferred composite.
//      Per-vertex inputs (TEXCOORD/POSITION), sun BRDF math against cb0[2],
//      no albedo SRV. Likely a per-light deferred geometry pass (sun disc,
//      sky dome, distant terrain) that samples the already-written
//      kDiffuseBuffer/kSpecularBuffer at clamped screen UV.
//   2. Shaders011 blob 556 (sha1 3d7efefcfa9c...) is ALSO not a composite -
//      it is a particle/decal shader with discard_nz distance fade.
//   3. DrawWorld::DeferredComposite is a C++ orchestrator that dispatches
//      3 separate RenderPassImmediately calls + 4 RenderGeometryGroup
//      calls across 4 render-target switchovers. There is no single
//      "composite PS"; the composite is a multi-pass operation.
//
// The actual composite PS is identified by live RenderDoc capture eid 45368
// (sha1 prefix 813c9acec23b, 3172 bytes, 6 SRVs, 2 CBs, writes RT 172 =
// kMain). That SHA does not appear in Shaders011.fxp (0/3939 blobs) or in
// Fallout4.unpacked.exe (0/924 embedded blobs). Reconstruction is blocked
// on locating the on-disk source of that bytecode. See
// `shaders/lighting/docs/lighting-shader-followups.md` "Shaders011.2122"
// for the open items and next-step procedure.
//
// Source binding (HOST C++ FUNCTION - still correct):
//   - Function: DrawWorld::DeferredComposite
//     REL::ID array { OG=728427, NG=2318313, AE=2318313 }
//     OG  RVA 0x02855E60 (size 2633b / 587 insn)
//     NG  RVA 0x0209B100 (size 2970b / 639 insn)
//     AE  RVA 0x021F0790 (size 2970b / 639 insn)
//   - Confirmed via Render_PreUI anchor-walk (see
//     Fallout4RE/knowledge/cross-runtime/render-subsystem-manual-overrides.json)
//   - Dispatches 3 separate PS draws - the one we want is the first follow-up
//     to the ambient/IBL pass, RenderDoc eid 45368 in FO4_frame5407.rdc.
//
// Do not commit speculation. The #error stays in place until the actual
// composite PS bytecode is sourced and round-trip verified.

#error \
"shaders/lighting/deferred_composite.hlsl is WIP - blob misidentified. " \
"See shader-2122-analysis.md and docs/lighting-shader-followups.md for " \
"the negative findings and the path to resolution."
