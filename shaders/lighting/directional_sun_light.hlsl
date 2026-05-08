// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction stub for the FO4 directional (sun) light deferred pixel
// shader. This is a reference reconstruction of Bethesda's compiled shader
// binary; see `shaders/lighting/README.md` and the runbook at
// `Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md`.
//
// Status: NEEDS-EXTRACTION
//
// Source binding:
//   - Dispatched inside DrawWorld::DeferredLightsImpl
//     REL::ID array { OG=1108521, NG=2318312, AE=2318312 }
//     (the sun-light dispatch is one of multiple draws inside this function)
//   - Reads:
//       t? : kGbufferNormal     (RT 20)
//       t? : kGbufferAlbedo     (RT 22)
//       t? : kGbufferMaterial   (RT 24)
//       t? : main depth         (DSV 2)
//       t? : sun shadow map     (cascade or single shadow map)
//   - Writes:
//       o0 : kDiffuseBuffer     (RT 58)
//       o1 : kSpecularBuffer    (RT 59)
//
// Why we want this shader:
//   Per-light deferred pass code is the BRDF reference for any future
//   shader-modifying feature (TruePBR, custom material BRDFs, energy-
//   conserving SSS replacement). It also confirms whether the engine
//   uses Lambert + Schlick GGX, Disney, or a custom BRDF.
//
// Reconstruction has not been performed: no DXBC blob is available in this
// repo. Populate per the runbook.
//
// Do not commit speculation.

#error \
"shaders/lighting/directional_sun_light.hlsl is a reconstruction stub. " \
"Populate it from a RenderDoc capture or .ba2 extraction following " \
"Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md."
