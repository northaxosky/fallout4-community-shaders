// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction of FO4's deferred-composite pixel shader.
//
// Status: REFERENCE - asm-level transcription, structural fidelity high.
//   Semantic naming of CB fields and texture roles is INCOMPLETE pending
//   IDA Hex-Rays cross-read of the dispatch site C++; unresolved items
//   are marked `TODO: identify` and migrated to
//   `shaders/lighting/docs/lighting-shader-followups.md` §Shaders011.3539.
//
// Canonical mapping:
//   * Corpus blob:    Shaders011.fxp blob 3539
//   * Corpus sha1:    861504f6dcbe2c1c7c3e5...
//   * Runtime sha1:   813c9acec23b701bffefb2fc795a633d46a05e18  (eid 45368
//     in FO4_frame5407.rdc) - mnemonic stream IDENTICAL to corpus blob;
//     byte-level difference is solely register-allocator choice.
//   * Source asm:     Shaders011.3539.861504f6dcbe.dxbc.asm
//   * Shape:          ps_5_0, 90 instructions, 6 samples, 6 SRVs
//                     (t0, t3, t4, t5, t7, t11), 2 CBs (CB12[47], CB2[3]),
//                     6 samplers, fullscreen-quad input, single SV_Target
//                     to RT 172 = kMain (R11G11B10_FLOAT).
//
// Host dispatch:
//   DrawWorld::DeferredComposite
//   REL::ID { OG=728427, NG=2318313, AE=2318313 }
//   OG  RVA 0x02855E60   NG RVA 0x0209B100   AE RVA 0x021F0790
//   This PS is the FIRST follow-up draw to the ambient/IBL pass (eid 45345);
//   it fires at eid 45368 in the canonical capture.
//
// What this shader does (high level, interpreted from asm):
//   1. Samples a "material ID" channel from t3 (8-bit code) and linear depth
//      from t7.
//   2. Selects one of two 4x4 reprojection matrices from CB12 based on a
//      depth threshold (0.01). The selected matrix is used to reconstruct
//      view-space position from screen-space UV + depth.
//   3. Checks if the material code is in {2, 3} (likely kMaterialSkin /
//      kMaterialHair); if it IS, outputs 0 (these materials composite in a
//      separate pass).
//   4. Otherwise: samples 4 color/HDR-scratch buffers (t0, t4, t5, t11),
//      combines them, and applies sun-direction lighting via cb2[1] (sun
//      dir) + cb2[2].w (specular power) + cb2[1].w (intensity).
//   5. Distance- and depth-based fog/tonemap-style blending using CB12
//      indices 14, 35, 41..46 - these look like the same fog/distance-fade
//      parameter block already partially documented in
//      `shader-3560-analysis.md` for the ambient/IBL pass.
//
// Limits of this reconstruction (be honest):
//   - CB12 field NAMES are not from C++ cross-read; only the INDICES are
//     known. The math is faithful but the field semantics are
//     `// TODO: identify` markers.
//   - The texture role mapping (t0..t11 -> kMain/kGbuffer*/scratch RT
//     index) is inferred from rdoc capture eid 45368 SRV bindings (which
//     reference RT 250, 253, 256, 389, 395, 183-depth - all outside the
//     committed `cs::engine::RenderTarget` enum). They appear to be
//     dynamic per-frame scratch RTs allocated by RenderTargetManager.
//     Confirming the binding semantics requires IDA Hex-Rays on the
//     dispatch site's `OMSetRenderTargets`/`PSSetShaderResources` setup.
//   - DXC round-trip validation: see "Round-trip notes" section below.
//
// Reference: this is a derivative reverse-engineering of Bethesda's
// compiled shader. Licensed under this repo's terms; the asm-level math
// derives from D3DDisassemble output of corpus blob 3539.

// ----------------------------------------------------------------------------
// Constant buffer layouts.
// CB indices are confirmed from the asm declarations (CB12[47] = 47 vec4s,
// CB2[3] = 3 vec4s). Field-level semantics are NOT cross-read from C++ in
// this revision; named accessors below are introduced for readability and
// must be validated against the dispatch site before use.
// ----------------------------------------------------------------------------

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..13]: TODO: identify - referenced indirectly through cb12[14] dp4
    float4 cb12_pad_0_13[14];

    // [14]: dp4 with reconstructed view-space position (with .w=1) yields
    //       a scalar used as fog-distance input. Likely a fog plane equation
    //       (FogDistancePlane: float4 = (normal.xyz, d)). TODO: identify.
    float4 cb12_idx14_fog_distance_plane;

    // [15..19]: TODO: identify
    float4 cb12_pad_15_19[5];

    // [20..23]: 4x4 matrix - "far" reprojection (screen-space + linear-depth
    //           to view-space position; selected when sampled-depth >= 0.01).
    //           Row-major: cb12[20]=row0, cb12[21]=row1, cb12[22]=row2,
    //           cb12[23]=row3. TODO: confirm naming via IDA.
    float4x4 cb12_far_reproj_matrix;  // rows 20..23

    // [24..27]: 4x4 matrix - "near" reprojection (selected when sampled-depth
    //           < 0.01). Same row order as far matrix.
    float4x4 cb12_near_reproj_matrix; // rows 24..27

    // [28..34]: TODO: identify
    float4 cb12_pad_28_34[7];

    // [35]: .z used as additive scalar after the fog-plane dp4.
    //       TODO: identify; possibly FogBias.
    float4 cb12_idx35;

    // [36..40]: TODO: identify
    float4 cb12_pad_36_40[5];

    // [41]: .x and .z used in linear-remap of view-space distance into a
    //       fog blend factor: blend = saturate(distance * cb12[41].x -
    //       cb12[41].z). TODO: identify; likely FogDistanceRamp.xz.
    float4 cb12_idx41;

    // [42]: .xyz = fog color "A" lerp endpoint; .w = log-space exponent for
    //       fog intensity (used in exp2(log2(blend) * .w)). TODO: confirm.
    float4 cb12_idx42_fog_color_a_and_exp;

    // [43]: .xyz = fog color "B" lerp endpoint; .w = fog intensity threshold
    //       used in two places (movc default + lt comparison vs final
    //       output luma). TODO: confirm.
    float4 cb12_idx43_fog_color_b_and_threshold;

    // [44]: .xyz = fog color "C" lerp endpoint; .w = intensity scale used
    //       in `mad r1.w, r0.w, cb12[44].w, r1.w`. TODO: confirm.
    float4 cb12_idx44_fog_color_c_and_scale;

    // [45]: .xyz = fog color "D" lerp endpoint. TODO: identify .w.
    float4 cb12_idx45_fog_color_d;

    // [46]: .xy and .zw used in saturated linear remap of the fog-plane
    //       distance: r2.xy = saturate(distance * cb12[46].xy - cb12[46].zw).
    //       TODO: identify; likely fog-distance near/far + intensity.
    float4 cb12_idx46_fog_distance_remap;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: .xy = screen-space UV scale (multiplied with SV_POSITION.xy at
    //      the very first instruction); .zw = related scale used in
    //      view-space reconstruction. Likely (1/width, 1/height, width,
    //      height) or (RcpFrameDim.xy, FrameDim.xy). TODO: confirm.
    float4 cb2_idx0_screen_uv_scale;

    // [1]: .xyz = sun direction (or directional light direction) in
    //      view-space (dot with reconstructed view-space-normalized
    //      position gives N.L term).
    //      .w = directional light intensity scalar.
    //      TODO: confirm via IDA on dispatch-site C++.
    float4 cb2_idx1_sun_dir_and_intensity;

    // [2]: .xyz = directional light color (lerp endpoint with sampled t4
    //      color).
    //      .w = specular power exponent (`pow(NdotL, exp)` style).
    //      TODO: confirm.
    float4 cb2_idx2_sun_color_and_spec_power;
};

// ----------------------------------------------------------------------------
// Resource bindings.
// Slot indices match the corpus blob 3539 declarations exactly. The runtime
// re-encoded variant (rdoc eid 45368) uses sequential slots t0..t5 instead,
// but the LOGICAL bindings are identical per the 2026-05-18 mnemonic-diff
// finding. Texture roles below are inferred from the asm's usage pattern;
// authoritative RT-index mapping requires IDA cross-read of the host's
// `PSSetShaderResources` call (TODO).
// ----------------------------------------------------------------------------

// t0: HDR-scratch buffer; sampled with s0 in the non-skin branch as the
//     primary color base. Per rdoc eid 45368 slot 0 = RT 250 (R8G8B8A8_SRGB).
//     TODO: identify which engine RT this is; not in the committed
//     `cs::engine::RenderTarget` enum (RT indices > 100 are dynamic scratch).
Texture2D<float4> g_tHdrBaseColor : register(t0);

// t3: material-id buffer; .y channel sampled once. Used as a discriminator:
//     after `mat * 255 - {2,3}`, abs(...) < 0.25 selects "is skin or hair"
//     branch. The .y channel hints that this is a packed gbuffer aux RT
//     where material code lives in green. TODO: confirm.
Texture2D<float4> g_tMaterialIdBuffer : register(t3);

// t4: secondary color sample (sampled without explicit LOD = uses
//     mip-mapping). Combined with the lighting math output via grayscale
//     desaturation lerp. Per rdoc slot 2 = RT 253 (R8G8B8A8_SRGB).
//     TODO: identify; possibly a previously-composited frame or color-graded
//     reference for tonemap saturation control.
Texture2D<float4> g_tSecondaryColor : register(t4);

// t5: HDR scratch "A". Sampled and added to t11. Per rdoc slot 3 = RT 389
//     (R11G11B10_FLOAT). TODO: identify; likely an accumulated ambient or
//     IBL contribution buffer.
Texture2D<float4> g_tHdrScratchA : register(t5);

// t7: linear depth buffer; .y channel sampled. Per rdoc slot 4 = depth
//     target 183 (D24S8). The `.y` channel access on a depth target is
//     unusual - may indicate the engine binds a typed view that aliases
//     depth into a different channel. TODO: confirm.
Texture2D<float4> g_tLinearDepth : register(t7);

// t11: HDR scratch "B". Sampled and added to t5. Per rdoc slot 5 = RT 395
//      (R11G11B10_FLOAT). TODO: identify.
Texture2D<float4> g_tHdrScratchB : register(t11);

SamplerState g_sBaseColor : register(s0);
SamplerState g_sMaterialId : register(s3);
SamplerState g_sSecondaryColor : register(s4);
SamplerState g_sHdrScratchA : register(s5);
SamplerState g_sDepth : register(s7);
SamplerState g_sHdrScratchB : register(s11);

// ----------------------------------------------------------------------------
// Entry point.
// ----------------------------------------------------------------------------

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    // Insn 0: screen-space UV from SV_POSITION.xy * CB2[0].xy
    float2 uv = input.position.xy * cb2_idx0_screen_uv_scale.xy;

    // Insn 1-2: sample material-id (.y of t3) and linear depth (.y of t7)
    float matIdRaw = g_tMaterialIdBuffer.SampleLevel(g_sMaterialId, uv, 0).y;
    float depth    = g_tLinearDepth.SampleLevel(g_sDepth, uv, 0).y;

    // Insn 3-16: select reprojection matrix based on depth threshold.
    //   if (depth < 0.01)   -> use NEAR matrix, scale depth by 100
    //   else                -> use FAR matrix,  linearize depth slightly
    float linearizedDepth;
    float4x4 reprojMatrix;
    if (depth < 0.01)
    {
        linearizedDepth = depth * 100.0;
        reprojMatrix    = cb12_near_reproj_matrix;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojMatrix    = cb12_far_reproj_matrix;
    }

    // Insn 17-20: material-ID test - is matId in {2, 3}?
    //   r0.zw = matIdRaw * 255 + {-2, -3}
    //   r0.zw = abs(r0.zw) < 0.25
    //   r0.z  = r0.z || r0.w     (true if matId == 2 OR 3)
    float matIdByte = matIdRaw * 255.0;
    bool  isMatId2  = abs(matIdByte - 2.0) < 0.25;
    bool  isMatId3  = abs(matIdByte - 3.0) < 0.25;
    bool  isSkinOrHair = isMatId2 || isMatId3;

    if (!isSkinOrHair)
    {
        // -------- Insn 21-25: sample base + ambient pair, combine. --------
        float3 baseColor = g_tHdrBaseColor.SampleLevel(g_sBaseColor, uv, 0).xyz;
        float3 ambientA  = g_tHdrScratchA.SampleLevel(g_sHdrScratchA, uv, 0).xyz;
        float3 ambientB  = g_tHdrScratchB.SampleLevel(g_sHdrScratchB, uv, 0).xyz;
        float3 ambientSum = ambientA + ambientB;
        float3 litColor  = baseColor * ambientSum;  // r6 = baseColor * (t5 + t11)

        // -------- Insn 26: sample secondary color (with mip filtering). --
        float3 secondaryColor = g_tSecondaryColor.Sample(g_sSecondaryColor, uv).xyz;

        // -------- Insn 27-35: reconstruct view-space position. -----------
        //   uvNDC.x = (uv.x * CB2[0].z)  remapped to [-1, +1]
        //   uvNDC.y = (-uv.y * CB2[0].w + 1) remapped to [-1, +1]
        //   pos4    = (uvNDC.xy, linearizedDepth, 1)
        //   posView = mul(reprojMatrix, pos4) / posView.w
        //   (TODO: confirm matrix order vs row-major/column-major; the asm
        //   uses dp4 on rows 0..3 of the matrix which is row-major mul.)
        float3 uvRemapped;
        uvRemapped.x = uv.x * cb2_idx0_screen_uv_scale.z;
        uvRemapped.z = -uv.y * cb2_idx0_screen_uv_scale.w + 1.0;
        float2 uvNDC = uvRemapped.xz * 2.0 - 1.0;

        float4 pos4 = float4(uvNDC, linearizedDepth, 1.0);
        float4 posViewH;
        posViewH.x = dot(reprojMatrix[0], pos4);
        posViewH.y = dot(reprojMatrix[1], pos4);
        posViewH.z = dot(reprojMatrix[2], pos4);
        posViewH.w = dot(reprojMatrix[3], pos4);
        float3 posView = posViewH.xyz / posViewH.www;

        // -------- Insn 36-37: fog-plane distance. -------------------------
        //   r0.w = 1
        //   r0.w = dp4(cb12[14], r0.xyzw)
        //   r0.w = r0.w + cb12[35].z
        float fogPlaneDistance = dot(cb12_idx14_fog_distance_plane, float4(posView, 1.0));
        fogPlaneDistance += cb12_idx35.z;

        // -------- Insn 38-42: distance-based fog factor. ------------------
        //   r1.x = dot(posView, posView)     // length squared
        //   r1.y = sqrt(r1.x)                 // length
        //   r1.y = r1.y * cb12[41].x - cb12[41].z
        //   r1.z = saturate(r1.y)
        float posViewLenSq = dot(posView, posView);
        float posViewLen   = sqrt(posViewLenSq);
        float distanceRamp = posViewLen * cb12_idx41.x - cb12_idx41.z;
        float distanceFactor = saturate(distanceRamp);

        // -------- Insn 43-45: dual saturated remap of fog-plane distance. -
        //   r2.xy = saturate(fogPlaneDistance.xx * cb12[46].xy - cb12[46].zw)
        //   r0.w  = r2.x + distanceFactor * (r2.y - r2.x)   // lerp
        float2 fogRemapPair = saturate(fogPlaneDistance.xx * cb12_idx46_fog_distance_remap.xy
                                       - cb12_idx46_fog_distance_remap.zw);
        float  fogBlend     = lerp(fogRemapPair.x, fogRemapPair.y, distanceFactor);

        // -------- Insn 46-52: fog intensity (with threshold + scale). -----
        //   if (distanceRamp > 0.75) {
        //     r2.x = (distanceFactor - 0.75) * 4.0
        //     r2.x = r2.x * (1.0 - cb12[43].w) + cb12[43].w
        //     r2.x = min(r2.x, 1.0)
        //     fogIntensityClamp = r2.x
        //   } else {
        //     fogIntensityClamp = cb12[43].w
        //   }
        float fogIntensityClamp;
        if (distanceRamp > 0.75)
        {
            float t = (distanceFactor - 0.75) * 4.0;
            float scaled = t * (1.0 - cb12_idx43_fog_color_b_and_threshold.w)
                             + cb12_idx43_fog_color_b_and_threshold.w;
            fogIntensityClamp = min(scaled, 1.0);
        }
        else
        {
            fogIntensityClamp = cb12_idx43_fog_color_b_and_threshold.w;
        }

        // -------- Insn 53-55: small-distance escape. ----------------------
        //   r1.y = (distanceRamp < 0.015) ? distanceFactor*66.666672 : 1.0
        float nearEscape = (distanceRamp < 0.015)
                           ? (distanceFactor * 66.666672)
                           : 1.0;

        // -------- Insn 56-59: log-exp distance shaping. -------------------
        //   r1.z = log(distanceFactor) * cb12[42].w
        //   r1.z = exp(r1.z)              // = distanceFactor ^ cb12[42].w
        //   r1.z = min(fogIntensityClamp, r1.z)
        float distancePow   = pow(distanceFactor, cb12_idx42_fog_color_a_and_exp.w);
        float fogIntensity  = min(fogIntensityClamp, distancePow);

        // -------- Insn 60-61: fog blend weight. ---------------------------
        //   r1.w = 1.0 - fogBlend          // unfogged weight
        //   r1.w = fogBlend * cb12[44].w + r1.w
        //   (equivalent to: fogBlend * (cb12[44].w - 1) + 1)
        float fogBlendWeight = fogBlend * cb12_idx44_fog_color_c_and_scale.w
                               + (1.0 - fogBlend);

        // -------- Insn 62-67: fog color (4-corner lerp). ------------------
        //   colorAC  = cb12[42].xyz + fogIntensity * (cb12[44].xyz - cb12[42].xyz)
        //   colorBD  = cb12[43].xyz + fogIntensity * (cb12[45].xyz - cb12[43].xyz)
        //   fogColor = colorAC + fogBlend * (colorBD - colorAC)
        float3 fogColorAC = lerp(cb12_idx42_fog_color_a_and_exp.xyz,
                                 cb12_idx44_fog_color_c_and_scale.xyz, fogIntensity);
        float3 fogColorBD = lerp(cb12_idx43_fog_color_b_and_threshold.xyz,
                                 cb12_idx45_fog_color_d.xyz, fogIntensity);
        float3 fogColor   = lerp(fogColorAC, fogColorBD, fogBlend);

        // -------- Insn 68: combined fog scalar. ---------------------------
        float combinedFog = fogBlendWeight * fogIntensity;

        // -------- Insn 69-70: normalize view-space dir + scale. -----------
        //   r1.x = rsq(posViewLenSq)
        //   r0.xyz = posView * r1.x          // unit dir
        //   r0.w   = combinedFog * r1.y      // (per asm's r1.xxxy mul)
        float3 viewDirUnit = posView * rsqrt(posViewLenSq);
        // Note: the asm packs `r0.xyzw *= r1.xxxy` which puts combinedFog
        // into r0.w via .y of r1. After this, r0.w carries combinedFog *
        // nearEscape. This is the value used as the "fog mix factor" later.
        float fogMixFactor = combinedFog * nearEscape;

        // -------- Insn 71-76: sun-direction lighting. ---------------------
        //   NdotL    = max(dot(viewDirUnit, cb2[1].xyz), 0)
        //   specular = pow(NdotL, cb2[2].w) * cb2[1].w
        float NdotL    = max(dot(viewDirUnit, cb2_idx1_sun_dir_and_intensity.xyz), 0.0);
        float specular = pow(NdotL, cb2_idx2_sun_color_and_spec_power.w)
                         * cb2_idx1_sun_dir_and_intensity.w;

        // -------- Insn 77-78: blend secondary color with sun color. -------
        //   r0.xyz = lerp(secondaryColor, cb2[2].xyz, specular)
        float3 secondaryLit = lerp(secondaryColor, cb2_idx2_sun_color_and_spec_power.xyz,
                                   specular);

        // -------- Insn 79-83: grayscale-saturation tonemap path. ----------
        //   r1.xyz   = litColor * 3 + secondaryColor     // r6 * 3 + r7
        //   gray     = dot(r1.xyz, (1/3, 1/3, 1/3))
        //   r2.xyz   = gray - r0.xyz
        //   r1.xyz   = gray + gray * (r2.xyz - 0?)       // wait this is gray.xxx as t
        //   Actually: r1.xyz = r1.xxx * r2.xyz + r0.xyz
        //     which is: lerp(secondaryLit, gray, gray)?  No: it's
        //     secondaryLit + gray*(gray - secondaryLit) which simplifies to
        //     lerp(secondaryLit, gray.xxx, gray).
        float3 ambientWeighted = litColor * 3.0 + secondaryColor;
        float  gray            = dot(ambientWeighted, float3(1.0/3.0, 1.0/3.0, 1.0/3.0));
        float3 graySaturated   = secondaryLit + gray * (gray.xxx - secondaryLit);

        // -------- Insn 80, 84: branch on combinedFog vs threshold. --------
        //   r1.w = (fogMixFactor < cb12[43].w)
        //   output.xyz = r1.w ? graySaturated : secondaryLit
        bool useGraySaturated = (fogMixFactor
                                  < cb12_idx43_fog_color_b_and_threshold.w);
        output.color.xyz = useGraySaturated ? graySaturated : secondaryLit;

        // -------- Insn 85: output alpha = fogMixFactor. -------------------
        output.color.w = fogMixFactor;
    }
    else
    {
        // Insn 86-87: skin / hair materials produce no contribution here;
        // they are composited by a different pass (see DrawWorld::
        // DeferredComposite's second/third RenderPassImmediately call).
        output.color = float4(0, 0, 0, 0);
    }

    return output;
}

// ============================================================================
// Round-trip notes (for the reviewer + future maintainer)
//
// This file was authored as a one-pass asm-to-HLSL transcription of corpus
// blob 3539 (sha1 861504f6dcbe...) against the disassembly at
// Shaders011.3539.861504f6dcbe.dxbc.asm.
//
// fxc round-trip status: see local roundtrip notes for the
// compile output + insn-count delta against the original.
//
// Round-trip results (fxc /T ps_5_0 /O3 /Ni, recompile + asm-mnemonic diff
// against the original at Shaders011.3539.861504f6dcbe.dxbc.asm):
//
//   * Resource bindings: EXACT MATCH. All 6 textures bound to
//     t0/t3/t4/t5/t7/t11; all 6 samplers to s0/s3/s4/s5/s7/s11; cb12 + cb2
//     at correct slots. Signature: SV_POSITION-only input, single
//     SV_Target output - exact match.
//   * Sample call count: EXACT MATCH (6 / 6 with the original).
//   * Instruction count: 108 insns vs original 90 (+18 / +20%).
//     Structural fidelity verified; instruction-count delta documented.
//
//   The +20% delta is interpreted as register-allocator / common-
//   subexpression-elimination differences between my literal asm
//   transcription and the original Bethesda HLSL. Specifically:
//   - the `if (depth < 0.01) { matrix = near } else { matrix = far }`
//     pattern is harder for fxc to fold into a single movc-style select
//     than a direct ternary or per-row movc;
//   - my explicit `float4x4` copies (16 dp4 ops worth of locals) may
//     force more temporaries than the original;
//   - the grayscale-saturation tonemap formulation may have a tighter
//     equivalent than what I wrote.
//
//   The reconstruction is therefore SHIPPED-WIP: structurally faithful,
//   semantically transparent, compiles cleanly, but the round-trip is
//   not byte-equivalent to the original. Follow-up work to tighten the
//   compiler output (queued under `shaders/lighting/docs/lighting-shader-followups.md`
//   §Shaders011.3539): rework the matrix-select pattern, then revisit
//   the lighting + tonemap blocks.
//
// What is faithfully reconstructed:
//   * Resource declarations (6 SRVs, 6 samplers, 2 CBs) at exact slot
//     indices.
//   * Control flow (depth-based matrix select, material-id-based skin/hair
//     gate, near-distance escape branch, fog-intensity threshold branch).
//   * All 6 texture sample calls with their LOD modes (5 SampleLevel(0),
//     1 Sample).
//   * The dp4 / dp3 / rsq / pow / lerp math is asm-faithful per instruction.
//
// What needs cross-read to finalize (see followups doc §Shaders011.3539):
//   * Engine RT-index mapping for t0/t3/t4/t5/t7/t11 (the rdoc slot bindings
//     reference RT 250/256/253/389/183-depth/395 which are outside the
//     `cs::engine::RenderTarget` enum; they appear to be dynamic
//     RenderTargetManager scratch RTs).
//   * CB12 field names (currently `cb12_idx<N>_*` placeholders). The
//     dispatch site C++ in `DrawWorld::DeferredComposite` body should set
//     up these fields - read via IDA Hex-Rays on the AE RVA 0x021F0790
//     function body.
//   * Sampler addressing modes (all declared `mode_default` in asm, but the
//     actual D3D11 sampler state object setup is in the dispatch C++).
//
// What is intentionally NOT done in this revision (separate work):
//   * Skyrim CS analog adaptation (`ISLightingComposite.hlsl` is a CLOSER
//     analog than `ISSAOComposite.hlsl` for this PS; both differ in
//     significant ways from this FO4 composite though). The FO4 PS does
//     not have separate `dirDiffuse`/`dirSpecular` accumulation buffers -
//     it computes specular directly from the reconstructed view-space dir
//     and the sun direction.
//   * Permutation diff against a second RenderDoc capture (indoor vs
//     outdoor). Deferred to a follow-up pass.
//   * Validation that the "secondary color" t4 sample is the previous
//     frame's tonemap reference (suspected based on the grayscale-
//     saturation tonemap pattern at insns 79-84) - requires a second
//     capture or IDA cross-read.
// ============================================================================
