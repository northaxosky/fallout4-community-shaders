// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction stub for the FO4 ambient + image-based-lighting deferred
// pixel shader. This is a reference reconstruction of Bethesda's compiled
// shader binary; see `shaders/lighting/README.md` and the runbook at
// `Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md`.
//
// Status: RENDERDOC-CONFIRMED (HLSL still WIP)
// See `shaders/lighting/shader-3560-analysis.md` for the structural
// reverse-engineering of this shader (Shaders011 blob 3560) including:
//   - Per-SRV-slot resource map confirmed by RenderDoc capture
//     FO4_frame5407.rdc eid 45345 (Phase C):
//       t1  kGbufferNormal      (R16G16_UNORM, octahedral)
//       t2  kGbufferMaterial    (R8G8B8A8_UNORM, gloss/spec/SSS/mat-id)
//       t3  gbuffer aux         (R8G8B8A8_UNORM, packed roughness + mat-id)
//       t4  kGbufferAlbedo      (R8G8B8A8_SRGB)
//       t5  ambient diffuse A   (R11G11B10_FLOAT, paired with t11)
//       t6  ambient diffuse aux (R11G11B10_FLOAT, paired with t12)
//       t7  kMain depth         (D24_UNORM_S8_UINT, DSV 2)
//       t8  IBL probe cubemap array (B8G8R8A8_SRGB, 252 slices, 8 mips)
//       t9  kSSAO               (R8G8B8A8_UNORM, AO in .x)
//       t10 SS ambient input    (R11G11B10_FLOAT, bilateral source)
//       t11 ambient diffuse B   (R11G11B10_FLOAT, paired with t5)
//       t12 SS ambient aux      (R11G11B10_FLOAT, paired with t6)
//       t14 kMainPreAlpha       (R8G8B8A8_SRGB, lit scene for IBL overlay)
//       t15 kMainDepthMips      (R32_FLOAT, 12-mip depth pyramid)
//   - Output: o0 -> kDiffuseBuffer (RT 58, R11G11B10_FLOAT).
//   - The SSGI-Phase-2-relevant AO-application boundary: line 264 of the
//     ASM applies kSSAO with a single multiply on the combined
//     ambient+IBL term, AFTER the cubemap and bilateral-blur math and
//     BEFORE fog blending. Direct light is computed by separate per-light
//     pixel shaders and is never multiplied by kSSAO via this path.
//   - kSSAO write timeline: written by DrawWorld::ImagespaceSAO at
//     Render_PreUI +0x036b, read by DrawWorld::DeferredLightsImpl at
//     Render_PreUI +0x039c. SSGI Phase 2 hook: RegisterPreDeferredLightsImpl
//     (already implemented in src/RenderHooks.cpp).
//
// Source binding:
//   - Dispatched inside DrawWorld::DeferredLightsImpl
//     REL::ID array { OG=1108521, NG=2318312, AE=2318312 }
//     (see Fallout4RE/exports/cs-lighting-shader-id-map.json)
//   - 14 SRVs, 3 CBs, 1 SV_Target output
//
// Reconstruction gap:
//   The full HLSL has not been committed because the 177-instruction
//   bilateral SSSS-style blur block (ASM lines 61-238) requires a
//   round-trip iteration loop the runbook caps at one pass with > 10%
//   diff -> WIP. The Phase B walk of DrawWorld::DeferredLightsImpl
//   confirmed that closing the SRV t4/t6/t10/t12/t14/t15 binding gap
//   and the CB12 [30..46] semantic gap requires either:
//     1. RenderDoc capture of the deferred-lighting pixel-shader stage
//        (canonical source — captured pipeline state lists each SRV
//        and CB12's contents at dispatch time), or
//     2. Per-shader manual disassembly of the BSShader subclass
//        owning this technique (see exports/cs-bsshader-vtables.json
//        for candidates).
//
//   Both gaps are blocked on the BSShader subclass virtual dispatch
//   issued by BSBatchRenderer::RenderPassImmediately, which the
//   static call graph in Fallout4RE/.cache/cross-runtime.sqlite does
//   not resolve.
//
//   The structural information in the analysis doc is sufficient for
//   SSGI Phase 2 to integrate at the correct boundary without the full
//   HLSL: rewrite the kSSAO buffer (RT 28) before this pass dispatches,
//   and the existing single-multiply at ASM line 264 carries the result
//   onto the ambient term only.

#error \
"shaders/lighting/ambient_ibl_pass.hlsl is a reconstruction stub. " \
"The structural analysis is complete (see shader-3560-analysis.md). " \
"Full HLSL reconstruction is WIP; see analysis doc 'Reconstruction gap' section."
