// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction of FO4's deferred-composite pixel shader.
//
// Status: REFERENCE - CB12/CB2 field semantics, SRV format/role
//   mapping, output RT/blend/DSS/RS state, and material-ID encoding
//   are HIGH-confidence (Fallout4RE sister-repo handoff 2026-05-22,
//   knowledge/cross-runtime/deferred-composite-*.md). The following
//   items remain partially inferred or open and are NOT gating
//   shipping:
//     - rt_arg 26 (logical t0): numeric RTM cache key is authoritative;
//       the public engine-name string is unresolved (stale public enum
//       tagged it `kTAAAccumulation` but shader math + format suggest
//       albedo).
//     - t11 producer (logical g_tDirectSpecular): the per-frame writer
//       RVA is unresolved; the symbol name is the strongest inference
//       from format + symmetric pairing with t5 and composite math
//       `albedo * (t5 + t11)`. The handoff catalogs it as "HDR scratch
//       B; producer writer unresolved" (`cs-deferred-composite-srv-rt-
//       state.json` confidence=low for the producer field). The role
//       is plausible but not RE-confirmed.
//     - Friendly C++ symbol names for the FogState writer functions.
//       The source struct (BSGraphics::State::fogState) + offsets ARE
//       resolved; only PDB-level function names are missing.
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
//   Setup site: BSDFCompositeShader::SetupGeometry
//   OG  RVA 0x028770A0   NG RVA 0x020BA350   AE RVA 0x0220F9E0
//   This PS is the FIRST follow-up draw to the ambient/IBL pass (eid 45345);
//   it fires at eid 45368 in the canonical capture.
//
// What this shader does (high level, validated against asm + RE handoff):
//   1. Samples kGbufferMaterial.w from t3 (8-bit UNORM material code) and
//      linear depth from t7 (.x channel - depth SRV, not stencil).
//   2. Selects one of two 4x4 reprojection matrices from CB12 based on a
//      depth threshold (depth <= 0.01 -> Near, else Far). Same per-frame
//      reproj infrastructure as ambient/IBL (blob 3559), sun-light (3295),
//      VLS slice (2147).
//   3. Checks if the material code is in {2, 3} (kMaterialSkin /
//      kMaterialHair); if it IS, outputs 0 - those pixels are composited
//      by a different DrawWorld::DeferredComposite sub-pass.
//   4. Otherwise: combines albedo-base (t0) * (direct-diffuse t5 +
//      direct-specular t11), then mixes in a secondary color buffer (t4)
//      via grayscale-saturation lerp, then applies sun-direction lighting
//      via CB2 sun dir/color/spec-power.
//   5. Distance- and height-based fog blend using FogState.rangeData /
//      highLowRangeData / near/far/low/high colors (CB12 [14], [35], and
//      [41..46]).
//
// Output target: RT 172 = kMain (R11G11B10_FLOAT, RGB-only, no stored
// alpha). Blend: SrcAlpha/InvSrcAlpha Add, write mask RGB (alpha plane
// does not exist). DSS: depth Greater, read-only, stencil disabled.
// RS: solid, back-cull, front CCW, viewport 3840x2160, min depth 0.01.
// Samplers: point min/mag/mip, clamp UVW, compare Never (six aliases of
// the same sampler object at s0/s3/s4/s5/s7/s11 in the runtime descriptor).
//
// Reference: derivative reverse-engineering of Bethesda's compiled shader.
// Licensed under this repo's terms; the asm-level math derives from
// D3DDisassemble output of corpus blob 3539. Field semantics and
// resource bindings sourced from Fallout4RE knowledge/cross-runtime/
// deferred-composite-{cb-layouts,srv-rt-state,permutations-and-matid,
// coupling-pbr-roundtrip}.md (commit d178928 .. cb9c400, 2026-05-22).

// ----------------------------------------------------------------------------
// Constant buffer layouts.
// CB indices match the corpus blob (CB12[47] = 47 vec4s, CB2[3] = 3 vec4s).
// CB12 field semantics are HIGH-confidence per
// `cs-deferred-composite-cb-layouts.json` (writer: BSGraphics::Renderer::
// SetPerFrameConstants at OG 0x01D11160 / NG 0x01702EF0 / AE 0x0181DBE0).
// CB2 fields are HIGH-confidence per the BSDFCompositeShader::SetupGeometry
// upload at OG 0x028770A0 / NG 0x020BA350 / AE 0x0220F9E0.
// ----------------------------------------------------------------------------

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..13]: not read directly by this PS.
    // For cross-reference (runtime evidence cb12-runtime-evidence.json):
    //   [0..2]  ViewRotation rows (orthonormal 3x3, world -> view)
    //   [3]     ViewMatrix_row3 (homogeneous identity)
    //   [4..7]  Projection rows (focal_x=1.19, focal_y=2.12, near=15);
    //           [5].z is TAA-patched mid-frame
    //   [8..10] PrevFrame_ViewProj; [9] TAA-patched
    //   [11]    duplicate of [2]
    //   [12..14] ViewToWorld rows (transpose of [0..2]); this PS reuses
    //           [14] as a fog-plane equation via dp4.
    float4 cb12_pad_0_13[14];

    // [14] = ViewToWorld_row2_fog_plane.
    //   Third row of the view-to-world rotation. Sourced from
    //   BSGraphics::CameraStateData.camViewData.viewMat, transposed by
    //   SetPerFrameConstants. Composite dots it with reconstructed
    //   view-space position (with .w=1) to produce a world-height-like
    //   distance, then adds cb12[35].z (camera position adjust) to anchor
    //   the fog plane to world origin.
    float4 ViewToWorld_row2_fog_plane;

    // [15..19]: not read directly by this PS. Runtime semantics:
    //   [15]    ViewToWorld_row3 (homogeneous identity continuation)
    //   [16..18] WorldToView block (camera_pos partial in .w); [18] TAA-patched
    //   [19]    Depth reciprocal params (near recip + depth-range recip)
    float4 cb12_pad_15_19[5];

    // [20..23] = Far-depth reprojection matrix.
    //   Sourced from CameraStateData.camViewData.projMat, staged by
    //   SetCameraData and inverted/transposed by SetPerFrameConstants.
    //   Selected by composite, ambient/IBL, sun-light, and VLS when the
    //   sampled depth is >= 0.01. 0.84 / 0.47 diagonal in the captured
    //   frame. [21].w is TAA-patched mid-frame.
    float4 FarReproj_row0;
    float4 FarReproj_row1;
    float4 FarReproj_row2;
    float4 FarReproj_row3;

    // [24..27] = Near-depth reprojection matrix.
    //   Sourced from renderer camera constant staging [138..141], patched
    //   from inverse projection during DrawWorld::DeferredPrePass
    //   (OG 0x02850CB0 / NG 0x02096710 / AE 0x021EBDA0). Selected when
    //   sampled depth is <= 0.01 (first-person/near-depth geometry).
    //   [24].w + [25].w carry TAA sub-pixel camera offsets.
    float4 NearReproj_row0;
    float4 NearReproj_row1;
    float4 NearReproj_row2;
    float4 NearReproj_row3;

    // [28..34]: not read directly by this PS.
    //   [28]    Clip_planes_and_fog (near=0.02, far=125, density=1.2, far=160)
    //   [29]    Sun-light skin/SSS sincos pair; composite does NOT read it
    //           (verified refuted as composite tonemap/exposure).
    //   [30..34] camera/posAdjust derivatives consumed by siblings, not
    //           by composite.
    float4 cb12_pad_28_34[7];

    // [35] = CameraPosAdjust_for_fog_height.
    //   BSGraphics::CameraStateData.posAdjust staged by SetCameraData at
    //   renderer constant staging +0x6B0. Composite uses .z as the
    //   additive bias after dot(cb12[14], pos), giving the fog-height
    //   plane a world-space origin.
    float4 CameraPosAdjust_for_fog_height;

    // [36..40]: not read directly by this PS. SetPerFrameConstants
    //   uploads matrices and position vectors here for siblings.
    float4 cb12_pad_36_40[5];

    // [41] = FogDistanceRamp_and_lowHeightRamp.
    //   Packed reciprocal scale + bias from BSGraphics::State.fogState
    //   (rangeData + highLowRangeData). Composite reads .x/.z for the
    //   distance-fog linear remap; .y/.w carry the first height-ramp
    //   pair (not consumed by this PS, used by siblings).
    float4 FogDistanceRamp_and_lowHeightRamp;

    // [42] = FogNearLowColor_and_power.
    //   Near/low fog color converted to linear via pow(color, 2.2), plus
    //   fog power exponent in .w. Source: fogState.nearLowColor +
    //   fogState.power.
    float4 FogNearLowColor_and_power;

    // [43] = FogNearHighColor_and_clamp.
    //   Near/high fog color in linear space, plus clamp / minimum
    //   intensity threshold in .w (used both as movc default and as the
    //   lt comparator against the final fog mix factor).
    //   Source: fogState.nearHighColor + fogState.clamp.
    float4 FogNearHighColor_and_clamp;

    // [44] = FogFarLowColor_and_highDensityScale.
    //   Far/low fog color in linear space, plus high-density scale in .w
    //   used to bias fog intensity in the blend-weight math.
    //   Source: fogState.farLowColor + fogState.highDensityScale.
    float4 FogFarLowColor_and_highDensityScale;

    // [45] = FogFarHighColor_and_padding.
    //   Far/high fog color in linear space. .w is the FogState padding
    //   word copied through unchanged.
    //   Source: fogState.farHighColor + fogState.padding.
    float4 FogFarHighColor_and_padding;

    // [46] = FogHeightRampScaleBiasPair.
    //   Two saturated height remap pairs used as distance-dependent fog
    //   blend endpoints: .xy are reciprocal ranges, .zw are corresponding
    //   biases. Source: fogState.highLowRangeData.
    float4 FogHeightRampScaleBiasPair;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0] = ScreenSize.
    //   .xy = reciprocal render-target dimensions, .zw = render-target
    //   dimensions. In FO4_frame5407 the value is
    //   (1/3840, 1/2160, 3840, 2160). Source: BSDFCompositeShaderPixel-
    //   Constants, bound at PS b2 by SetupGeometry.
    float4 ScreenSize;

    // [1] = SunDirection_and_intensity.
    //   .xyz = sun direction transformed into view space and negated to
    //   match shader lighting convention. .w = sun intensity scalar.
    //   Source: ShadowSceneNode directional sun BSLight -> NiLight,
    //   composed with the renderer view basis. Same slot/convention as
    //   sun-light (blob 3295) CB2[1].
    //   FO4_frame5407 captured value: (0.833, 0.545, -0.091, 1.0).
    float4 SunDirection_and_intensity;

    // [2] = SunColor_and_SpecPower.
    //   .xyz = directional sun color converted to linear via
    //   pow(color, 2.2) and multiplied by light intensity. .w = composite
    //   specular power exponent. FO4_frame5407 value:
    //   (0.759, 0.759, 0.759, 8.0).
    float4 SunColor_and_SpecPower;
};

// ----------------------------------------------------------------------------
// Resource bindings.
// Slot indices match the corpus blob 3539 declarations exactly. The runtime
// re-encoded variant (rdoc eid 45368) uses sequential slots t0..t5 instead,
// but the LOGICAL bindings are identical per the 2026-05-18 mnemonic-diff
// finding. SRV identities + formats below are HIGH-confidence per
// `cs-deferred-composite-srv-rt-state.json` (setup site:
// BSDFCompositeShader::SetupGeometry, AE 0x0220F9E0 / NG 0x020BA350).
// The setup binding wrapper for t0 keys off `rt_arg 26` (the engine RTM
// cache key), which historically tagged `kTAAAccumulation` in a stale
// public enum. The shader's math (`baseColor * (diffuse + specular)`) and
// the runtime format (R8G8B8A8_SRGB at RT 250) are albedo-like, so the
// shader-visible name reflects that role; the public engine string for
// rt_arg 26 is the only remaining open question on this surface.
// ----------------------------------------------------------------------------

// t0: G-buffer albedo / base-color source. Per rdoc eid 45368 slot 0 =
//     runtime RT 250, R8G8B8A8_SRGB. Setup wrapper binds rt_arg 26
//     (numeric RTM key authoritative; public enum-name string is the
//     open question; see header comment block).
Texture2D<float4> g_tHdrBaseColor : register(t0);

// t3: kGbufferMaterial. The material ID is `round(t3.w * 255.0)` (8-bit
//     UNORM enum, range 0..20 vanilla + reserved). Per rdoc eid 45368
//     slot 1 = runtime RT 256, R8G8B8A8_UNORM, unnamed RT 30 at setup.
//     The .w-channel decode is locked by the prepass producer
//     (BSDFPrePassShader SV_Target2 / o2) and by the BSDFLight
//     deferred-light material-ID gate at code 1 (SSS).
Texture2D<float4> g_tMaterialIdBuffer : register(t3);

// t4: secondary color aux (HDR RGB at half-res or full-res depending
//     on path). Per rdoc slot 2 = runtime RT 253, R8G8B8A8_SRGB,
//     unnamed RT 31 at setup. Bind conditional on engine flag
//     `unk_143E475F6`. Sampled with mip filtering and combined into
//     the grayscale-saturation tonemap branch.
Texture2D<float4> g_tSecondaryColor : register(t4);

// t5: direct-light diffuse accumulation. Per rdoc slot 3 = runtime
//     RT 389, R11G11B10_FLOAT, unnamed RT 33 at setup. Verified as
//     "direct-light diffuse backing resource" in both
//     FO4_frame5407 and FO4_frame9483 captures. This is the
//     kDiffuseBuffer-family target written by BSDFLight + sun-light.
Texture2D<float4> g_tDirectDiffuse : register(t5);

// t7: linear depth buffer. Per rdoc slot 4 = depth target 183 (D24S8).
//     Wrapper binds via SetTextureDepth(slot 7, depth_arg 1), NOT
//     SetTextureStencil; the shader samples source `.x` (depth channel)
//     directly. The earlier `.y` reconstruction was a transcription
//     error fixed at the 2026-05-20 re-baseline.
Texture2D<float4> g_tLinearDepth : register(t7);

// t11: direct-light HDR scratch B; inferred to be the specular-light
//      accumulation companion to t5. Per rdoc slot 5 = runtime RT 395,
//      R11G11B10_FLOAT, unnamed RT 35 at setup. Bind active on the bit
//      `0x10000` permutation path (always-on for the main composite
//      fullscreen draw). RE handoff status: producer writer RVA
//      UNRESOLVED (`cs-deferred-composite-srv-rt-state.json`
//      `confidence=low` for the producer field). The "specular" name
//      reflects format + symmetric pairing with t5 and the composite
//      math `baseColor * (t5 + t11)` matching the standard deferred
//      diffuse + specular combine; treat as inference until a future
//      capture pins down the writer.
Texture2D<float4> g_tDirectSpecular : register(t11);

SamplerState g_sBaseColor       : register(s0);
SamplerState g_sMaterialId      : register(s3);
SamplerState g_sSecondaryColor  : register(s4);
SamplerState g_sDirectDiffuse   : register(s5);
SamplerState g_sDepth           : register(s7);
SamplerState g_sDirectSpecular  : register(s11);

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
    float2 uv = input.position.xy * ScreenSize.xy;

    // Insn 1-2: sample material-id (.w of t3) and linear depth (.x of t7).
    //   Corpus asm encodes the channels via the sample-instruction swizzle:
    //     insn 1: sample r0.z, t3.xywz   -> r0.z = texel.w (position z = 'w')
    //     insn 2: sample r0.w, t7.yzwx   -> r0.w = texel.x (position w = 'x')
    //   Reading .y here was a 2026-05-19 reconstruction error: it routed
    //   every pixel into the skin/hair branch (output = 0, black screen)
    //   because t3.y is not in {2/255, 3/255} but t3.w is the material code.
    float matIdRaw = g_tMaterialIdBuffer.SampleLevel(g_sMaterialId, uv, 0).w;
    float depth    = g_tLinearDepth.SampleLevel(g_sDepth, uv, 0).x;

    // Insn 3-16: select reprojection matrix based on depth threshold.
    //   if (depth <= 0.01)  -> use NEAR matrix, scale depth by 100
    //   else                -> use FAR matrix,  linearize depth slightly
    // Per-row ternary matches corpus shape closer than `float4x4` ?:.
    // Corpus blob Shaders011.3539.861504f6dcbe uses `ge l(0.010000)` at i=3.
    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

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
        // -------- Insn 21-25: sample albedo + direct-light pair, combine. -
        // Sums the two direct-light accumulation buffers and modulates by
        // base color. This is the "vanilla deferred lit" term for material
        // codes other than skin/hair.
        float3 baseColor    = g_tHdrBaseColor.SampleLevel(g_sBaseColor, uv, 0).xyz;
        float3 directDiff   = g_tDirectDiffuse.SampleLevel(g_sDirectDiffuse, uv, 0).xyz;
        float3 directSpec   = g_tDirectSpecular.SampleLevel(g_sDirectSpecular, uv, 0).xyz;
        float3 directTotal  = directDiff + directSpec;
        float3 litColor     = baseColor * directTotal;

        // -------- Insn 26: sample secondary color (with mip filtering). --
        float3 secondaryColor = g_tSecondaryColor.Sample(g_sSecondaryColor, uv).xyz;

        // -------- Insn 27-35: reconstruct view-space position. -----------
        //   uvNDC.x = (uv.x * CB2[0].z)  remapped to [-1, +1]
        //   uvNDC.y = (-uv.y * CB2[0].w + 1) remapped to [-1, +1]
        //   pos4    = (uvNDC.xy, linearizedDepth, 1)
        //   posView = mul(reprojMatrix, pos4) / posView.w
        //   The asm uses dp4 on rows 0..3 of the matrix which is row-major
        //   mul (RE-confirmed: reproj is the inverse-projection block
        //   prepared by SetPerFrameConstants).
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

        // -------- Insn 36-37: fog-plane distance. -------------------------
        //   r0.w = 1
        //   r0.w = dp4(cb12[14], r0.xyzw)
        //   r0.w = r0.w + cb12[35].z
        // ViewToWorld_row2 dotted with view-space pos projects onto the
        // ground-plane axis; CameraPosAdjust.z anchors the plane to world
        // origin.
        float fogPlaneDistance = dot(ViewToWorld_row2_fog_plane, float4(posView, 1.0));
        fogPlaneDistance += CameraPosAdjust_for_fog_height.z;

        // -------- Insn 38-42: distance-based fog factor. ------------------
        //   r1.x = dot(posView, posView)     // length squared
        //   r1.y = sqrt(r1.x)                 // length
        //   r1.y = r1.y * cb12[41].x - cb12[41].z   // distance ramp
        //   r1.z = saturate(r1.y)
        float posViewLenSq = dot(posView, posView);
        float posViewLen   = sqrt(posViewLenSq);
        float distanceRamp = posViewLen * FogDistanceRamp_and_lowHeightRamp.x
                             - FogDistanceRamp_and_lowHeightRamp.z;
        float distanceFactor = saturate(distanceRamp);

        // -------- Insn 43-45: dual saturated remap of fog-plane distance. -
        //   r2.xy = saturate(fogPlaneDistance.xx * cb12[46].xy - cb12[46].zw)
        //   r0.w  = r2.x + distanceFactor * (r2.y - r2.x)   // lerp
        float2 fogRemapPair = saturate(fogPlaneDistance.xx
                                       * FogHeightRampScaleBiasPair.xy
                                       - FogHeightRampScaleBiasPair.zw);
        float  fogBlend     = lerp(fogRemapPair.x, fogRemapPair.y, distanceFactor);

        // -------- Insn 46-52: fog intensity (with threshold + scale). -----
        //   if (distanceRamp > 0.75) {
        //     r2.x = (distanceFactor - 0.75) * 4.0
        //     r2.x = r2.x * (1.0 - cb12[43].w) + cb12[43].w
        //     r2.x = min(r2.x, 1.0)
        //   } else {
        //     fogIntensityClamp = cb12[43].w
        //   }
        float fogIntensityClamp;
        if (distanceRamp > 0.75)
        {
            float t = (distanceFactor - 0.75) * 4.0;
            float scaled = t * (1.0 - FogNearHighColor_and_clamp.w)
                             + FogNearHighColor_and_clamp.w;
            fogIntensityClamp = min(scaled, 1.0);
        }
        else
        {
            fogIntensityClamp = FogNearHighColor_and_clamp.w;
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
        float distancePow   = pow(distanceFactor, FogNearLowColor_and_power.w);
        float fogIntensity  = min(fogIntensityClamp, distancePow);

        // -------- Insn 60-61: fog blend weight. ---------------------------
        //   r1.w = 1.0 - fogBlend          // unfogged weight
        //   r1.w = fogBlend * cb12[44].w + r1.w
        //   (equivalent to: fogBlend * (cb12[44].w - 1) + 1)
        float fogBlendWeight = fogBlend * FogFarLowColor_and_highDensityScale.w
                               + (1.0 - fogBlend);

        // -------- Insn 62-67: fog color (4-corner lerp). ------------------
        //   colorAC  = cb12[42].xyz + fogIntensity * (cb12[44].xyz - cb12[42].xyz)
        //   colorBD  = cb12[43].xyz + fogIntensity * (cb12[45].xyz - cb12[43].xyz)
        //   fogColor = colorAC + fogBlend * (colorBD - colorAC)
        // 4-corner = near/low/high X far/low/high; intensity selects between
        // low and high inside each near/far band, then height blends them.
        float3 fogColorAC = lerp(FogNearLowColor_and_power.xyz,
                                 FogFarLowColor_and_highDensityScale.xyz,
                                 fogIntensity);
        float3 fogColorBD = lerp(FogNearHighColor_and_clamp.xyz,
                                 FogFarHighColor_and_padding.xyz,
                                 fogIntensity);
        float3 fogColor   = lerp(fogColorAC, fogColorBD, fogBlend);

        // -------- Insn 68: combined fog scalar. ---------------------------
        float combinedFog = fogBlendWeight * fogIntensity;

        // -------- Insn 69-70: normalize view-space dir + scale. -----------
        //   r1.x = rsq(posViewLenSq)
        //   r0.xyz = posView * r1.x          // unit dir
        //   r0.w   = combinedFog * r1.y      // (per asm's r1.xxxy mul,
        //                                       r1.y at this point is nearEscape)
        float3 viewDirUnit = posView * rsqrt(posViewLenSq);
        float  fogMixFactor = combinedFog * nearEscape;

        // -------- Insn 71-76: sun-direction lighting. ---------------------
        //   NdotL    = max(dot(viewDirUnit, cb2[1].xyz), 0)
        //   specular = pow(NdotL, cb2[2].w) * cb2[1].w
        float NdotL    = max(dot(viewDirUnit, SunDirection_and_intensity.xyz), 0.0);
        float specular = pow(NdotL, SunColor_and_SpecPower.w)
                         * SunDirection_and_intensity.w;

        // -------- Insn 77-78: sun mixes the FOG color toward the sun color.
        // Corpus asm:
        //   77:   add r1.xyz, -r2.xyzx, cb2[2].xyzx     ; SunColor - fogColor
        //   78:   mad r0.xyz, r0.xxxx, r1.xyzx, r2.xyzx ; fogColor + spec*(...)
        // (Earlier reconstruction used secondaryColor here; that was wrong.)
        float3 sunlitFogColor = lerp(fogColor, SunColor_and_SpecPower.xyz,
                                     specular);

        // -------- Insn 79-83: grayscale-saturation tonemap path. ----------
        //   r1.xyz   = litColor * 3 + secondaryColor    // r6 * 3 + r7
        //   gray     = dot(r1.xyz, (1/3, 1/3, 1/3))
        //   r2.xyz   = gray - r0.xyz                    // gray - sunlitFog
        //   r1.xyz   = r1.xxx * r2.xyz + r0.xyz
        //     -> sunlitFog + gray * (gray - sunlitFog)
        //     -> lerp(sunlitFog, gray.xxx, gray)
        float3 ambientWeighted = litColor * 3.0 + secondaryColor;
        float  gray            = dot(ambientWeighted, float3(1.0/3.0, 1.0/3.0, 1.0/3.0));
        float3 graySaturated   = sunlitFogColor + gray * (gray.xxx - sunlitFogColor);

        // -------- Insn 80, 84: branch on combinedFog vs clamp threshold. --
        //   r1.w = (fogMixFactor < cb12[43].w)
        //   output.xyz = r1.w ? graySaturated : sunlitFogColor
        bool useGraySaturated = (fogMixFactor < FogNearHighColor_and_clamp.w);
        output.color.xyz = useGraySaturated ? graySaturated : sunlitFogColor;

        // -------- Insn 85: output alpha = fogMixFactor. -------------------
        // Output RT is R11G11B10_FLOAT (no stored alpha plane); the
        // SrcAlpha/InvSrcAlpha blend equation uses this as the RGB blend
        // factor at write time. Any post-composite shader sampling .w of
        // kMain gets a synthesized 1.0 (verified in
        // cs-framebuffer-alpha-contract.json).
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
// blob 3539 (sha1 861504f6dcbe...) and progressively hardened through
// the Fallout4RE 7-lane render-reconstruction campaign. The current
// status reflects the 2026-05-22 sister-repo handoff
// (Workspace/knowledge/cross-runtime/sister-repo-handoff-2026-05-22.md).
//
// Corpus diff status (per Fallout4RE artifact
// Scratch/composite-postfix-2026-05-23.md, re-run AFTER the 2026-05-23
// `<` -> `<=` near/far threshold fix from commit `e1e6e72`):
//
//   * Resource bindings:  EXACT MATCH (6/6, t0..t11; cb12 + cb2).
//   * Sample call count:  EXACT MATCH (6/6).
//   * Signature:          EXACT MATCH (SV_POSITION input -> SV_Target0).
//   * Declarations:       EXACT MATCH (decl_count_delta = 0).
//   * Instruction count:  84 vs corpus 90 (-6 / -6.7%). Earlier
//                         checked-in `+18 / +20%` claim was stale data
//                         from before the 2026-05-20 channel + branch
//                         fixes; current fxc output is leaner than the
//                         corpus.
//   * Tool verdict:       `MISMATCH`, 3 mismatches (1 instruction-
//                         stream-divergence, 1 constant-value-
//                         divergence with 2 must-fix examples, 1 cb-
//                         reference-divergence with 5 examples).
//
// What the tool's "must-fix" findings actually are:
//   The diff tool compares insn-by-insn at positional index. After fxc
//   reorders the emitted stream (sample call from corpus i=2 hoists to
//   ours i=6; the material-id branch fold restructures everything
//   downstream), the by-index alignment breaks down. Manual inspection
//   of the compiled asm vs corpus asm shows that every literal flagged
//   as a must-fix (`4.000000` at corpus i=48, `1.000000` at corpus
//   i=60) and every cb reference flagged (`cb12[14].xyzw`,
//   `cb12[35].z`, `cb12[44].w`, `cb2[1].w`, `cb2[2].w`) IS present in
//   our shader at the logically-matching site; the literals live at
//   lines ~405 and ~417-419 of this file with side-comments tying them
//   to the corpus insn positions. The tool flags them as divergences
//   purely because alignment slipped. A stream-alignment-aware diff
//   (control-flow-graph reconstruction first, then per-node mnemonic
//   compare) would likely report clean; building that tooling is a
//   non-trivial Fallout4RE follow-up.
//
//   The `<=` fix from commit `e1e6e72` resolved the original
//   `semantic_miss_detected` verdict (corpus `ge l(0.010000)` at i=3
//   maps to depth `<= 0.01`; pre-fix reconstruction used `<`). The
//   current `MISMATCH` verdict is the tool's structural by-index
//   noise, not a known semantic miss. Treat with appropriate
//   skepticism: a future capture-diff would be the right way to fully
//   close this out.
//
// Local sha-stability baseline:
//   `scripts/verify-shader-roundtrip.ps1` locks the fxc output SHA1 for
//   this file in `scripts/shader-roundtrip-baselines.json`. After any
//   intentional edit, re-baseline with
//   `verify-shader-roundtrip.ps1 -UpdateBaselines`.
//
// What is faithfully reconstructed:
//   * Resource declarations (6 SRVs, 6 samplers, 2 CBs) at exact slot
//     indices.
//   * Control flow (depth-based matrix select, material-id-based skin/
//     hair gate, near-distance escape branch, fog-intensity threshold
//     branch).
//   * All 6 texture sample calls with their LOD modes (5 SampleLevel(0),
//     1 Sample).
//   * The dp4 / dp3 / rsq / pow / lerp math is asm-faithful per insn.
//
// Open items (do not gate shipping):
//   * Public engine-name string for `rt_arg 26` (logical t0). The numeric
//     RTM cache key is authoritative; the BSDFLight surface also binds
//     rt_arg 26 with a similar shader-role-vs-stale-enum tension.
//   * Producer writer RVA for t11 (logical `g_tDirectSpecular`). Current
//     naming is the strongest inference from format + symmetric pairing
//     with t5 and the composite math; producer field is `confidence=low`
//     in the RE handoff catalog.
//   * Friendly C++ symbol names for the FogState writer functions. The
//     source struct (BSGraphics::State::fogState) and offsets are
//     resolved; only PDB-level function names remain a doc gap.
//   * Permutation diff against a second RenderDoc capture (indoor vs
//     outdoor) would tighten confidence in the `unk_143E475F6` gating
//     of t4 and the `0x10000` bit gating of t11.
// ============================================================================
