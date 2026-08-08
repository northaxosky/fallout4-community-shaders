// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// FO4 BSDFLightShader deferred PS, unshadowed POINTOMNI cookie family.
//
// This sibling owns exactly POINTOMNI + GOBOPROJECTION at DIRSPLITS=2. The
// source is extracted from the conformance-pinned legacy point path rather
// than extending it: that file's source SHA is producer-owned.
//
// Wave 1 reconstructs the SPECULAR x IGNORERIM cells. IGNOREROUGHNESS remains
// a compile-only control until its distinct body is reconstructed.
//
// The native ABI is CB12[30], CB2[15], t0/t1/t2/t3/t7 texture2d and
// s0/s1/s2/s3/s7 mode_default. It has no shadow resource or comparison
// sampler. The cookie is a dual-paraboloid atlas projection, not the
// POINTOMNI+SHADOW cube-array projection.
//
// Extraction basis: bsdf_light_deferred.hlsl lines 846-885, 891-1190 and
// 1194-1207 at SHA-256
// c35208e60a454667ca26ad1831127b0645bb4137eadd63f6d2ec8dc2270929f6.
// This provenance pin guards the copy boundary; it is not a fidelity claim.
//
// Native Wave 1 matrix:
//   sha1      SPECULAR  IGNORERIM  IGNOREROUGHNESS  disposition
//   9969e800  yes       no         no               control
//   fa6948ba  no        no         no               admitted
//   d3331d19  no        yes        no               admitted
//   f33e32f9  yes       yes        no               admitted
//   09c2bd09  no        no         yes              compile-only
//   a65b5952  yes       no         yes              compile-only
//
// The four admitted cells hold the macro deltas visible in native constant
// reads. SPECULAR adds cb12[28] reads x/y and cb12[29].x for the second
// hair lobe, then adds two LightColor reads in the specular paths. IGNORERIM
// removes only the roughness-scaled edge contribution and its LightColor read.
//
// IGNOREROUGHNESS is deliberately different. The two native controls prove it
// is a real axis, but this wave does not claim that the legacy visibility and
// rim body is its reconstruction. It therefore remains compilable without
// becoming an admitted ABI/read-count row. Combining it with IGNORERIM would
// silently manufacture two unsupported cells, so that pair fails closed.
//
// The dual-paraboloid cookie math stays local to this sibling. It starts from
// the unprojected light-space z for the hemisphere decision, normalizes the
// selected paraboloid vector twice, mirrors the negative hemisphere, and packs
// it into the upper half of the t7 atlas. It is not shared with the shadowed
// POINTOMNI path, whose cookie coordinates derive from its shadow projection.
//
// No shader-fidelity manifest entry accompanies this source. Its admission
// manifest pins archive-side declarations and body read behavior only; native
// numerical fidelity remains producer-owned. The legacy source's conformance
// SHA and its compiled point target remain unchanged by this additive file.
//
// Each rejection below excludes a structural neighbor that would otherwise
// compile with this contract while meaning something different:
// shadow resources, another light kind, a shadow filter, an ambient block,
// attenuation-only routing, the half-omni hemisphere, or a cascade blend.
// These are source-family boundaries, not engine-provided selector defaults.
//
// The admitted ABI pin includes reflected buffer sizes as well as instruction
// declarations, so an unused trailing constant cannot widen RDEF silently.
// The cookie rows are intentionally a separate resource-contract family from
// unshadowed POINTOMNI: t7/s7 is a required declaration, not an optional use.
// DIRSPLITS=2 is the decoder baseline only; no cascade selection occurs here.
// The verifier checks the extraction provenance before invoking fxc.
// Manifests without this optional pin retain their existing behavior.

#if !defined(POINTOMNI)
#  error "this source is the native POINTOMNI gobo family; define POINTOMNI"
#endif
#if !defined(GOBOPROJECTION)
#  error "this source owns the t7/s7 GOBOPROJECTION contract; define GOBOPROJECTION"
#endif
#if !defined(RGBSPEC)
#  error "every native unshadowed POINTOMNI gobo blob carries RGBSPEC"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; the decoder baseline is a native axis"
#endif
#if DIRSPLITS != 2
#  error "this source reconstructs DIRSPLITS=2 only"
#endif
#ifdef SHADOW
#  error "the shadowed POINTOMNI family has a distinct shadow-map ABI"
#endif
#if defined(DIRECTIONAL) || defined(POINTSPOT) || defined(SPOT)
#  error "POINTOMNI gobo is exclusive with the directional and projected light families"
#endif
#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  error "FILTER_* selects a shadow tap and is invalid without SHADOW"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the directional DIRSPLITS=1 family"
#endif
#ifdef AMBIENT
#  error "no native unshadowed POINTOMNI gobo blob carries AMBIENT"
#endif
#ifdef ATTENUATION_ONLY
#  error "ATTENUATION_ONLY is not a native POINTOMNI gobo permutation"
#endif
#ifdef HALFOMNI
#  error "HALFOMNI only occurs on the shadowed POINTOMNI path"
#endif
#ifdef BLENDSPLIT
#  error "BLENDSPLIT is a directional cascade axis"
#endif
#if defined(IGNORERIM) && defined(IGNOREROUGHNESS)
#  error "the combined IGNORERIM and IGNOREROUGHNESS cells are not admitted in Wave 1"
#endif

#include "deferred_contracts.hlsli"

// LIGHT_TYPE_POINT branch (the unshadowed point-light path).
// Canonical mapping:
//   * Runtime sha1:  9969e800683c... (FO4_frame24669, QASmoke + Pip-Boy light)
//   * Shape:         ps_5_0, 215 instructions, 5 samples, 5 SRVs
//                    (t0/t1/t2/t3/t7 texture2d), 5 default samplers
//                    (s0/s1/s2/s3/s7 - NO comparison sampler), 2 CBs
//                    (CB12[30], CB2[15]), SV_POSITION plus unused POSITION14,
//                    2 MRT outputs (o0.xyzw + o1.xyzw).
// "unshadowed" classification: this PS has no SampleCmp instructions and
// no cascade/cube-shadow texture array. It is dispatched for point lights that
// bake their per-pixel attenuation into a 2D light-cookie texture (t7). The
// tail transforms view-space position through cb2[11..14], perspective-divides,
// and applies the bytecode's dual-paraboloid projection before sampling t7.
//
// What the point branch does (interpreted from asm):
//   1. Reconstruct view-space position from screen UV + depth via the
//      same Far/Near reproject matrix pair as directional (cb12[20..27]).
//   2. toLight = cb2[1].xyz - posView; d = length(toLight).
//   3. radial attenuation = pow(saturate(1 - saturate(d/cb2[1].w)^z), 2.2)
//      where the exponent z = cb2[3].z and the linear scale/bias come
//      from cb2[3].y / cb2[3].x. cb2[1].w is the light radius.
//   4. Light-cookie sample: project posView through cb2[11..14],
//      perspective-divide, dual-paraboloid-project, sample t7. The cookie
//      result (.xyz) modulates both diffuse and specular at the end.
//   5. Early-out: if attenuation <= 0.001, write zeros and return.
//   6. Gbuffer decode + octahedral normal decode identical to directional.
//   7. Material-id branched BRDF uses toLight_normalized instead of sun direction.
//   8. MRT: o0 = diffuse * cookie * attenuation / 3, o1 = spec * cookie *
//      attenuation; o1.w = 1.

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block (see `deferred_contracts.hlsli`).
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    // [28]: hair specular scales and powers. Same role as in the directional
    //       path. Identifier is a legacy misnomer; see the CB12 block above.
    float4 cb12_idx28_sss_params;

    // [29]: hair specular tangent shifts. Same legacy misnomer.
    float4 cb12_idx29_sss_angles;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: .xy = RcpFrameDim (screen-size invariant for UV computation).
    float4 ScreenSize;

    // [1]: .xyz = view-space light position, .w = light radius.
    //      `cb2[1].xyz - posView = toLight`; .w normalizes distance.
    float4 LightPos_and_Radius;

    // [2]: .xyz = light color HDR.
    float4 LightColor_HDR;

    // [3]: .x = falloff bias; .y = falloff scale;
    //      .z = falloff exponent for the normalized-distance curve.
    float4 cb2_idx3_attenuation_curve;

    // [4..10]: not read by this PS.
    float4 cb2_pad_4_10[7];

    // [11..14]: 4x4 light-space transform matrix. A dp4 against
    //           (posView, 1) yields the projected vector for t7.
    float4 cb2_lightspace_row0;
    float4 cb2_lightspace_row1;
    float4 cb2_lightspace_row2;
    float4 cb2_lightspace_row3;
};

// t0: RT26 kTAAAccumulation; .w supplies the skin alpha mix.
Texture2D<float4> g_tGbufferAlbedo : register(t0);
// t1: RT27 kTAAAccumulationSwap (octahedral 2-channel normal).
Texture2D<float4> g_tGbufferNormal : register(t1);
// t2: RT30 unnamed G-buffer auxiliary.
Texture2D<float4> g_tGbufferMaterial : register(t2);
// t3: main depth, sampled with explicit gradients.
Texture2D<float4> g_tMainDepth : register(t3);
// t7: light cookie / projected texture, addressed via dual paraboloid.
Texture2D<float4> g_tLightCookie : register(t7);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);
SamplerState g_sLightCookie     : register(s7);

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
    float  scale = sqrt(recon);
    return float3(enc * scale, z);
}

// Dual-paraboloid cookie projection from the transformed light-space vector.
float2 ProjectCookieUV(float3 dirLightSpace, float unprojectedZ)
{
    float3 d = normalize(dirLightSpace);
    bool negativeHemisphere = (unprojectedZ * 0.5 + 0.5) < 0.0;
    d.z += negativeHemisphere ? -1.0 : 1.0;
    d = normalize(d);
    float2 uv = d.xy / d.zz;
    uv = uv * 0.5 + 0.5;
    uv.y = negativeHemisphere ? (1.0 - uv.y) * 0.5 + 0.5 : uv.y * 0.5;
    return uv;
}

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float ddx_ = ddx_coarse(uv.x);
    float ddy_ = ddy_coarse(uv.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    float2 uvNDC = uv4.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float3 toLight    = LightPos_and_Radius.xyz - posView;
    float toLightLenSq = dot(toLight, toLight);
    float d = sqrt(toLightLenSq);
    float dNorm = saturate(d / LightPos_and_Radius.w);
    float dPowZ = exp2(log2(dNorm) * cb2_idx3_attenuation_curve.z);
    float falloffLin = saturate(cb2_idx3_attenuation_curve.y * dPowZ
                                + cb2_idx3_attenuation_curve.x);
    float attenuation = exp2(log2(1.0 - falloffLin) * 2.2);

    if (attenuation <= 0.001)
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    float3 lightDir = toLight * rsqrt(toLightLenSq);
    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;
    float3 normalView = DecodeOctahedralNormal(normalEnc);
    float roughness01 = 1.0 - matSample.x;
    float posViewLenInv = rsqrt(dot(posView, posView));
    float3 viewDirNeg = -posView * posViewLenInv.xxx;

#ifdef SPECULAR
    float NdotL_raw = dot(normalView, lightDir);
    float NdotL_sat = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);
#endif

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
    float3 brdfSpecular = float3(0, 0, 0);
    float brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        float albedoW = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
        float skinNdotL = dot(matSample.xyz, lightDir);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));
#ifndef SPECULAR
        float NdotL_sat = max(dot(normalView, lightDir), 0.0);
#endif

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1 = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity = saturate(cb12_idx28_sss_params.z * pow1 + NdotL_sat);
        brdfShadowMix = min(albedoW, sssIntensity);

#ifdef SPECULAR
        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2 = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2 = exp2(log2(vis2) * cb12_idx28_sss_params.y) *
            cb12_idx28_sss_params.x;
        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
#endif
    }
    else
    {
        float NdotV_raw = dot(viewDirNeg, normalView);
#ifndef SPECULAR
        float NdotL_raw = dot(lightDir, normalView);
        float NdotL_sat = max(NdotL_raw, 0.0);
        float NdotL_clamped = saturate(NdotL_sat);
#endif
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = lightDir - normalView * NdotL_raw;
        float tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA = roughSq / (roughSq + 0.57);
        float visB = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;
        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix = NdotL_sat * visibilityGeom;

#ifdef SPECULAR
        float specExp = exp2(matSample.x * 10.0 + 1.0);
        float3 halfVec = lightDir + viewDirNeg;
        halfVec *= rsqrt(dot(halfVec, halfVec));
        float NdotV_sat = saturate(NdotV_raw);
        float VdotH = saturate(dot(viewDirNeg, halfVec));
        float NdotH = saturate(dot(halfVec, normalView));
        float distributionNorm = (specExp + 2.0) * 0.159155;
        float distribution = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;
        float VdotH_nonneg = max(VdotH, 0.0);
        float minN = min(NdotL_clamped, NdotV_sat);
        float twoNdotH = NdotH + NdotH;
        bool usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool useUnityRatio = (NdotV_sat == minN);
        float ratioNLNV = NdotL_clamped / NdotV_sat;
        float ratio = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;
        float oneMinusVdotH = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);
        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= 3.141593;
        brdfSpecular = NdotL_clamped * (specMag * LightColor_HDR.xyz);
#endif
    }

#ifndef SPECULAR
    float NdotL_raw = dot(normalView, lightDir);
    float NdotL_clamped = saturate(NdotL_raw);
#endif
#ifndef IGNORERIM
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float edge = exp2(log2(1.0 - NdotV_view) * 0.01);
    float toLightDotView = saturate(dot(viewDirNeg, -lightDir));
    float ambientTerm = toLightDotView * edge * NdotL_clamped * roughness01;
#else
    float ambientTerm = 0.0;
#endif
    float3 diffuseAccum = LightColor_HDR.xyz * ambientTerm;
    diffuseAccum += LightColor_HDR.xyz * brdfShadowMix;

    float4 posViewHomog = float4(posView, 1.0);
    float3 lsDir;
    lsDir.x = dot(cb2_lightspace_row0, posViewHomog);
    lsDir.y = dot(cb2_lightspace_row1, posViewHomog);
    lsDir.z = dot(cb2_lightspace_row2, posViewHomog);
    float lsW = dot(cb2_lightspace_row3, posViewHomog);
    float2 cookieUV = ProjectCookieUV(lsDir / lsW.xxx, lsDir.z);
    float3 cookieRGB = g_tLightCookie.Sample(g_sLightCookie, cookieUV).xyz;

    diffuseAccum *= cookieRGB;
    float3 specAccum = cookieRGB * brdfSpecular;
#ifdef SPECULAR
    output.specular.xyz = attenuation * specAccum;
#else
    output.specular.xyz = float3(0, 0, 0);
#endif
    output.specular.w = 1.0;
    output.diffuse.xyz = (attenuation * diffuseAccum) / 3.0;
    output.diffuse.w = 0.0;
    return output;
}

// Wave 1 C1 holds the bare, SPECULAR, IGNORERIM and SPECULAR+IGNORERIM
// cookie rows to their native declaration and constant-read contracts.
// IGNOREROUGHNESS compiles as a control but intentionally retains the base
// visibility and rim body until the follow-up reconstruction wave.
//
// Reconstructed invariants:
//   * Resource declarations (5 SRVs + 5 default samplers + 2 CBs) at
//     exact slot indices (t0/t1/t2/t3/t7, s0/s1/s2/s3/s7).
//   * CB12[30], CB2[15], SV_POSITION + unused POSITION14, MRT o0 + o1.
//   * Major control flow: depth-based reprojection select, radial attenuation,
//     early-out, material-id BRDF branch, dual-paraboloid cookie sample, and
//     final cookie-modulated composition.
