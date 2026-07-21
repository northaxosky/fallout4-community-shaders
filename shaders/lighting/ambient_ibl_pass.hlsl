// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Reconstruction of FO4 ambient/IBL deferred PS, Shaders011.fxp #3559 (corpus 7460585eaf76..., runtime 761d41008016..., eid 45345).
// Status: reference asm transcription; 263-insn fullscreen pass writes kDiffuseBuffer (RT 58) with 14 SRVs, 14 samplers, CB12/CB0/CB2.
// Host: DrawWorld::DeferredLightsImpl, REL::ID { OG=1108521, NG=2318312, AE=2318312 }.
// SSGI boundary: this shader applies kSSAO once at the final multiply, after IBL and SSSS, before downstream fog.
// Integration: write SSGI-modulated AO into kSSAO before this dispatch (RegisterPostDeferredPrePass).
// Flow: sample ambient pair/depth; select Far/Near reproj; reflect into IBL cube array; blend lit scene; apply skin SSSS blur; modulate by gloss and AO.
// Limits: CB12/CB0/CB2 names are partial; SSSS offsets need dispatch-site confirmation; perpendicular blur pass remains unidentified.

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..11]: not read directly by this PS. Per runtime capture
    // (FO4_frame5407.rdc):
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
    // [0]: per runtime capture (eid 45345 CB2 slot): .xy = RcpFrameDim
    //      (1/3840, 1/2160 in the
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

// Resource bindings. Semantic roles inferred from asm reading.

// t1: kGbufferNormal (RT 20). Octahedral-encoded normal sampled at insn 33;
//     decoded at insns 34-39.
Texture2D<float4> g_tGbufferNormal : register(t1);

// t2: kGbufferMaterial (RT 24). .y = material code / IBL array slice;
//     .z = gloss factor.
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

#ifdef SSGI
// SSGI injection (drop-in, not part of the vanilla contract): the GI resolve
// pass writes full-res, ready-to-combine bounce at a free slot.
//   t0  = indirect diffuse bounce (il * linAlbedo)
Texture2D<float4> g_tSSGIBounce : register(t0);
// Future albedo-aware MultiBounceAO may use t13 again.
#endif

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

// SSSS bilateral-blur kernel.
// 11 taps total: 1 center + 10 ring along one separable axis at offsets
// +/- {2.0, 1.28, 0.72, 0.32, 0.08}. The perpendicular pass lives in a
// sibling shader (not investigated here).
// Tap offsets are scaled by (CB0[0].xy * base) / depth-derived factor.
// The 10 ring taps are emitted as 5 paired CB0[0].xyxy MAD operations in
// the corpus asm. The center weight multiplies the t10 center sample
// (blurSourceCenter, corpus r8 from insn 21), which is also the bilateral
// lerp target for each ring tap.
// Per-tap RGB weights match the Christensen-Burley SSSS approximation
// (per-wavelength absorption, red diffuses farthest).

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

// Center tap weight multiplies the t10 center sample (blurSourceCenter).
static const float3 SSSS_CENTER_WEIGHT = float3(0.560479, 0.669086, 0.784728);

// Entry point.

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

    // Insn 0: screen-space UV.
    float2 uv = input.position.xy * ScreenSize.xy;

    // Insn 1-5: sample ambient pair + scale.
    float3 shadingData    = g_tGbufferShadingData.SampleLevel(g_sGbufferShadingData, uv, 0).xyw;
    float3 ambientA       = g_tAmbientDiffuseA.SampleLevel(g_sAmbientDiffuseA, uv, 0).xyz;
    float3 ambientB       = g_tAmbientDiffuseB.SampleLevel(g_sAmbientDiffuseB, uv, 0).xyz;
    float3 ambientPairSum = (ambientA + ambientB) * 3.0;

    // Insn 6-20: sample depth, depth-based matrix select.
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

    // Insn 21: bilateral-blur center sample (color).
    float3 blurSourceCenter = g_tBlurSource.SampleLevel(g_sBlurSource, uv, 0).xyz;

    // Insn 22-27: glossiness factor derivation.
    //   r0.z = shadingData.y * 3
    //   r1.y = saturate(shadingData.x - 0.3) -> rsq -> rcp -> min(1)
    //   rsq(0)=INF -> rcp=0, so rough01==0 yields 0 (unconditional, no guard).
    float yTripled    = shadingData.y * 3.0;
    float rough01     = saturate(shadingData.x - 0.3);
    float roughFactor = min(sqrt(rough01), 1.0);
    float glossFactor = yTripled * roughFactor;

    // Insn 28-31: gbufferMaterial sample + IBL contribution gate.
    //   r1.yw = t2.SampleLevel(...).yz
    //   r1.w = t2.z * t2.z * 50.0
    //   r2.w = (0.001961 < r1.y)
    float4 matRaw = g_tGbufferMaterial.SampleLevel(g_sGbufferMaterial, uv, 0);
    float matSliceFloat = matRaw.y;
    float matGlossOrSpec = matRaw.z;
    float glossSquaredScaled = matGlossOrSpec * matGlossOrSpec * 50.0;
    bool  hasIBL = (matSliceFloat > 0.001961);

    // Insn 32-67: IBL cubemap reflection block.
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
        //   r1.y = matSliceFloat * 255 - 1
        //   r4.w = round_ni(r1.y)            // cubemap array slice
        float mipLevel = (1.0 - shadingData.x) * 6.0 + linearizedDepth * 0.001953;
        float arraySlice = floor(matSliceFloat * 255.0 - 1.0);

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

    // Insn 71-76: sample t14 lit-scene reference + blend with IBL.
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

    // Insn 77-256: skin material (id == 5) bilateral blur block.
    //   if (abs(shadingData.z * 255 - 5) < 0.25) { ... 9-tap separable
    //   SSSS blur over t10, weighted by t15 depth similarity ... }
    bool isSkin = (abs(shadingData.z * 255.0 - 5.0) < 0.25);
    float3 ambientAccum;
    if (isSkin)
    {
        // Insn 80-84: skin block setup.
        //   r4 = t4.Sample(uv).xyz
        //   r0.w = (r0.w & 0x3f800000) which masks to 1.0 if r0.w was 1.0;
        //          effectively r0.w = depth>=0.01 ? 1.0 : 0.0, then
        //          r0.w = r0.w * cb0[0].z + 1.0
        float3 skinAux = g_tSkinAuxColor.Sample(g_sSkinAuxColor, uv).xyz;
        float  depthMaskF = (depth >= 0.01) ? 1.0 : 0.0;
        float  blurDepthScale = depthMaskF * cb0_idx0_screen_scale_and_blur_tolerance.z + 1.0;

        float  refDepth = g_tBlurDepthRef.SampleLevel(g_sBlurDepthRef, uv, 0).y;
        float  centerRef = blurDepthScale * refDepth;

        // Insn 85-87: per-tap offset basis.
        //   r5.xy = cb0[0].xx * (0.078125, 0.138890) / centerRef
        //   (these are kernel-spread base offsets in UV)
        float2 tapBase = cb0_idx0_screen_scale_and_blur_tolerance.xx
                       * float2(0.078125, 0.138890)
                       / centerRef;

        // Insn 88-249: 10 ring-tap bilateral samples.
        // Each tap follows the same pattern:
        //   tap_uv = uv + tapBase * SSSS_RING_OFFSETS[i]
        //   tap_mat = t3.Sample(tap_uv).w * 255 - 5  // skin id check
        //   if (abs(tap_mat) < 0.25): blend the t10 ring sample toward the
        //     t10 center (blurSourceCenter) by depth-similarity dt; else use
        //     the t10 center as the tap value.
        //   accumulator += tap_value * SSSS_RING_WEIGHTS[i]
        // The center contribution (SSSS_CENTER_WEIGHT * blurSourceCenter) is
        // added at the end.
        float3 blurAccum = float3(0, 0, 0);
        [unroll]
        for (int i = 0; i < 10; ++i)
        {
            float2 tapUV = uv + tapBase * SSSS_RING_OFFSETS[i];
            float  tapMatId = g_tGbufferShadingData.SampleLevel(
                                   g_sGbufferShadingData, tapUV, 0).w * 255.0 - 5.0;
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
                tapBlended = lerp(tapColor, blurSourceCenter, dt);
            }
            else
            {
                tapBlended = blurSourceCenter;
            }
            blurAccum += tapBlended * SSSS_RING_WEIGHTS[i];
        }

        // Center contribution: SSSS_CENTER_WEIGHT * t10 center sample
        // (corpus insn 104 weights r8 = the t10 sample from insn 21).
        blurAccum += SSSS_CENTER_WEIGHT * blurSourceCenter;

        // Insn 252-256: ambient probe additions + skin accumulator.
        //   r6 = t6.SampleLevel(uv); r0 = t12.SampleLevel(uv)
        //   r0.xyw += r6(t6) + r4(t4 skinAux) + blurAccum
        float3 probeA = g_tAmbientProbeA.SampleLevel(g_sAmbientProbeA, uv, 0).xyz;
        float3 probeB = g_tAmbientProbeB.SampleLevel(g_sAmbientProbeB, uv, 0).xyz;
        ambientAccum  = probeA + probeB + skinAux + blurAccum;
    }
    else
    {
        // Non-skin path: the corpus skin branch (if_nz r1.z, insn 79) has no
        // else, so r8 retains the t10 center sample from insn 21 as the
        // ambient base into the final combine (insn 260). t10 is the
        // precomputed ambient-diffuse buffer: non-skin uses it raw; skin
        // additionally SSSS-blurs it above.
        ambientAccum = blurSourceCenter;
    }

    // Insn 258-260: modulate by gloss + spec scaling.
    //   r0.xyz = glossFactor.xxx * iblLitBlend
    //   r0.xyz = glossSquaredScaled * r0.xyz
    //   r0.xyz = r0.xyz * ambientPairSum + ambientAccum
    float3 modulated = ambientAccum
                     + (glossSquaredScaled * (glossFactor * iblLitBlend) * ambientPairSum);

    // Insn 261-262: AO modulation - THE single AO application.
    //   r0.w = t9.Sample(uv).y    (kSSAO)
    //   o0.xyz = r0.w * r0.xyz
    float aoFactor = g_tSSAO.Sample(g_sSSAO, uv).y;
#ifdef SSGI
    // AO already arrives through the integrated engine t9 path.
    int3 ssgiPx = int3(int2(input.position.xy), 0);
    float3 giBounce = g_tSSGIBounce.Load(ssgiPx).rgb;
    output.color.xyz = aoFactor * modulated + giBounce;
#else
    output.color.xyz = aoFactor * modulated;
#endif

    // Insn 263: output alpha.
    output.color.w = 1.0;

    return output;
}

// Round-trip notes (for the reviewer + future maintainer)
// fxc round-trip status: see local roundtrip notes for the
// compile output + insn-count delta against the original.
// Round-trip result (shader_corpus_diff.py against corpus blob 3559):
//   * CONTRACT: PASS - resource bindings (14 SRVs + 14 samplers + 3 CBs)
//     and input/output signatures EXACT MATCH.
//   * Sample count: 44 vs original 44 (EXACT). The bilateral kernel has
//     the full 10 ring taps plus the t10 center tap; the earlier -3 gap
//     was the t10 center sample (blurSourceCenter) being dead-stripped
//     because the kernel wrongly used skinAux (t4) as the center source.
//   * Instruction stream: -2.6% vs corpus; remaining delta is compiler
//     shape noise (branch-vs-movc matrix select, register packing).
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
//   * Full 10-ring-tap + center separable SSSS bilateral blur with
//     Christensen-Burley per-RGB tap weights (asm-exact), bilaterally
//     blended toward the t10 center sample.
//   * SSGI AO-application boundary at the final multiply.
// What needs cross-read to finalize:
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
// What is intentionally NOT done in this revision (separate work):
//   * Permutation diff against indoor / night capture.
//   * Skyrim CS SSSS analog comparison. Skyrim CS at
//     `package/Shaders/SubsurfaceScattering/` (if present) has a
//     separable SSSS implementation that would be worth diffing
//     against this FO4 10-tap kernel.
//   * Confirming the AO-application boundary survives across all
//     permutations (this PS may have IBL-off / SSSS-off variants
//     that move the AO application around).
