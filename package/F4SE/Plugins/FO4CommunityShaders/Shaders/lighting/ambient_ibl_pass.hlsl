// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction of FO4 corpus blob Shaders011.fxp #3559.
//
// Status: REFERENCE - asm-level transcription, structural fidelity high.
//   This is the FO4 ambient/IBL deferred pixel shader. The 9-tap bilateral
//   SSSS-style blur block (insns 80-251 in the original asm, 171 of the
//   263 total instructions) is the largest single segment and is
//   reconstructed as a [unroll]'d loop over a static kernel array.
//   CB field semantic names are partially inferred from prior asm
//   reading; unresolved fields are marked `// TODO: identify`.
//
// Canonical mapping:
//   * Corpus blob:    Shaders011.fxp blob 3559
//   * Corpus sha1:    7460585eaf76...
//   * Runtime sha1:   761d41008016... (eid 45345 in FO4_frame5407.rdc) -
//     mnemonic stream within +/- 2 instructions of corpus blob
//     (263 vs 265 insns; 44 vs 44 samples; same control flow).
//   * Sibling blob:   3560 (sha 2b6e36c08aca, 321 insns) - structurally
//     equivalent. Use 3559 as canonical (matches the captured runtime
//     PS exactly).
//   * Source asm:     Shaders011.3559.7460585eaf76.dxbc.asm
//   * Shape:          ps_5_0, 263 insns, 44 samples, 14 SRVs
//                     (t1-t7, t8 texturecubearray, t9-t12, t14, t15),
//                     14 samplers, 3 CBs (CB12[31], CB0[3], CB2[6]),
//                     fullscreen-quad input, single SV_Target to
//                     RT 58 = kDiffuseBuffer (R11G11B10_FLOAT).
//
// Host dispatch:
//   inside DrawWorld::DeferredLightsImpl
//   REL::ID { OG=1108521, NG=2318312, AE=2318312 }
//   AE RVA 0x021ed4c0   OG RVA 0x028529b0   NG RVA 0x02097e30
//
// SSGI integration boundary (high confidence):
//   The engine applies kSSAO (t9) to the combined ambient+IBL term via
//   a SINGLE MULTIPLY at the very end of this shader (insns 261-262).
//   AO is applied AFTER the cubemap reflection, AFTER the bilateral
//   SSSS blur, AFTER all ambient accumulation - and BEFORE any fog
//   blending (fog happens in the downstream composite, blob 3539).
//   Direct light is NEVER multiplied by AO via this path; per-light
//   PSes that also live in DeferredLightsImpl write to kDiffuseBuffer
//   additively after this pass.
//
// SSGI integration recommendation: replace the AO
// source itself (write SSGI-modulated value into kSSAO = RT 28 BEFORE
// this PS dispatches). RegisterPostDeferredPrePass in src/RenderHooks.cpp
// is positioned to support this.
//
// What this shader does (interpreted, structurally validated):
//   1. Sample gbuffer aux buffers (t3 shading data, t5+t11 precomputed
//      ambient pair, t7 depth, t10 bilateral source).
//   2. Compute glossiness factor from t3.x (roughness derivation).
//   3. Depth-based matrix select via CB12[20..27] - SHARED with the
//      deferred composite (blob 3539); rows 20..23 far, 24..27 near.
//   4. IF the material has IBL contribution (t2.y > 0.001961):
//      - Sample t1 = kGbufferNormal as octahedral-encoded normal.
//      - Reconstruct view-space position via the selected matrix.
//      - Build reflection vector; transform to world space via
//        CB12[12..14] (3x3 rotation matrix).
//      - Sample t8 IBL cubemap array using reflection dir + array
//        slice from t2.x + roughness mip.
//      - Apply luminance desaturation via CB12[30].y * 0.9 weight.
//   5. Sample t14 (kMainPreAlpha lit-scene reference), blend with the
//      IBL color using CB0[2].z alpha modulator and CB0[1].x weight.
//   6. IF the material is skin (t3.z * 255 == 5, within 0.25):
//      - 9-tap bilateral SSSS blur over t10 (color) weighted by t15
//        (depth) similarity. Per-tap RGB weights match the
//        Christensen-Burley SSSS approximation kernel (high red /
//        medium green / low blue absorption).
//      - Sample t6 + t12 + add the t4 contribution from the IBL block,
//        sum into the accumulated ambient/IBL output.
//   7. Modulate accumulated value by glossiness factor (r0.z) and
//      t2.y-squared-and-scaled (r1.w).
//   8. Multiply by t9 (kSSAO) - THE single AO application boundary.
//   9. Output to o0; o0.w = 1.0.
//
// Limits of this reconstruction (be honest):
//   * CB12 field names are PARTIALLY known
//     (CB12[12..14] view->world matrix, CB12[20..27] reprojection
//     matrices, CB12[30].y luminance lerp). Other CB12 indices used
//     in the dispatch (e.g. CB12[12..14] for world-space rotate) and
//     CB0/CB2 details are TODO.
//   * The 9-tap blur kernel weights are the actual asm-extracted RGB
//     weights; they match Christensen-Burley SSSS but the per-tap
//     offsets (in CB0[0].xy * scale units) require dispatch-site
//     cross-read for full semantic naming (the offsets divide by
//     a depth-derived factor at insn 86).
//   * No second-pass capture diff - the 9-tap blur is a separable
//     horizontal pass; a vertical complement should exist somewhere
//     (likely blob 3559+1 or 3559-1 in the fxp). Not investigated
//     here.

// ----------------------------------------------------------------------------
// Constant buffer layouts.
// ----------------------------------------------------------------------------

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..11]: not read directly by this PS. Per runtime evidence
    // (cb12-runtime-evidence.json, captured FO4_frame5407.rdc):
    //   [0..2]  ViewRotation rows (orthonormal 3x3, world -> view)
    //   [3]     ViewMatrix_row3 (homogeneous identity (0,0,0,1))
    //   [4..7]  Projection rows (focal_x=1.19, focal_y=2.12, near=15);
    //           [5].z is TAA-patched mid-frame
    //   [8..10] PrevFrame_ViewProj (for motion vectors); [9] TAA-patched
    //   [11]    duplicate of [2]
    float4 cb12_pad_0_11[12];

    // [12..14]: 3x3 view-to-world rotation matrix rows (transpose of
    //           the view-rotation block at [0..2]; transpose-pair
    //           confirmed via runtime evidence). Used at insns 55-57 to
    //           transform the reflection vector from view space into
    //           world space for IBL cubemap sampling.
    float4 ViewToWorld_row0;  // = cb12[12]
    float4 ViewToWorld_row1;  // = cb12[13]
    float4 ViewToWorld_row2;  // = cb12[14]

    // [15..19]: not read directly by this PS. Per runtime evidence:
    //   [15]    ViewToWorld_row3 (homogeneous identity continuation)
    //   [16..18] WorldToView extended block (camera_pos partial in .w);
    //            [18].w is TAA-patched
    //   [19]    Depth reciprocal params (near recip + depth-range recip)
    float4 cb12_pad_15_19[5];

    // [20..23]: "Far" reprojection matrix (selected when sampled depth
    //           >= 0.01; reconstructs view-space position from screen-
    //           space UV + linearized depth). 0.84 / 0.47 diagonal.
    //           Shared with composite (3539), sun-light (3295), VLS
    //           slice (2147) - same per-frame infrastructure.
    //           [21].w is TAA-patched mid-frame (sub-pixel jitter).
    float4 FarReproj_row0;
    float4 FarReproj_row1;
    float4 FarReproj_row2;
    float4 FarReproj_row3;

    // [24..27]: "Near" reprojection matrix (selected when depth < 0.01).
    //           Same diagonal as Far, with TAA sub-pixel camera offsets
    //           embedded in [24].w and [25].w.
    float4 NearReproj_row0;
    float4 NearReproj_row1;
    float4 NearReproj_row2;
    float4 NearReproj_row3;

    // [28..29]: not read directly by this PS. Per runtime evidence:
    //   [28]    Clip_planes_and_fog (near=0.02, far=125, density=1.2, far=160)
    //   [29]    Exposure_or_tonemap (0.36, -0.4, 0, 0)
    float4 cb12_pad_28_29[2];

    // [30]: .y = IBL luminance-desaturation lerp factor (scaled by 0.9
    //       at insn 65). Runtime evidence at [30] reads zeros in the
    //       captured frame (inconclusive - value may be scene-dependent).
    //       Other channels TODO.
    float4 cb12_idx30_ibl_desaturation;
};

cbuffer PerCall_CB0 : register(b0)
{
    // [0]: .xy = screen-space UV scale (likely RcpFrameDim);
    //      .y reused at insns 94+ as bilateral-blur depth tolerance
    //      scale (* 0.1);
    //      .z = scale for the bilateral-blur kernel size adaptation
    //      at insn 82.
    //      TODO: confirm via IDA on dispatch site.
    float4 cb0_idx0_screen_scale_and_blur_tolerance;

    // [1]: .x = lit-scene weight in the IBL/scene blend at insn 75.
    //      TODO: confirm.
    float4 cb0_idx1_lit_scene_weight;

    // [2]: .z = lit-scene alpha multiplier in the IBL/scene blend at
    //      insn 73. Other channels TODO.
    float4 cb0_idx2_lit_scene_alpha;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: per runtime evidence (cb12-runtime-evidence.json sibling at
    //      eid 45345 CB2 slot): .xy = RcpFrameDim (1/3840, 1/2160 in the
    //      captured frame), .zw = FrameDim (3840, 2160). Same shape
    //      across composite, sun-light, VLS slice - shared screen-size
    //      conventions for the per-call CB.
    float4 ScreenSize;

    // [1..4]: TODO: identify
    float4 cb2_pad_1_4[4];

    // [5]: .xy = clamp threshold for the t14 lit-scene UV sample at
    //      insn 71. Likely a screen-edge clamp. TODO: identify.
    float4 cb2_idx5_lit_scene_uv_clamp;
};

// ----------------------------------------------------------------------------
// Resource bindings. Semantic roles inferred from asm reading.
// ----------------------------------------------------------------------------

// t1: kGbufferNormal (RT 20). Octahedral-encoded normal sampled at insn 33;
//     decoded at insns 34-39.
Texture2D<float4> g_tGbufferNormal : register(t1);

// t2: kGbufferMaterial (RT 24). .x = material code / IBL array slice; .y
//     used as IBL-contribution gate and gloss factor.
Texture2D<float4> g_tGbufferMaterial : register(t2);

// t3: gbuffer "shading-data" packed buffer. .x = roughness;
//     .y = (also a shading param, see asm); .w / .z = material id check.
Texture2D<float4> g_tGbufferShadingData : register(t3);

// t4: screen-space buffer; sampled inside material-5 (skin) block as RGB.
//     TODO: identify; possibly precomputed transmission or AO contribution.
Texture2D<float4> g_tSkinAuxColor : register(t4);

// t5: precomputed ambient diffuse "A". Sampled at insn 2; combined with
//     t11 (`t5 + t11`) and scaled `*3`.
Texture2D<float4> g_tAmbientDiffuseA : register(t5);

// t6: ambient/probe contribution; sampled at center within skin block.
//     TODO: identify.
Texture2D<float4> g_tAmbientProbeA : register(t6);

// t7: main depth (DSV 2). Linear depth sampled at insn 6; used at
//     insn 7 for the depth<0.01 matrix-select test.
Texture2D<float4> g_tMainDepth : register(t7);

// t8: IBL probe cubemap ARRAY. Sampled at insn 63 with reflection vector
//     + array slice from t2 + roughness-derived mip.
TextureCubeArray<float4> g_tIBLProbeCube : register(t8);

// t9: kSSAO (RT 28). Single-channel sample at insn 261; the AO modulator.
Texture2D<float4> g_tSSAO : register(t9);

// t10: bilateral SSSS blur source (color). Sampled at center (insn 21)
//      and at every 9 bilateral-tap position inside the skin block.
//      TODO: identify; likely another ambient/diffuse contribution buffer.
Texture2D<float4> g_tBlurSource : register(t10);

// t11: precomputed ambient diffuse "B" (paired with t5).
Texture2D<float4> g_tAmbientDiffuseB : register(t11);

// t12: ambient/probe contribution "B"; sampled at center within skin
//      block (insn 253), added to t6.
Texture2D<float4> g_tAmbientProbeB : register(t12);

// t14: lit screen target (likely kMainPreAlpha = RT 2 or kMain).
Texture2D<float4> g_tLitScene : register(t14);

// t15: depth source for bilateral weight. Sampled at every bilateral tap;
//      difference vs r0.w (a depth value derived from depth at insn 82)
//      is the depth-bilateral weight.
Texture2D<float4> g_tBlurDepthRef : register(t15);

// Samplers (all mode_default; addressing modes set by D3D11 sampler state
// objects in the dispatch C++; TODO confirm).
SamplerState g_sGbufferNormal      : register(s1);
SamplerState g_sGbufferMaterial    : register(s2);
SamplerState g_sGbufferShadingData : register(s3);
SamplerState g_sSkinAuxColor       : register(s4);
SamplerState g_sAmbientDiffuseA    : register(s5);
SamplerState g_sAmbientProbeA      : register(s6);
SamplerState g_sMainDepth          : register(s7);
SamplerState g_sIBLProbeCube       : register(s8);
SamplerState g_sSSAO               : register(s9);
SamplerState g_sBlurSource         : register(s10);
SamplerState g_sAmbientDiffuseB    : register(s11);
SamplerState g_sAmbientProbeB      : register(s12);
SamplerState g_sLitScene           : register(s14);
SamplerState g_sBlurDepthRef       : register(s15);

// ----------------------------------------------------------------------------
// SSSS bilateral-blur kernel.
//
// 11 taps total: 1 center + 10 ring along one separable axis at offsets
// +/- {2.0, 1.28, 0.72, 0.32, 0.08}. The perpendicular pass lives in a
// sibling shader (not investigated here).
//
// Tap offsets are scaled by (CB0[0].xy * base) / depth-derived factor.
// The 10 ring taps are emitted as 5 paired CB0[0].xyxy MAD operations in
// the corpus asm. The center weight multiplies skinAux (t4), NOT the
// center sample of t10 (blurSourceCenter is unused once the kernel
// begins; the earlier sample is shared with the non-skin branch).
//
// Per-tap RGB weights match the Christensen-Burley SSSS approximation
// (per-wavelength absorption, red diffuses farthest).
// ----------------------------------------------------------------------------

// 10 symmetric ring offsets (5 negative + 5 positive); index i has weight
// SSSS_RING_WEIGHTS[i].
static const float2 SSSS_RING_OFFSETS[10] =
{
    float2(-2.000,  -2.000),
    float2(-1.280,  -1.280),
    float2(-0.720,  -0.720),
    float2(-0.320,  -0.320),
    float2(-0.080,  -0.080),
    float2( 0.080,   0.080),
    float2( 0.320,   0.320),
    float2( 0.720,   0.720),
    float2( 1.280,   1.280),
    float2( 2.000,   2.000),
};

// Symmetric per-RGB weights. Asm-extracted from blob 3559 corpus
// (insns 103 outer, 119 -1.28, 137 -0.72, 152 -0.32, 169 -0.08,
//  185 +0.08, 202 +0.32, 219 +0.72, 235 +1.28, 250 outer).
// Sum + center weight (0.560479, 0.669086, 0.784728) = ~1.0 per channel.
static const float3 SSSS_RING_WEIGHTS[10] =
{
    float3(0.004717, 0.000185, 0.000051),  // -2.0
    float3(0.019283, 0.002820, 0.000842),  // -1.28
    float3(0.036390, 0.013100, 0.006437),  // -0.72
    float3(0.077180, 0.113491, 0.079380),  // -0.32
    float3(0.082190, 0.035861, 0.020926),  // -0.08
    float3(0.082190, 0.035861, 0.020926),  // +0.08
    float3(0.077180, 0.113491, 0.079380),  // +0.32
    float3(0.036390, 0.013100, 0.006437),  // +0.72
    float3(0.019283, 0.002820, 0.000842),  // +1.28
    float3(0.004717, 0.000185, 0.000051),  // +2.0
};

// Center tap weight multiplies skinAux (t4), not blurSourceCenter (t10).
static const float3 SSSS_CENTER_WEIGHT = float3(0.560479, 0.669086, 0.784728);

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

    // ----- Insn 0: screen-space UV --------------------------------------
    float2 uv = input.position.xy * ScreenSize.xy;

    // ----- Insn 1-5: sample ambient pair + scale ------------------------
    float3 shadingData    = g_tGbufferShadingData.SampleLevel(g_sGbufferShadingData, uv, 0).xyw;
    float3 ambientA       = g_tAmbientDiffuseA.SampleLevel(g_sAmbientDiffuseA, uv, 0).xyz;
    float3 ambientB       = g_tAmbientDiffuseB.SampleLevel(g_sAmbientDiffuseB, uv, 0).xyz;
    float3 ambientPairSum = (ambientA + ambientB) * 3.0;

    // ----- Insn 6-20: sample depth, depth-based matrix select -----------
    // Corpus uses explicit `if/else` with per-row `mov rN.xyzw, cb12[K]`
    // (insns 7-20). Per-row ternary gives `movc rN, ...` which is the
    // closest fxc gets without [branch] making the issue worse.
    float depth = g_tMainDepth.SampleLevel(g_sMainDepth, uv, 0).y;
    bool isNearPath = (depth < 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    // ----- Insn 21: bilateral-blur center sample (color) ----------------
    float3 blurSourceCenter = g_tBlurSource.SampleLevel(g_sBlurSource, uv, 0).xyz;

    // ----- Insn 22-27: glossiness factor derivation ---------------------
    //   r0.z = shadingData.y * 3
    //   r1.y = saturate(shadingData.x - 0.3) -> rsq -> rcp -> min(1)
    //   r0.z = r0.z * r1.y
    float yTripled    = shadingData.y * 3.0;
    float rough01     = saturate(shadingData.x - 0.3);
    float roughInv    = (rough01 > 0.0) ? min(1.0 / sqrt(rough01), 1.0) : 1.0;
    float glossFactor = yTripled * roughInv;

    // ----- Insn 28-31: gbufferMaterial sample + IBL contribution gate ---
    //   r1.yw = t2.SampleLevel(...).xy
    //   r1.w = r1.w * r1.w * 50.0
    //   r2.w = (0.001961 < r1.y)
    float4 matRaw = g_tGbufferMaterial.SampleLevel(g_sGbufferMaterial, uv, 0);
    float matSliceFloat = matRaw.x;
    float matGlossOrSpec = matRaw.y;
    float glossSquaredScaled = matGlossOrSpec * matGlossOrSpec * 50.0;
    bool  hasIBL = (matGlossOrSpec > 0.001961);

    // ----- Insn 32-67: IBL cubemap reflection block ---------------------
    float3 iblColor = float3(0, 0, 0);
    if (hasIBL)
    {
        // Insn 33-39: octahedral normal decode from t1
        //   r9.xy = t1.SampleLevel(...).xy * 4 - 2
        //   r9.zw = (1 - r9.xy*r9.xy/4)
        //   normal = (r9.xy * sqrt(r9.z), -r9.w)
        float2 enc = g_tGbufferNormal.SampleLevel(g_sGbufferNormal, uv, 0).xy * 4.0 - 2.0;
        float  encDotEnc = dot(enc, enc);
        float  zRecon = 1.0 - encDotEnc * 0.25;
        float3 normalView = float3(enc * sqrt(zRecon), -(1.0 - encDotEnc * 0.5));

        // Insn 40-48: reconstruct view-space position via reproj matrix
        float3 uvRemapped;
        uvRemapped.x = uv.x * ScreenSize.z;
        uvRemapped.z = -uv.y * ScreenSize.w + 1.0;
        float2 uvNDC = uvRemapped.xz * 2.0 - 1.0;
        float4 pos4 = float4(uvNDC, linearizedDepth, 1.0);
        float4 posViewH;
        posViewH.x = dot(reprojRow0, pos4);
        posViewH.y = dot(reprojRow1, pos4);
        posViewH.z = dot(reprojRow2, pos4);
        posViewH.w = dot(reprojRow3, pos4);
        float3 posView = posViewH.xyz / posViewH.www;

        // Insn 49-54: view direction + reflection vector
        //   r3.xyw = -posView * rsqrt(dot(posView, posView))
        //   r2.w   = dot(r3.xyw, normalView) * 2
        //   r3.xyw = normalView * -r2.w + r3.xyw
        float3 viewDirNeg = -posView * rsqrt(dot(posView, posView));
        float  ndotv2     = 2.0 * dot(viewDirNeg, normalView);
        float3 reflView   = -normalView * ndotv2 + viewDirNeg;

        // Insn 55-57: rotate reflView -> world space via cb12[12..14]
        float3 reflWorld;
        reflWorld.x = dot(ViewToWorld_row0.xyz, reflView);
        reflWorld.y = dot(ViewToWorld_row1.xyz, reflView);
        reflWorld.z = dot(ViewToWorld_row2.xyz, reflView);

        // Insn 58-62: roughness-derived mip + array slice
        //   r1.x = (1 - shadingData.x) * 6   // roughness-to-mip
        //   r1.x = linearizedDepth * 0.001953 + r1.x  // depth term
        //   r1.y = matGlossOrSpec * 255 - 1
        //   r4.w = round_ni(r1.y)            // cubemap array slice
        float mipLevel = (1.0 - shadingData.x) * 6.0 + linearizedDepth * 0.001953;
        float arraySlice = floor(matGlossOrSpec * 255.0 - 1.0);

        // Insn 63: IBL cubemap sample
        float3 cubeSample = g_tIBLProbeCube.SampleLevel(g_sIBLProbeCube,
                                                        float4(reflWorld, arraySlice),
                                                        mipLevel).xyz;

        // Insn 64-67: luminance desaturation
        //   luma = dot(cubeSample, (0.299, 0.587, 0.114))
        //   weight = cb12[30].y * 0.9
        //   iblColor = lerp(cubeSample, luma.xxx, weight)
        float  luma   = dot(cubeSample, float3(0.299, 0.587, 0.114));
        float  desatW = cb12_idx30_ibl_desaturation.y * 0.9;
        iblColor      = lerp(cubeSample, luma.xxx, desatW);
    }
    else
    {
        // Insn 69: iblColor = 0 (else branch)
        iblColor = float3(0, 0, 0);
    }

    // ----- Insn 71-76: sample t14 lit-scene reference + blend with IBL --
    //   uv_clamped = min(uv, cb2[5].xy)
    //   r4 = t14.Sample(uv_clamped)
    //   r2.w = min(r4.w * cb0[2].z, 1.0)
    //   r4.xyz = r4.xyz * cb0[1].x - iblColor
    //   r3.xyz = lerp(iblColor, r4_lit, r2.w)  // [reformulated from MAD]
    float2 uvClamped     = min(uv, cb2_idx5_lit_scene_uv_clamp.xy);
    float4 litRaw        = g_tLitScene.Sample(g_sLitScene, uvClamped);
    float  litAlpha      = min(litRaw.w * cb0_idx2_lit_scene_alpha.z, 1.0);
    float3 iblLitBlend   = lerp(iblColor,
                                 litRaw.xyz * cb0_idx1_lit_scene_weight.x,
                                 litAlpha);
    // (Use iblLitBlend in place of "iblColor" from here on; r3.xyz in asm.)

    // ----- Insn 77-256: skin material (id == 5) bilateral blur block ----
    //   if (abs(shadingData.z * 255 - 5) < 0.25) { ... 9-tap separable
    //   SSSS blur over t10, weighted by t15 depth similarity ... }
    bool isSkin = (abs(shadingData.z * 255.0 - 5.0) < 0.25);
    float3 ambientAccum;
    if (isSkin)
    {
        // ----- Insn 80-84: skin block setup ------------------------------
        //   r4 = t4.Sample(uv).xyz
        //   r0.w = (r0.w & 0x3f800000) which masks to 1.0 if r0.w was 1.0;
        //          effectively r0.w = depth>=0.01 ? 1.0 : 0.0, then
        //          r0.w = r0.w * cb0[0].z + 1.0
        float3 skinAux = g_tSkinAuxColor.Sample(g_sSkinAuxColor, uv).xyz;
        float  depthMaskF = (depth >= 0.01) ? 1.0 : 0.0;
        float  blurDepthScale = depthMaskF * cb0_idx0_screen_scale_and_blur_tolerance.z + 1.0;

        float  refDepth = g_tBlurDepthRef.SampleLevel(g_sBlurDepthRef, uv, 0).y;
        float  centerRef = blurDepthScale * refDepth;

        // ----- Insn 85-87: per-tap offset basis -----------------------
        //   r5.xy = cb0[0].xx * (0.078125, 0.138890) / centerRef
        //   (these are kernel-spread base offsets in UV)
        float2 tapBase = cb0_idx0_screen_scale_and_blur_tolerance.xx
                       * float2(0.078125, 0.138890)
                       / centerRef;

        // ----- Insn 88-249: 10 ring-tap bilateral samples ---------------
        // Each tap follows the same pattern:
        //   tap_uv = uv + tapBase * SSSS_RING_OFFSETS[i]
        //   tap_mat = t3.Sample(tap_uv).x * 255 - 5  // skin id check
        //   if (abs(tap_mat) < 0.25): blend t10 sample with skinAux by
        //     depth-similarity dt; else use skinAux as the tap value.
        //   accumulator += tap_value * SSSS_RING_WEIGHTS[i]
        // The center contribution (SSSS_CENTER_WEIGHT * skinAux) is
        // added at the end.
        float3 blurAccum = float3(0, 0, 0);
        [unroll]
        for (int i = 0; i < 10; ++i)
        {
            float2 tapUV = uv + tapBase * SSSS_RING_OFFSETS[i];
            float  tapMatId = g_tGbufferShadingData.SampleLevel(
                                   g_sGbufferShadingData, tapUV, 0).x * 255.0 - 5.0;
            float3 tapBlended;
            if (abs(tapMatId) < 0.25)
            {
                float3 tapColor = g_tBlurSource.SampleLevel(
                                       g_sBlurSource, tapUV, 0).xyz;
                float  tapDepth = g_tBlurDepthRef.SampleLevel(
                                       g_sBlurDepthRef, tapUV, 0).y;
                float  dt = min(abs(-tapDepth * blurDepthScale + centerRef)
                                * cb0_idx0_screen_scale_and_blur_tolerance.y * 0.1,
                                1.0);
                tapBlended = lerp(tapColor, skinAux, dt);
            }
            else
            {
                tapBlended = skinAux;
            }
            blurAccum += tapBlended * SSSS_RING_WEIGHTS[i];
        }

        // ----- Center contribution: SSSS_CENTER_WEIGHT * skinAux --------
        blurAccum += SSSS_CENTER_WEIGHT * skinAux;

        // ----- Insn 252-256: ambient probe additions + skin accumulator
        //   r6 = t6.SampleLevel(uv)
        //   r0 = t12.SampleLevel(uv) (rgb in .xyw, .z unused)
        //   r0.xyw += r6.xyz + r4.xyz (= iblLitBlend) + blurAccum
        float3 probeA = g_tAmbientProbeA.SampleLevel(g_sAmbientProbeA, uv, 0).xyz;
        float3 probeB = g_tAmbientProbeB.SampleLevel(g_sAmbientProbeB, uv, 0).xyw.xyz;
        ambientAccum  = probeA + probeB + iblLitBlend + blurAccum;
    }
    else
    {
        // Insn 257: non-skin path -- ambient just uses the IBL-lit blend
        // (matched against r8 in the asm flow, which was previously set to
        // skinAux but only on the skin path).
        ambientAccum = iblLitBlend;
    }

    // ----- Insn 258-260: modulate by gloss + spec scaling ---------------
    //   r0.xyz = depth.zzz * iblLitBlend          (r0.z carries depth from earlier)
    //   r0.xyz = glossSquaredScaled * r0.xyz
    //   r0.xyz = r0.xyz * ambientPairSum + ambientAccum
    float3 modulated = ambientAccum
                     + (glossSquaredScaled * (linearizedDepth * iblLitBlend) * ambientPairSum);

    // ----- Insn 261-262: AO modulation - THE single AO application ------
    //   r0.w = t9.Sample(uv).y    (kSSAO)
    //   o0.xyz = r0.w * r0.xyz
    float aoFactor = g_tSSAO.Sample(g_sSSAO, uv).y;
    output.color.xyz = aoFactor * modulated;

    // ----- Insn 263: output alpha -------------------------------------
    output.color.w = 1.0;

    return output;
}

// ============================================================================
// Round-trip notes (for the reviewer + future maintainer)
//
// fxc round-trip status: see local roundtrip notes for the
// compile output + insn-count delta against the original.
//
// Round-trip result (fxc /T ps_5_0 /O3 /Ni, recompile + asm-mnemonic diff
// against the original at Shaders011.3559.7460585eaf76.dxbc.asm):
//
//   * Resource bindings: EXACT MATCH (14 SRVs + 14 samplers + 3 CBs).
//   * Signature: EXACT MATCH (fullscreen-quad SV_POSITION-only input,
//     single SV_Target output).
//   * Instruction count: 269 vs original 265 (+4 / +1.5%) - within the
//     ±10% threshold for this larger shader. The shared matrix-select
//     overhead is amortized over more instructions here.
//   * Sample count: 41 vs original 44 (-3). Cause identified: the
//     bilateral-blur kernel in this reconstruction has 9 ring taps
//     (-2.0, -1.28, -0.72, -0.32, -0.08, +0.08, +0.32, +0.72, +2.0)
//     while the original asm has 10 ring taps (the same plus a
//     +1.28 tap that pairs with the +2.0 final). The +1.28 tap is
//     missing from the SSSS_BLUR_OFFSETS table; adding it (and a
//     matching weight entry in SSSS_TAP_WEIGHTS) closes the sample-
//     count gap. Tracked under §`Shaders011.3559` open items.
//
// What is faithfully reconstructed:
//   * Resource declarations (14 SRVs + 14 samplers + 3 CBs) at exact
//     slot indices (t1..t12, t14, t15 - matching the corpus blob's
//     declarations including the gaps at t0 and t13).
//   * Input + output signatures.
//   * Major control flow: depth-based matrix select, IBL-contribution
//     gate, material-id-based skin/non-skin branch.
//   * Octahedral normal decode pattern (canonical FO4 / Skyrim CS
//     pattern).
//   * View-space position reconstruction shared with composite + VLS.
//   * Cubemap-array sampling with reflection vector + array slice +
//     roughness-derived mip.
//   * 9-of-10-tap separable SSSS bilateral blur with Christensen-
//     Burley per-RGB tap weights (kernel weights are asm-exact;
//     the +1.28 tap is the lone gap).
//   * SSGI AO-application boundary at the final multiply.
//
// What needs cross-read to finalize:
//   * Add the missing +1.28 ring tap to close the sample-count gap.
//     Trivial: append (1.28, 1.28) to SSSS_BLUR_OFFSETS + a matching
//     weight to SSSS_TAP_WEIGHTS; recompile.
//   * CB12 field semantics beyond [12..14, 20..27, 30].
//   * CB0 + CB2 field semantics.
//   * Texture role names for t4, t6, t10, t12, t15 (medium-confidence
//     placeholders inferred from asm usage; the rdoc capture's eid 45345
//     SRV bindings reference RT indices outside the public
//     `cs::engine::RenderTarget` enum and need IDA cross-read or
//     RenderTargetManager allocation log walk).
//   * The separable-blur PERPENDICULAR pass. This PS does one axis;
//     the other axis must be a sibling blob in Shaders011.fxp.
//     Search candidates: blobs near 3559 (3557-3561 range) with same
//     shape (14 SRVs, 263+/- insns).
//
// What is intentionally NOT done in this revision (separate work):
//   * Permutation diff against indoor / night capture.
//   * Skyrim CS SSSS analog comparison. Skyrim CS at
//     `package/Shaders/SubsurfaceScattering/` (if present) has a
//     separable SSSS implementation that would be worth diffing
//     against this FO4 10-tap kernel.
//   * Confirming the AO-application boundary survives across all
//     permutations (this PS may have IBL-off / SSSS-off variants
//     that move the AO application around).
// ============================================================================
