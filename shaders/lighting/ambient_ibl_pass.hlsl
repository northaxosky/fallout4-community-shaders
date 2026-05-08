// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction stub for the FO4 ambient + image-based-lighting deferred
// pixel shader. This is a reference reconstruction of Bethesda's compiled
// shader binary; see `shaders/lighting/README.md` and the runbook at
// `Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md`.
//
// Status: NEEDS-EXTRACTION
//
// Source binding:
//   - Dispatched inside DrawWorld::DeferredLightsImpl
//     REL::ID array { OG=1108521, NG=2318312, AE=2318312 }
//     (see Fallout4RE/exports/cs-lighting-shader-id-map.json)
//   - Reads:
//       t? : kGbufferNormal     (RT 20, R16G16_UNORM, view-space octahedral)
//       t? : kGbufferAlbedo     (RT 22)
//       t? : kGbufferMaterial   (RT 24, glossiness/specular/backlight/SSS)
//       t? : kSSAO              (RT 28, R8_UNORM)
//       t? : main depth         (DSV 2)
//   - Writes:
//       o0 : kDiffuseBuffer     (RT 58, R11G11B10F)  -- ambient term added
//       (does NOT touch kSpecularBuffer for the ambient lobe)
//
// Why we want this shader:
//   The sibling SSGI feature currently post-modulates kDiffuseBuffer with AO
//   (see features/ScreenSpaceGI/Shaders/ApplyAOCS.hlsl). That darkens direct
//   light along with ambient, which is wrong. Knowing exactly how the engine
//   weights AO inside this pass lets SSGI integrate at the correct boundary.
//
// Reconstruction has not been performed: no DXBC blob is available in this
// repo. To populate this file, follow the runbook's "Source A: live RenderDoc"
// or "Source B: extracted .ba2" steps, then transcribe the disassembly here
// per the runbook's per-shader workflow.
//
// Do not commit speculation. Until the bytecode is available, this file
// declares only the bindings we positively know from RT-enum cross-reference
// (sibling repo `src/Engine.h`).

#error \
"shaders/lighting/ambient_ibl_pass.hlsl is a reconstruction stub. " \
"Populate it from a RenderDoc capture or .ba2 extraction following " \
"Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md."
