// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction stub for the FO4 ambient + image-based-lighting deferred
// pixel shader. This is a reference reconstruction of Bethesda's compiled
// shader binary; see `shaders/lighting/README.md` and the runbook at
// `Fallout4RE/Workspace/docs/lighting-shader-reconstruction-runbook.md`.
//
// Status: ANALYSIS-COMPLETE / HLSL-WIP
// See `shaders/lighting/shader-3560-analysis.md` for the structural
// reverse-engineering of this shader (Shaders011 blob 3560) including:
//   - Per-SRV-slot resource map (high-confidence: t1 kGbufferNormal,
//     t7 depth, t8 IBL cubemap array, t9 kSSAO).
//   - Per-CB-index constant map (camera matrices, fog params, light dir).
//   - Five-block structural breakdown of the 321-instruction shader.
//   - The SSGI-Phase-2-relevant AO-application boundary: line 264 of the
//     ASM applies kSSAO with a single multiply on the combined
//     ambient+IBL term, AFTER the cubemap and bilateral-blur math and
//     BEFORE fog blending. Direct light is computed by separate per-light
//     pixel shaders and is never multiplied by kSSAO via this path.
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
//   diff -> WIP. Closing the gap requires:
//     1. Walking DrawWorld::DeferredLightsImpl at AE 0x021ed4c0 to
//        recover t4 / t6 / t10 / t12 / t14 / t15 binding semantics from
//        the SetShaderResources call.
//     2. Recovering the cbPerFrameDeferred struct layout (CB12 indices
//        30 / 35 / 41-46 are fog/distance constants whose specifics need
//        the C++ side).
//     3. Iterating HLSL -> dxc -> disassemble -> diff on the bilateral
//        block until per-channel kernel weights match.
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
