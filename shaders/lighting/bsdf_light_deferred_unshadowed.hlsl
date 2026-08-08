// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// FO4 BSDFLightShader deferred PS, native unshadowed light family: the nine
// archive blobs that carry no SHADOW and declare exactly t0..t3 with s0..s3 and
// no other resource. What this source owns is unshadowed lighting - not a shadow
// selection, and not a cascade count. The resource clause is load-bearing: 24
// decoded blobs carry DIRSPLITS=2 with no SHADOW, but the GOBOPROJECTION ones
// keep a t7 light cookie and the SPOT ones have their own contract, so this file
// is a resource-contract family, not that whole predicate.
//
// This is a sibling of `bsdf_light_deferred.hlsl`, not a replacement, in the
// same arrangement as `bsdf_light_deferred_dirsplits2.hlsl` (the shadowed
// DIRSPLITS=2 FILTER family) and `bsdf_light_deferred_shadow_only.hlsl` (the
// DIRSPLITS=1 SHADOW_ONLY family). The legacy adapter is pinned byte-for-byte
// by the producer conformance manifest, so a new native macro family goes in
// its own source rather than growing the pinned one.
//
// The ownership boundary is the absence of SHADOW, and that absence is not a
// selector value. SHADOW is simply inactive for these nine, so the shadow-
// resource axis does not exist for them and nothing may be grouped or compared
// along it. The native shader declares no shadow texture and no shadow sampler
// at all: exactly t0..t3 as texture2d, exactly s0..s3 as mode_default, and a
// much smaller CB2. There is therefore no filter axis here - FILTER_* selects a
// shadow tap, and there is no shadow to tap. The guards below reject every
// FILTER_* macro rather than reinterpreting a missing SHADOW as an unfiltered
// shadowed shader, and rather than admitting "no shadow" as a third tap mode
// alongside raw t4/s4 and comparison t5/s5.
//
// Reconstructed from these nine archive blobs, each disassembled on its own and
// read against its own constant table. No register role and no body logic is
// transferred by analogy from the ten passing no-SHADOW SPOT blobs, which are a
// structural reference only and stay with the legacy adapter.
//
// Cross-family native ASM comparison, so the reuse question is answered by
// measurement rather than by eye. Two controlled pairs were compared against the
// shadowed DIRSPLITS=2 family, each differing only by SHADOW and its filter:
//
//   039c8935 (DIRECTIONAL SPECULAR) vs shadowed 0fd35e4a  180 vs 222 instrs
//   9f44ba67 (POINTOMNI SPECULAR)   vs shadowed 2fe442f1  179 vs 240 instrs
//
//   IDENTICAL, verified byte-for-byte on the instruction text: the leading 26
//   and 38 instructions respectively - VPOS offset, depth fetch and view-space
//   position reconstruction.
//   NOT identical, equal only after renaming registers: the next 10 and 18 -
//   the G-buffer samples and the normal decode. Register allocation differs
//   (r2/r3 against r3/r4), so this is a similarity, not an identity, and it is
//   deliberately not upgraded into a shared include.
//   DIFFERENT: everything after that. The shadowed directional enters cascade
//   selection at `lt ..., cb2[9].w` where this family goes straight to lighting,
//   and the shadowed point builds a shadow projection with dp4 against
//   cb2[11]/cb2[12] where this family has no such matrix at all.
//
// So identity is claimed only for the prologue, and the BRDF, normal-decode and
// lighting bodies here are independently reconstructed with no cross-family
// reuse claim. Nothing is refactored into a shared .hlsli on similarity alone.
//
//   sha1      macros beyond DIRSPLITS=2 RGBSPEC              CB2  CB12  instrs
//   a9435eca  DIRECTIONAL                                    [3]  [30]     133
//   039c8935  DIRECTIONAL SPECULAR                           [3]  [31]     196
//   28858d7b  DIRECTIONAL SPECULAR IGNOREROUGHNESS           [3]  [31]     166
//   477c3e1e  DIRECTIONAL SPECULAR AMBIENT                   [9]  [31]     223
//   987c4e79  DIRECTIONAL SPECULAR AMBIENT IGNOREROUGHNESS   [9]  [31]     194
//   12d92cd3  POINTOMNI                                      [4]  [30]     142
//   9f44ba67  POINTOMNI SPECULAR                             [4]  [30]     196
//   b4337a89  POINTOMNI IGNORERIM                            [4]  [30]     130
//   fcabd749  POINTOMNI SPECULAR IGNORERIM                   [4]  [30]     187
//
// DIRSPLITS=2 is the decoder baseline for these nine, not an active cascade
// axis, and this file must not be read as owning two-cascade behaviour. The
// CB2 read-sets prove it: DIRECTIONAL reads exactly {0,1,2} or {0,1,2,6,7,8},
// POINTOMNI reads exactly {0,1,2,3}, and the declared CB2 sizes are 3, 9 and 4.
// Nothing reads a split-distance, cascade-projection or shadow world-scale or
// filter register, and those constants are not merely unread - SplitDistances,
// FadeDistances, ShadowMapProj and the rest are `absent` from all nine constant
// tables, so no register is allocated to them at all. The AMBIENT rows at
// cb2[6..8] are one DirectionalAmbient gradient occupying three registers, not
// a split/fade pair. The macro is carried because it is a native axis that is
// never assumed, not because a cascade is selected here.
//
// DIRECTIONAL and POINTOMNI keep separate light bodies even though their
// resource contracts are identical, because the disassembly differs and the
// difference is load-bearing:
//
//   1. DIRECTIONAL with SPECULAR reads cb12[30].y as a Schlick-shaped gloss
//      term, scales the specular exponent by `1 - schlickFres * 0.98`, and
//      scales the specular output by `1 - schlickFres * 0.5`. POINTOMNI with
//      SPECULAR does none of that: its exponent is the raw `exp2(gloss*10+1)`
//      and its output has no such factor. That single omission is why every
//      POINTOMNI blob declares CB12[30] where the specular directional ones
//      declare CB12[31].
//   2. POINTOMNI reads cb2[3] `LightAttenuation` and derives its light
//      direction from a light position at cb2[1].xyz with a radius at
//      cb2[1].w. No directional blob reads cb2[3] at all; its cb2[1] is a
//      direction.
//   3. The directional composition adds three terms the point one has no
//      instructions for: the premultiplied-albedo backface wrap, the
//      depth-scaled forward blend, and the albedo tint. Bare POINTOMNI samples
//      t0 only inside the hair branch, and only its .w channel.
//
// Equality level, stated plainly: this family is admitted on its declared ABI
// and its constant read behaviour, not on its instruction stream.
// `scripts/shaders/verify-native-abi-admission.ps1` re-measures, for all nine
// native macro sets, that the reconstruction declares the same constant
// buffers, SRVs, samplers and signature as the blob the macro set came from,
// that it reads the same set of constant-buffer registers, and that it reads
// each of them the same number of times. That is a contract claim only. It is
// not an execution-equivalence claim, and this repo cannot bless its own
// output: execution proof stays with the producer oracle in the sibling
// `fallout4-re`.
//
// No axis here or in the shadowed DIRSPLITS=2 family is exempted from the
// read-count pin. IGNOREROUGHNESS and IGNORERIM are both reconstructed; the
// controlled pairs below pin this layer, while producer oracle evidence
// completed the shadowed reconstruction.

#if defined(SHADOW)
#  error "this source is the no-SHADOW family; the shadowed DIRSPLITS=2 blobs are bsdf_light_deferred_dirsplits2.hlsl"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the DIRSPLITS=1 family in bsdf_light_deferred_shadow_only.hlsl"
#endif
#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  error "FILTER_* selects a shadow tap and requires SHADOW; a missing SHADOW is never a filtered shader"
#endif
#ifdef HALFOMNI
#  error "HALFOMNI occurs only with POINTOMNI and SHADOW; it is not part of this layer"
#endif
#ifdef GOBOPROJECTION
#  error "GOBOPROJECTION blobs declare t7/s7 and are a different resource contract"
#endif
#if defined(SPOT) || defined(POINTSPOT)
#  error "the no-SHADOW SPOT blobs stay with the legacy adapter in bsdf_light_deferred.hlsl"
#endif
#if (defined(DIRECTIONAL) + defined(POINTOMNI)) != 1
#  error "define exactly one of DIRECTIONAL or POINTOMNI; the archive also carries a no-light-kind AMBIENT blob at DIRSPLITS=2 that this source does not reconstruct"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; it is a native axis and is never assumed, even though these nine only carry it as a decoder baseline"
#endif
#if DIRSPLITS != 2
#  error "this source reconstructs DIRSPLITS=2 only; DIRSPLITS=1 and DIRSPLITS=3 are separate native families"
#endif
#if !defined(RGBSPEC)
#  error "every native no-SHADOW DIRSPLITS=2 blob carries RGBSPEC"
#endif

#ifdef DIRECTIONAL
#  ifdef IGNORERIM
#    error "no DIRECTIONAL blob carries IGNORERIM; the archive puts it on POINTOMNI and SPOT only"
#  endif
#  if defined(AMBIENT) && !defined(SPECULAR)
#    error "the two AMBIENT directional blobs both carry SPECULAR; AMBIENT alone is not a native set here"
#  endif
#  if defined(IGNOREROUGHNESS) && !defined(SPECULAR)
#    error "all 19 decoded DIRECTIONAL+IGNOREROUGHNESS blobs carry SPECULAR; the 11 archive blobs that drop SPECULAR are POINTOMNI/POINTSPOT/SPOT, never DIRECTIONAL"
#  endif
#endif

#ifdef POINTOMNI
#  ifdef AMBIENT
#    error "no POINTOMNI blob carries AMBIENT; its CB2[4] has no DirectionalAmbient rows"
#  endif
#  ifdef IGNOREROUGHNESS
#    error "POINTOMNI carries IGNOREROUGHNESS only together with GOBOPROJECTION, which this source rejects"
#  endif
#endif

// Shared CB12[0..27] per-frame schema (single source of truth across the
// deferred-pipeline PS reconstructions). Included unchanged: the conformance
// manifest pins the shaders that also include it.
#include "deferred_contracts.hlsli"

// Internal selector. This names what the native declarations move together; it
// is not a macro the engine defines, and nothing outside this file sets it.
//
// The Schlick-shaped gloss term at cb12[30].y, and both places it is consumed,
// are native to the specular DIRECTIONAL body and absent from every POINTOMNI
// body. Reading it is what raises the declared CB12 size from 30 to 31.
#if defined(DIRECTIONAL) && defined(SPECULAR)
#  define FO4_UNSHADOWED_USES_GLOSS_FRESNEL 1
#endif

// Constant buffer layouts.
//
// A permutation must match the native size in BOTH places fxc records it, and
// the two are driven by different things:
//
//   * the SHEX `dcl_constantbuffer CBn[size]` is sized from the highest
//     register the body actually READS, plus one, so unread trailing members
//     leave it alone;
//   * the RDEF reflection block is sized from what the source DECLARES, so an
//     unread trailing member still widens it and is merely marked [unused].
//
// So a member that is declared unconditionally but read only by some
// permutations is invisible in the instruction stream and still wrong in
// reflection. Every trailing member below is therefore declared under the same
// condition that reads it, which makes both sizes fall out together: CB12 is
// [30] unless the gloss term above is read, and CB2 is [3] for directional,
// [4] once LightAttenuation is read, and [9] once the ambient gradient rows
// are read.

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block. See `deferred_contracts.hlsli`.
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    // [28]: hair specular parameters (fHairPrimSpecScale, fHairPrimSpecPow,
    //       fHairSecSpecScale, fHairSecSpecPow). Not subsurface scattering.
    float4 cb12_idx28_hair_spec_params;

    // [29]: hair specular tangent shifts (fHairPrimSpecShift,
    //       fHairSecSpecShift, 0, 0).
    float4 cb12_idx29_hair_spec_shifts;

    // [30]: .y is the 1 - x raised-to-fourth gloss term in the Schlick-shaped
    //       fresnel scale. Declared only for specular DIRECTIONAL, the one
    //       family that reads it. Declaring it everywhere left the base
    //       directional and every POINTOMNI permutation reflecting CB12 as 31
    //       registers - 496 bytes - against a native 30, or 480.
    //
    //       The condition is spelled out rather than reusing
    //       FO4_UNSHADOWED_USES_GLOSS_FRESNEL, which is defined as exactly the
    //       same pair, so the resource contract can be read here without
    //       resolving a macro. Keep the two in step.
#if defined(DIRECTIONAL) && defined(SPECULAR)
    float4 cb12_idx30;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: constant ID 0 `VPOSOffset`. .xy scales SV_POSITION into the sampling
    //      UV, .zw into the NDC pair.
    float4 ScreenSize;

    // [1]: constant ID 1 `LightVector`. Under DIRECTIONAL, .xyz is the sun
    //      direction in view space. Under POINTOMNI, .xyz is the light
    //      position in view space and .w is the radius the distance is
    //      normalised by - a different meaning in the same register, which is
    //      one reason the two bodies are reconstructed separately.
    float4 LightVector;

    // [2]: constant ID 2 `LightColor`. .xyz in the BSDFLightShader HDR-scale
    //      convention.
    float4 LightColor_HDR;

#if defined(POINTOMNI)
    // [3]: constant ID 3 `LightAttenuation`. .x is the attenuation bias, .y
    //      the scale and .z the falloff exponent. Every POINTOMNI blob reads
    //      all three, which is what makes its declared CB2 size 4. No
    //      directional blob allocates this register.
    float4 LightAttenuation;
#elif defined(AMBIENT)
    // [3..5]: constant IDs 3 `LightAttenuation`, 4 `ProjectedLightVector` and
    //         5 `SpotData` are absent from the directional blobs - not merely
    //         unread, they have no allocated register. The hole is what puts
    //         `DirectionalAmbient` at register 6.
    float4 cb2_pad_3_5[3];

    // [6..8]: constant ID 6 `DirectionalAmbient`, the ambient gradient rows
    //         for the diffuse and specular image-based terms.
    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#endif
};

// Resource bindings. All nine blobs declare exactly these, and nothing else:
// four g-buffer texture2d reads and four mode_default samplers. There is no
// shadow array, no comparison sampler, and no gobo projector.

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

// Helpers.

static const float FO4_SPECULAR_SCALE = 3.141593;

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
    return float3(enc * sqrt(recon), z);
}

#ifdef AMBIENT
float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded;
    encoded.x = dot(cb2_ambient_gradient_row0, directionH);
    encoded.y = dot(cb2_ambient_gradient_row1, directionH);
    encoded.z = dot(cb2_ambient_gradient_row2, directionH);
    return exp2(log2(encoded) * 2.2);
}
#endif

struct PS_INPUT
{
    float4 position  : SV_POSITION;
    float4 posUnused : POSITION14;   // unused interpolant; matches the corpus ISGN
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;  // -> kDiffuseBuffer (RT 58)
    float4 specular : SV_Target1;  // -> kSpecularBuffer (RT 59)
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    // Near/far reprojection partition. Native selects the row set with a real
    // branch rather than a per-component select, and the near select is
    // inclusive at exactly 0.01.
    float  linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    if (depth <= 0.01)
    {
        linearizedDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    // Native flips then scales, as two separate steps, rather than folding the
    // flip into the scale.
    float2 uvFlipped = float2(uv4.z, 1.0 - uv4.w);
    float2 uvNDC = uvFlipped * 2.0 - 1.0;
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

#ifdef POINTOMNI
    // Punctual setup. The attenuation is evaluated before anything is sampled,
    // and a fully attenuated pixel returns two zeroed targets - native emits
    // this early-out ahead of every g-buffer fetch.
    float3 toLight   = LightVector.xyz - posView;
    float  distSq    = dot(toLight, toLight);
    float  distNorm  = saturate(sqrt(distSq) / LightVector.w);
    float  falloff   = exp2(log2(distNorm) * LightAttenuation.z);
    float  attenBase = saturate(LightAttenuation.y * falloff + LightAttenuation.x);
    float  attenuation = exp2(log2(1.0 - attenBase) * 2.2);

    if (attenuation <= 0.001)
    {
        output.diffuse  = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    float3 lightDir = toLight * rsqrt(distSq);
#else
    float3 lightDir = LightVector.xyz;
#endif

    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

#ifdef AMBIENT
    float3 ambientDiffuse  = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

#ifndef IGNOREROUGHNESS
    float roughness01 = 1.0 - matSample.x;
#endif

    float  posViewLen  = rsqrt(dot(-posView, -posView));
    float3 viewDirNeg  = -posView * posViewLen;

    // N.L is shared or rematerialised to match what crosses the branch. Under
    // SPECULAR both branches multiply their specular lobe by the clamped
    // value, so it is one live value computed once. Without SPECULAR nothing
    // N.L-derived crosses the branch, and native evaluates the dot product
    // separately in each scope that needs it. That is the whole difference
    // between one cb2[1] read and three on the bare directional permutation.
#ifdef SPECULAR
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_pos     = max(NdotL_raw, 0.0);
    float NdotL_clamped = min(NdotL_pos, 1.0);
#endif

#ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
    float oneMinusGloss  = 1.0 - saturate(cb12_idx30.y);
    float oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float schlickFres    = 1.0 - oneMinusGloss * oneMinusGloss4;
#endif

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    // Material-id-branched BRDF.
    float3 brdfSpecular  = float3(0, 0, 0);
    float  brdfShadowMix = 0.0;
#ifdef DIRECTIONAL
    float  brdfModulator = 0.0;
    float4 albedoSample  = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
#endif

    if (isMaterial1)
    {
        // Two-lobe anisotropic hair specular driven by the engine's
        // fHairPrimSpec* / fHairSecSpec* values. It dots the t2 sample's xyz,
        // a distinct normal in that g-buffer slot, not the octahedral normal.
        // Without SPECULAR only the first lobe exists, and it feeds the
        // diffuse mix alone.
#ifdef POINTOMNI
        float albedoAlpha = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
#else
        float albedoAlpha = albedoSample.w;
#endif
        float skinNdotL  = dot(matSample.xyz, lightDir);
        float skinNdotV  = dot(matSample.xyz, viewDirNeg);
        float sinScaleL  = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV  = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));
#ifndef SPECULAR
        float NdotL_pos  = max(dot(normalView, lightDir), 0.0);
#endif

        float sinA1, cosA1;
        sincos(cb12_idx29_hair_spec_shifts.y, sinA1, cosA1);
        float rot1     = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1     = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1     = exp2(log2(vis1) * cb12_idx28_hair_spec_params.w);
        float hairIntensity =
            saturate(cb12_idx28_hair_spec_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoAlpha, hairIntensity);

#ifdef SPECULAR
        float sinA2, cosA2;
        sincos(cb12_idx29_hair_spec_shifts.x, sinA2, cosA2);
        float rot2     = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2     = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2     = exp2(log2(vis2) * cb12_idx28_hair_spec_params.y)
            * cb12_idx28_hair_spec_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
#endif
    }
    else
    {
        float NdotV_raw = dot(viewDirNeg, normalView);
#ifndef SPECULAR
        float NdotL_raw = dot(lightDir, normalView);
        float NdotL_pos = max(NdotL_raw, 0.0);
#endif

#ifdef AMBIENT
        float3 reflectionDir = 2.0 * NdotV_raw * normalView - viewDirNeg;
        float  oneMinusNdotV = 1.0 - saturate(NdotV_raw);
        float  ambientSpecularFactor =
            exp2(log2(oneMinusNdotV) * (3.0 - matSample.x)) * 0.25;
        ambientSpecular = matSample.y * ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir);
#endif

#ifdef IGNOREROUGHNESS
        // The roughness-driven visibility geometry below is the whole of what
        // IGNOREROUGHNESS removes from this branch; the diffuse mix collapses
        // to the unmodified clamped N.L.
        brdfShadowMix = NdotL_pos;
#else
        float3 tangentV  = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL  = lightDir - normalView * NdotL_raw;
        float  tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA    = roughSq / (roughSq + 0.57);
        float visB    = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin   = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                           * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix  = NdotL_pos * visibilityGeom;
#endif

#ifdef SPECULAR
        float specExpBase   = exp2(matSample.x * 10.0 + 1.0);
#ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
        float specExpScale = 1.0 - schlickFres * 0.98;
        float specExp      = specExpScale * specExpBase;
        float distributionNorm = (specExpBase * specExpScale + 2.0) * 0.159155;
#else
        // POINTOMNI never reads the gloss term, so its exponent is the raw
        // material value and its normalisation has no scale factor.
        float specExp = specExpBase;
        float distributionNorm = (specExpBase + 2.0) * 0.159155;
#endif

        float3 halfVec = lightDir - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_raw);
        float VdotH     = saturate(dot(viewDirNeg, halfVec));
        float NdotH     = saturate(dot(halfVec, normalView));

        float distribution = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, 0.0);
        float minN         = min(NdotL_clamped, NdotV_sat);
        float twoNdotH     = NdotH + NdotH;
        bool  usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool  useUnityRatio = (NdotV_sat == minN);
        float ratioNLNV    = NdotL_clamped / NdotV_sat;
        float ratio        = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility   = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH  = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm    = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= FO4_SPECULAR_SCALE;

        brdfSpecular = NdotL_clamped * (specMag * LightColor_HDR.xyz);
#endif

#ifdef DIRECTIONAL
        brdfModulator = matSample.z * 100.0;
#endif
    }

    // Final composition.
#ifndef SPECULAR
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_clamped = saturate(NdotL_raw);
#endif

    float3 finalDiffuse = LightColor_HDR.xyz * brdfShadowMix;

#if !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
    // Rim / backscatter tail, scaled by the smoothness. IGNOREROUGHNESS
    // deletes that smoothness, and IGNORERIM deletes the term directly; in the
    // archive either macro removes exactly this block.
    float NdotV_view  = saturate(dot(normalView, viewDirNeg));
    float ambientFres = exp2(log2(1.0 - NdotV_view) * 0.01);
    float fresEdge    = saturate(dot(viewDirNeg, -lightDir));
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;

    finalDiffuse += LightColor_HDR.xyz * ambientTerm;
#endif

#ifdef DIRECTIONAL
    float3 albedoPremult = albedoSample.w * albedoSample.xyz;

    float backfaceWrap = saturate(-NdotL_raw);
    finalDiffuse += LightColor_HDR.xyz * (albedoPremult * backfaceWrap);

    float forwardBlend = saturate((brdfModulator + NdotL_raw) / (brdfModulator + 1.0));
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * LightColor_HDR.xyz) * albedoSample.xyz;
#endif

#ifdef POINTOMNI
    finalDiffuse *= attenuation;
#endif

#ifdef SPECULAR
#  ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
    output.specular.xyz = (1.0 - schlickFres * 0.5) * brdfSpecular;
#  else
    output.specular.xyz = attenuation * brdfSpecular;
#  endif
#else
    output.specular.xyz = float3(0, 0, 0);
#endif
#ifdef AMBIENT
    output.specular.xyz += ambientSpecular;
#endif
    output.specular.w = 1.0;

    output.diffuse.xyz = finalDiffuse;
#ifdef AMBIENT
    output.diffuse.xyz += ambientDiffuse;
#endif
    output.diffuse.xyz /= 3.0;
    output.diffuse.w = 0.0;

    return output;
}

// IGNOREROUGHNESS and IGNORERIM are held to exact constant read-counts here,
// not exempted. Numerical proof later found one texture-derived AMBIENT path
// that those pins cannot see.
//
// This layer supplies the controlled no-AMBIENT pair that localises the
// cb2[1]/cb2[2] read-count deltas. Producer oracle evidence later completed the
// shadowed DIRSPLITS=2 reconstruction, including its distinct AMBIENT path, so
// both layers now enforce exact read counts:
//
//   IGNOREROUGHNESS, no AMBIENT    039c8935 196 -> 28858d7b 166 instrs
//   IGNOREROUGHNESS, with AMBIENT  477c3e1e 223 -> 987c4e79 194 instrs
//   IGNORERIM, no SPECULAR         12d92cd3 142 -> b4337a89 130 instrs
//   IGNORERIM, with SPECULAR       9f44ba67 196 -> fcabd749 187 instrs
//
// Native 987c4e79 replaces the roughness-dependent ambient exponent retained by
// 477c3e1e with the fixed square, in addition to removing the roughness-driven
// visibility geometry and rim term. This source still retains that exponent
// under IGNOREROUGHNESS, so the 987c4e79 semantic path remains for a later wave.
// The deleted `tangentL` and `fresEdge` account for the two lost cb2[1] reads,
// and the deleted rim product for the one lost cb2[2] read. IGNORERIM removes
// the rim term only, costing one cb2[2] read and no cb2[1].
//
// Both macro axes remain explicit, not #undef'd or mapped onto another axis,
// and neither manifest declares a count-exemption axis.
//
// What IGNOREROUGHNESS does NOT remove is pinned just as deliberately, because
// "it drops a whole lobe" is the obvious wrong hypothesis. Measured across both
// pairs, the material-code-1 hair specular path is untouched: cb12[28]
// HairSpecParams holds at 4 reads, cb12[29] HairSpecShift at 2, cb12[30] at 1,
// and both Kajiya-Kay shifted-tangent sincos lobes survive in every member of
// both pairs. The ambient gradient is untouched too - cb2[6..8] hold at 2 reads
// each across the AMBIENT pair. Only cb2[1] (5 -> 3) and cb2[2] (6 -> 5) move.
// The fixed-square substitution is texture-derived, so the read-count gate
// cannot distinguish it. Native removes one log and two exps across the ambient
// exponent and rim, plus one sqrt, while only those two constant registers move.
