// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction stub for the FO4 DeferredComposite pixel shader. This is a
// reference reconstruction of Bethesda's compiled shader binary; see
// `shaders/lighting/README.md` and the runbook at
// `Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md`.
//
// Status: NEEDS-EXTRACTION
//
// Source binding:
//   - Function: DrawWorld::DeferredComposite
//     REL::ID array { OG=728427, NG=2318313, AE=2318313 }
//     OG  RVA 0x02855E60 (size 2633b / 587 insn)
//     NG  RVA 0x0209B100 (size 2970b / 639 insn)
//     AE  RVA 0x021F0790 (size 2970b / 639 insn)
//     Confirmed via Render_PreUI anchor-walk; see
//     Fallout4RE/knowledge/cross-runtime/render-subsystem-manual-overrides.json
//   - Reads:
//       t? : kGbufferAlbedo     (RT 22)
//       t? : kDiffuseBuffer     (RT 58) -- diffuse light accumulation
//       t? : kSpecularBuffer    (RT 59) -- specular light accumulation
//       (potentially) emissive, fog
//   - Writes:
//       o0 : kMain              (RT 3) -- composited HDR scene
//
// Why we want this shader:
//   This is the canonical place to insert apply-passes that need diffuse and
//   specular accumulation buffers separated from direct light - exactly the
//   boundary SSGI Phase 2 wants to hook (instead of the current
//   post-DeferredLightsImpl modulation that darkens both ambient and direct).
//
// Reconstruction has not been performed: no DXBC blob is available in this
// repo. Populate per the runbook.
//
// Do not commit speculation.

#error \
"shaders/lighting/deferred_composite.hlsl is a reconstruction stub. " \
"Populate it from a RenderDoc capture or .ba2 extraction following " \
"Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md."
