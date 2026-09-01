// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#if LANDSCAPE && LAND_LOD_BLEND
// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Exact AE 1.11.221 BSDFPrePassShader LANDSCAPE + LAND_LOD_BLEND pixel family.
//
// Native routes:
//   PS2858 0x02000023 and PS2859 0x0200003B -> non-instanced body 1dcf7cfc...
//   PS2680 0x0A000023                       -> instanced body e3c03ae9...
//
// The fixed companion axes are VC + TEXTURE + MOTION_VECTORS. INSTANCED is the
// only source variant. Every other PrePass axis is outside this source family.

#if !defined(LANDSCAPE) || !LANDSCAPE
#  error "this source is the native LANDSCAPE family"
#endif
#if !defined(LAND_LOD_BLEND) || !LAND_LOD_BLEND
#  error "this source is the native LAND_LOD_BLEND family"
#endif
#if !defined(TEXTURE) || !TEXTURE || !defined(VC) || !VC
#  error "the native LAND_LOD_BLEND routes require TEXTURE and VC"
#endif
#ifndef INSTANCED
#  define INSTANCED 0
#endif
#if INSTANCED != 0 && INSTANCED != 1
#  error "INSTANCED must be 0 or 1"
#endif
#if ALPHA_TEST || BLEND || BONE_TINTING || COMBINED || EARLYDEPTH || EYE || FACE
#  error "mixed PrePass family axes"
#endif
#if GLOWMAP || GRADIENT_REMAP || HAIR || LOD_LANDSCAPE || MENU_SCREEN
#  error "mixed PrePass family axes"
#endif
#if MODELSPACENORMALS || PIPBOY_SCREEN || SKEW_SPECULAR_ALPHA || SKIN_TINT
#  error "mixed PrePass family axes"
#endif
#if TESSELLATE_DISP_HEIGHT || TREE_ANIM || ADDITIONAL_ALPHA_MASK
#  error "mixed PrePass family axes"
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    // These indices are observed on the assigned draws only.
    float4 cb12_pad_0_29[30];
    float4 cb12_idx30_global_fade;

    float4 PrevFrame_WorldToClip_row0;
    float4 PrevFrame_WorldToClip_row1;
    float4 PrevFrame_WorldToClip_row2;
    float4 PrevFrame_WorldToClip_row3;

    float4 cb12_pad_35_36[2];

    float4 CurrFrame_WorldToClip_row0;
    float4 CurrFrame_WorldToClip_row1;
    float4 CurrFrame_WorldToClip_row2;
    float4 CurrFrame_WorldToClip_row3;
};

cbuffer PerLandLod_CB0 : register(b0)
{
    float4 cb0_land_lod_params;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2_scroll_anchor_and_alpha;
    float4 cb2_specular_tint;
    float4 cb2_scroll_delta;
    float4 cb2_land_material_gate;
    float4 cb2_land_flags0;
    float4 cb2_land_flags1;
    float4 cb2_land_flags2;
    float4 cb2_land_flags3;
    float4 cb2_material_id_and_smoothness;
};

#if INSTANCED
struct LandInstanceRecord
{
    float4 pad_0_3;
    float2 pad_4_5;
    int lodSlice;
    int layerSlices[12];
};

StructuredBuffer<LandInstanceRecord> g_bLandInstances : register(t4);

Texture2DArray<float4> g_tLandAlbedo : register(t0);
Texture2DArray<float4> g_tLandNormal : register(t1);
Texture2DArray<float4> g_tLandMaterial : register(t2);
SamplerState g_sLandAlbedo : register(s0);
SamplerState g_sLandNormal : register(s1);
SamplerState g_sLandMaterial : register(s2);
#else
Texture2D<float4> g_tLandAlbedo[4] : register(t0);
Texture2D<float4> g_tLandNormal[4] : register(t4);
Texture2D<float4> g_tLandMaterial[4] : register(t8);
SamplerState g_sLandAlbedo[4] : register(s0);
SamplerState g_sLandNormal[4] : register(s4);
SamplerState g_sLandMaterial[4] : register(s8);
#endif

#if INSTANCED
Texture2DArray<float4> g_tLandLodAlbedo : register(t3);
SamplerState g_sLandLodAlbedo : register(s3);
#else
Texture2D<float4> g_tLandLodAlbedo : register(t14);
SamplerState g_sLandLodAlbedo : register(s14);
#endif

Texture2D<float4> g_tLandColorNoise : register(t13);
SamplerState g_sLandColorNoise : register(s13);
Texture2D<float4> g_tLandNormalNoise : register(t15);
SamplerState g_sLandNormalNoise : register(s15);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 curr_pos_u : TEXCOORD3;
    float4 prev_pos_v : TEXCOORD4;
    float4 vertexColor : COLOR0;
#if INSTANCED
    nointerpolation uint instanceIndex : COLOR2;
#endif
    float2 lodAlbedoUV : TEXCOORD6;
    float4 layerWeights : TEXCOORD7;
    float3 lodBlend : TEXCOORD8;
    uint isFrontFace : SV_IsFrontFace;
};

struct PS_OUTPUT
{
    float4 albedo : SV_Target0;
    float2 normalOct : SV_Target1;
    float4 material : SV_Target2;
    float4 auxA : SV_Target3;
    float3 specTint : SV_Target4;
    float2 motionVec : SV_Target5;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    float2 uv = float2(input.curr_pos_u.w, input.prev_pos_v.w);
#if INSTANCED
    int landSlices[12] = g_bLandInstances[input.instanceIndex].layerSlices;
#endif
    bool4 landActive = (input.layerWeights > 0.0);
    float3 landAlbedo = 0.0;
    float2 landMaterial = 0.0;
    float3 landNormal = 0.0;
    [unroll]
    for (int landLayer = 0; landLayer < 4; ++landLayer)
    {
        if (landActive[landLayer])
        {
            float landWeight = input.layerWeights[landLayer];
#if INSTANCED
            float3 landAlbedoUV = float3(uv, landSlices[landLayer * 3]);
            landAlbedo += landWeight * g_tLandAlbedo.Sample(
                g_sLandAlbedo, landAlbedoUV).xyz;
            float3 landNormalUV = float3(uv, landSlices[landLayer * 3 + 1]);
            float2 landNormalXY = g_tLandNormal.Sample(
                g_sLandNormal, landNormalUV).xy;
#else
            landAlbedo += landWeight * g_tLandAlbedo[landLayer].Sample(
                g_sLandAlbedo[landLayer], uv).xyz;
            float2 landNormalXY = g_tLandNormal[landLayer].Sample(
                g_sLandNormal[landLayer], uv).xy;
#endif
            landNormalXY = landNormalXY * 2.0 - 1.0;
            float landNormalZ = sqrt(
                1.0 - min(dot(landNormalXY, landNormalXY), 1.0));
            landNormal += landWeight * float3(landNormalXY, landNormalZ);
#if INSTANCED
            float3 landMaterialUV = float3(uv, landSlices[landLayer * 3 + 2]);
            landMaterial += landWeight * g_tLandMaterial.Sample(
                g_sLandMaterial, landMaterialUV).xy;
#else
            landMaterial += landWeight * g_tLandMaterial[landLayer].Sample(
                g_sLandMaterial[landLayer], uv).xy;
#endif
        }
    }

#if INSTANCED
    float3 landLodAlbedo = g_tLandLodAlbedo.Sample(
        g_sLandLodAlbedo,
        float3(input.lodAlbedoUV,
               g_bLandInstances[input.instanceIndex].lodSlice)).xyz;
#else
    float3 landLodAlbedo = g_tLandLodAlbedo.Sample(
        g_sLandLodAlbedo, input.lodAlbedoUV).xyz;
#endif
    float4 landNoiseUV = (input.lodBlend.xyxy + cb0_land_lod_params.zwzw)
                       * float4(0.000250, 0.000250, 0.000350, 0.000350);
    float3 landColorNoise = g_tLandColorNoise.Sample(
        g_sLandColorNoise, landNoiseUV.xy).xyz;
    landColorNoise = landColorNoise * (34.0 / 9.0) - 2.006;

    float3 landLodTangentRaw = cross(
        landNormal, cross(landNormal, float3(1.0, 0.0, 0.0)));
    float3 landLodBitangentRaw = cross(landNormal, landLodTangentRaw);
    float3 landLodTangent = normalize(landLodTangentRaw);
    float3 landLodBitangent = normalize(landLodBitangentRaw);
    float3 landLodAxis = normalize(landNormal);
    float2 landNoiseXY = g_tLandNormalNoise.Sample(
        g_sLandNormalNoise, landNoiseUV.zw).xy;
    landNoiseXY = landNoiseXY * 2.0 - 1.0;
    float landNoiseZ = sqrt(
        1.0 - min(dot(landNoiseXY, landNoiseXY), 1.0));
    float3 landNoiseVector = float3(landNoiseXY, landNoiseZ);
    float3 landLodNormal;
    landLodNormal.x = dot(landLodTangent, landNoiseVector);
    landLodNormal.y = dot(landLodBitangent, landNoiseVector);
    landLodNormal.z = dot(landLodAxis, landNoiseVector);

    landAlbedo = lerp(
        landAlbedo, landLodAlbedo * landColorNoise, input.lodBlend.z);
    landNormal = normalize(
        lerp(landNormal, landLodNormal, input.lodBlend.z));
    landMaterial = lerp(
        landMaterial, cb0_land_lod_params.yx, input.lodBlend.z);

    float auxRoughness = landMaterial.y;
    landAlbedo *= input.vertexColor.xyz;

    float3 tNorm = normalize(input.tangent);
    float3 bNorm = normalize(input.bitangent);
    float3 nGeom = normalize(input.normal);
    float3 nts = float3(landNormal.xy,
                        (input.isFrontFace != 0) ? landNormal.z : -landNormal.z);
    float axisX = dot(tNorm, nts);
    float axisY = dot(bNorm, nts);
    float axisZ = dot(nGeom, nts);

    float4 landFlags = max(cb2_land_flags0, 0.0) * input.layerWeights.x
                     + max(cb2_land_flags1, 0.0) * input.layerWeights.y;
    landFlags += max(cb2_land_flags2, 0.0) * input.layerWeights.z;
    landFlags += max(cb2_land_flags3, 0.0) * input.layerWeights.w;
    float fadeScale = cb12_idx30_global_fade.x * landFlags.w;

    float2 anchor = cb2_scroll_anchor_and_alpha.xy;
    float2 target = cb2_scroll_delta.xy;
    bool2 svScrolls = (target >= 0.0);
    float2 delta = target - anchor;
    float2 svRaw = cb12_idx30_global_fade.xx * delta + anchor;
    float2 sval = svScrolls ? (svRaw * anchor) : anchor;

    float landZ = saturate(landFlags.z);
    float matZxFade = landZ * cb12_idx30_global_fade.x;
    float2 landAux = float2(landFlags.y, landFlags.x);
    bool2 landAuxPinned = (landAux == -1.0);
    float2 landAuxGate = landAuxPinned ? 0.0 : cb12_idx30_global_fade.xx;
    float matZxComp = mad(
        -cb12_idx30_global_fade.x,
        landZ,
        1.0);
    float lerpedMatX = landMaterial.x * matZxComp + matZxFade;
    float2 landSval = lerp(sval, landAux, landAuxGate);

    output.auxA.x = auxRoughness * landSval.x;
    output.auxA.y = lerpedMatX * landSval.y;

    float3 nWorldFromTBN = normalize(float3(axisX, axisY, min(axisZ, 0.0)));
    float octZ = sqrt(nWorldFromTBN.z * -8.0 + 8.0);
    output.normalOct.xy = nWorldFromTBN.xy / octZ.xx + 0.5;
    output.auxA.z = cb2_scroll_anchor_and_alpha.w * 0.01;

    float matGate = (cb2_material_id_and_smoothness.w < 0.0)
                  ? 0.0
                  : cb12_idx30_global_fade.x;
    bool matUseSpan = cb2_material_id_and_smoothness.y;
    float matSpan = cb2_material_id_and_smoothness.w
                  - cb2_material_id_and_smoothness.z;
    float matVal = matUseSpan
                 ? (matGate * matSpan + cb2_material_id_and_smoothness.z)
                 : (matGate * cb2_material_id_and_smoothness.w);
    output.material.z = sqrt(matVal * 0.02);

    output.material.x = (cb12_idx30_global_fade.x != 0.0)
                      ? cb2_land_material_gate.x
                      : 0.0;
    output.material.y = cb2_material_id_and_smoothness.x * (1.0 / 255.0);
    output.material.w = saturate(cb2_material_id_and_smoothness.x);

    output.specTint.xyz = cb2_specular_tint.xyz;
    output.albedo.w = cb2_scroll_anchor_and_alpha.z;

    bool landFadeApplies = (landFlags.w != -1.0);
    float alphaFade = saturate(1.0 - fadeScale);
    output.albedo.xyz = landFadeApplies ? (landAlbedo * alphaFade) : landAlbedo;
    output.auxA.w = 1.0;

    float4 currWorld = float4(input.curr_pos_u.xyz, 1.0);
    float currClipX = dot(CurrFrame_WorldToClip_row0, currWorld);
    float currClipY = dot(CurrFrame_WorldToClip_row1, currWorld);
    float currClipW = dot(CurrFrame_WorldToClip_row3, currWorld);
    float2 currNDC = float2(currClipX, currClipY) / currClipW.xx;

    float4 prevWorld = float4(input.prev_pos_v.xyz, 1.0);
    float prevClipX = dot(PrevFrame_WorldToClip_row0, prevWorld);
    float prevClipY = dot(PrevFrame_WorldToClip_row1, prevWorld);
    float prevClipW = dot(PrevFrame_WorldToClip_row3, prevWorld);
    float2 prevNDC = float2(prevClipX, prevClipY) / prevClipW.xx;

    output.motionVec = (currNDC - prevNDC) * float2(-0.5, 0.5);
    return output;
}
#elif LOD_LANDSCAPE
// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Exact AE 1.11.221 BSDFPrePassShader LOD_LANDSCAPE pixel family.
//
// Raw technique bit 0x00000200 emits LOD_LANDSCAPE. The native family has
// 14 bodies / 14 containers / 25 routes. Opaque rows write six MRTs; the two
// BLEND rows write five MRTs and carry blend alpha in o0.w/o1.w/o3.w/o4.w.

#if !defined(ADDITIONAL_ALPHA_MASK) || !defined(ALPHA_TEST) \
    || !defined(BLEND) || !defined(BONE_TINTING) \
    || !defined(EARLYDEPTH) || !defined(EYE) || !defined(FACE) \
    || !defined(GLOWMAP) || !defined(GRADIENT_REMAP) \
    || !defined(HAIR) || !defined(INSTANCED) \
    || !defined(LOD_LANDSCAPE) || !defined(MENU_SCREEN) \
    || !defined(MODELSPACENORMALS) \
    || !defined(SKEW_SPECULAR_ALPHA) || !defined(SKIN_TINT) \
    || !defined(TESSELLATE_DISP_HEIGHT) || !defined(TEXTURE) \
    || !defined(VC)
#  error "define the complete BSDFPrePass compile vector"
#endif

#if ADDITIONAL_ALPHA_MASK || EARLYDEPTH || EYE || FACE || GLOWMAP \
    || GRADIENT_REMAP || HAIR || SKEW_SPECULAR_ALPHA || SKIN_TINT \
    || TESSELLATE_DISP_HEIGHT
#  error "unsupported selector in the LOD_LANDSCAPE pixel family"
#endif
#if !LOD_LANDSCAPE || !TEXTURE
#  error "this source requires LOD_LANDSCAPE=1 and TEXTURE=1"
#endif
#if (ALPHA_TEST != 0 && ALPHA_TEST != 1) \
    || (BLEND != 0 && BLEND != 1) \
    || (BONE_TINTING != 0 && BONE_TINTING != 1) \
    || (INSTANCED != 0 && INSTANCED != 1) \
    || (MENU_SCREEN != 0 && MENU_SCREEN != 1) \
    || (MODELSPACENORMALS != 0 && MODELSPACENORMALS != 1) \
    || (VC != 0 && VC != 1)
#  error "LOD_LANDSCAPE selectors must be Boolean"
#endif
#if ALPHA_TEST + BLEND + BONE_TINTING + MODELSPACENORMALS > 1
#  error "the native family admits at most one material selector"
#endif
#if MENU_SCREEN && (ALPHA_TEST || BLEND || BONE_TINTING \
                    || MODELSPACENORMALS)
#  error "MENU_SCREEN has no native material-selector pair"
#endif
#if INSTANCED && (ALPHA_TEST || BLEND || BONE_TINTING \
                  || MODELSPACENORMALS)
#  error "INSTANCED has no native material-selector pair"
#endif
#if MODELSPACENORMALS && VC
#  error "the native MODELSPACENORMALS row has VC=0"
#endif
#if MENU_SCREEN && INSTANCED && !VC
#  error "the native MENU_SCREEN+INSTANCED row has VC=1"
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_29[30];
    float4 cb12_idx30_global_fade;

    float4 PrevFrame_WorldToClip_row0;
    float4 PrevFrame_WorldToClip_row1;
    float4 PrevFrame_WorldToClip_row2;
    float4 PrevFrame_WorldToClip_row3;

    float4 cb12_pad_35_36[2];

    float4 CurrFrame_WorldToClip_row0;
    float4 CurrFrame_WorldToClip_row1;
    float4 CurrFrame_WorldToClip_row2;
    float4 CurrFrame_WorldToClip_row3;
};

#if INSTANCED
struct InstanceRecord
{
    float4 rows[9];
};

cbuffer PerInstance_CB13 : register(b13)
{
    InstanceRecord cb13_instances[455];
};
#endif

#if !BONE_TINTING
cbuffer PerLod_CB0 : register(b0)
{
    float4 cb0_lod_noise_offset;
};
#endif

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2_scroll_anchor_and_alpha;
    float4 cb2_specular_tint;
#if BLEND
    float4 cb2_blend_params;
#endif
    float4 cb2_scroll_delta;
    float4 cb2_pad;
    float4 cb2_material_flags;
#if BONE_TINTING
    float4 cb2_bone_tint_params;
#endif
    float4 cb2_material_id_and_smoothness;
};

Texture2D<float4> g_tAlbedo : register(t0);
Texture2D<float4> g_tNormalMap : register(t1);
Texture2D<float4> g_tMaterial : register(t2);

SamplerState g_sAlbedo : register(s0);
SamplerState g_sNormalMap : register(s1);
SamplerState g_sMaterial : register(s2);

#if BONE_TINTING
Texture2D<float4> g_tBoneIndex : register(t13);
SamplerState g_sBoneIndex : register(s13);
Texture2D<float4> g_tBoneTint : register(t14);
SamplerState g_sBoneTint : register(s14);
#endif

#if MENU_SCREEN
Texture2D<float4> g_tMenuOverlay : register(t4);
SamplerState g_sMenuOverlay : register(s4);
#endif

#if !BONE_TINTING
Texture2D<float4> g_tLodColorNoise : register(t13);
SamplerState g_sLodColorNoise : register(s13);
Texture2D<float4> g_tLodNormalNoise : register(t15);
SamplerState g_sLodNormalNoise : register(s15);
#endif

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 curr_pos_u : TEXCOORD3;
    float4 prev_pos_v : TEXCOORD4;
#if VC
    float4 vertexColor : COLOR0;
#endif
#if BONE_TINTING
    float4 boneTint : COLOR1;
#endif
#if INSTANCED
    nointerpolation uint instanceIndex : COLOR2;
#endif
    float2 lodUV : TEXCOORD9;
    uint isFrontFace : SV_IsFrontFace;
};

struct PS_OUTPUT
{
    float4 albedo : SV_Target0;
#if BLEND
    float4 normalOct : SV_Target1;
#else
    float2 normalOct : SV_Target1;
#endif
    float4 material : SV_Target2;
    float4 auxA : SV_Target3;
#if BLEND
    float4 specTint : SV_Target4;
#else
    float3 specTint : SV_Target4;
#endif
#if !BLEND
    float2 motionVec : SV_Target5;
#endif
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    float2 uv = float2(input.curr_pos_u.w, input.prev_pos_v.w);
#if !BONE_TINTING
    float4 lodNoiseUV = (input.lodUV.xyxy + cb0_lod_noise_offset.zwzw)
                      * float4(0.000250, 0.000250, 0.000350, 0.000350);
    float3 lodColorNoise = g_tLodColorNoise.Sample(
        g_sLodColorNoise, lodNoiseUV.xy).xyz;
    float2 lodNormalNoiseXY = g_tLodNormalNoise.Sample(
        g_sLodNormalNoise, lodNoiseUV.zw).xy;
    lodNormalNoiseXY = lodNormalNoiseXY * 2.0 - 1.0;
    lodColorNoise = lodColorNoise * (34.0 / 9.0) - 2.006;
#endif

    float4 albedoSample = g_tAlbedo.Sample(g_sAlbedo, uv);
#if !BONE_TINTING
    albedoSample.xyz *= lodColorNoise;
#endif

    float4 normalMapSample = g_tNormalMap.Sample(g_sNormalMap, uv);
#if MODELSPACENORMALS
    float3 nmRaw = normalMapSample.xyz * 2.0 - 1.0;
#else
    float2 nts_xy = normalMapSample.xy * 2.0 - 1.0;
    float nts_xy_lenSq = saturate(dot(nts_xy, nts_xy));
    float nts_z = sqrt(1.0 - nts_xy_lenSq);
#endif

    float4 materialSample = g_tMaterial.Sample(g_sMaterial, uv);
    float auxRoughness = materialSample.y;

#if VC
#if !BONE_TINTING
    albedoSample.a *= input.vertexColor.a;
    albedoSample.xyz *= input.vertexColor.xyz;
#else
    albedoSample *= input.vertexColor;
#endif
#endif

#if MENU_SCREEN
    float3 menuOverlay = g_tMenuOverlay.Sample(g_sMenuOverlay, uv).xyz;
    albedoSample.xyz += menuOverlay;
#endif

#if ALPHA_TEST
    clip(albedoSample.a - cb2_specular_tint.w);
#endif

    float3 tNorm = normalize(input.tangent);
    float3 bNorm = normalize(input.bitangent);
    float3 nGeom = normalize(input.normal);
#if !BONE_TINTING
#if MODELSPACENORMALS
    float3 lodNormalBase = normalMapSample.xzy * 2.0 - 1.0;
#else
    float3 lodNormalBase = float3(nts_xy, nts_z);
#endif
    float lodNoiseZ = sqrt(
        1.0 - min(dot(lodNormalNoiseXY, lodNormalNoiseXY), 1.0));
    float3 lodNoiseVector = float3(lodNormalNoiseXY, lodNoiseZ);
    float3 lodTangent = cross(
        lodNormalBase, cross(lodNormalBase, float3(1.0, 0.0, 0.0)));
    float3 lodBitangent = cross(lodNormalBase, lodTangent);
    float3 nts;
    nts.y = dot(normalize(lodBitangent), lodNoiseVector);
    nts.x = dot(normalize(lodTangent), lodNoiseVector);
    float lodSignedZ = dot(normalize(lodNormalBase), lodNoiseVector);
    nts.z = (input.isFrontFace != 0) ? lodSignedZ : -lodSignedZ;
#else
    float3 nts = float3(
        nts_xy, (input.isFrontFace != 0) ? nts_z : -nts_z);
#endif

    float axisX = dot(tNorm, nts);
    float axisY = dot(bNorm, nts);
    float axisZ = dot(nGeom, nts);
    float fadeScale = cb2_material_flags.w * cb12_idx30_global_fade.x;

#if INSTANCED
    InstanceRecord instanceRecord = cb13_instances[input.instanceIndex];
#else
    float2 anchor = cb2_scroll_anchor_and_alpha.xy;
    float2 target = cb2_scroll_delta.xy;
    bool2 svScrolls = (target >= 0.0);
    float2 delta = target - anchor;
    float2 svRaw = cb12_idx30_global_fade.xx * delta + anchor;
    float2 sval = svScrolls ? (svRaw * anchor) : anchor;
#endif

    float matZxFade = cb12_idx30_global_fade.x
                    * cb2_material_flags.z;
    float matZxComp = mad(
        -cb2_material_flags.z,
        cb12_idx30_global_fade.x,
        1.0);
    float lerpedMatX = materialSample.x * matZxComp + matZxFade;

#if INSTANCED
    output.auxA.x = auxRoughness * instanceRecord.rows[7].x;
    output.auxA.y = lerpedMatX * instanceRecord.rows[7].y;
#else
    output.auxA.x = auxRoughness * sval.x;
    output.auxA.y = lerpedMatX * sval.y;
#endif

    float3 nWorldFromTBN = normalize(float3(axisX, axisY, min(axisZ, 0.0)));
#if BLEND
    output.normalOct.z = -nWorldFromTBN.z;
#endif
    float octZ = sqrt(nWorldFromTBN.z * -8.0 + 8.0);
    output.normalOct.xy = nWorldFromTBN.xy / octZ.xx + 0.5;

#if INSTANCED
    output.auxA.z = 0.01 * instanceRecord.rows[7].w;
#else
    output.auxA.z = cb2_scroll_anchor_and_alpha.w * 0.01;
#endif

    float matGate = (cb2_material_id_and_smoothness.w < 0.0)
                  ? 0.0
                  : cb12_idx30_global_fade.x;
    bool matUseSpan = cb2_material_id_and_smoothness.y;
    float matSpan = cb2_material_id_and_smoothness.w
                  - cb2_material_id_and_smoothness.z;
    float matVal = matUseSpan
                 ? (matGate * matSpan + cb2_material_id_and_smoothness.z)
                 : (matGate * cb2_material_id_and_smoothness.w);
    output.material.z = sqrt(matVal * 0.02);

    bool bitB = cb2_material_flags.x;
    bool bitA = cb2_material_flags.y && cb12_idx30_global_fade.x;
    output.material.x = (bitB || bitA) ? 1.0 : 0.0;
    output.material.y =
        cb2_material_id_and_smoothness.x * (1.0 / 255.0);
    output.material.w = saturate(cb2_material_id_and_smoothness.x);

#if INSTANCED
    output.specTint.xyz = instanceRecord.rows[8].xyz;
#else
    output.specTint.xyz = cb2_specular_tint.xyz;
#endif

#if BLEND
    float blendAlpha = (cb2_blend_params.y == 1.0) ? albedoSample.a : 1.0;
    clip(cb2_blend_params.x * blendAlpha - (4.0 / 255.0));
    blendAlpha *= cb2_blend_params.x;
    output.albedo.w = blendAlpha;
    output.normalOct.w = blendAlpha;
    output.auxA.w = blendAlpha;
    output.specTint.w = blendAlpha;
#else
#if INSTANCED
    output.albedo.w = instanceRecord.rows[7].z;
#else
    output.albedo.w = cb2_scroll_anchor_and_alpha.z;
#endif
#endif

    bool bypassFade = (cb2_material_flags.w == -1.0);
    float alphaFade = bypassFade ? 1.0 : (1.0 - fadeScale);

#if BONE_TINTING
    float4 boneIndex = g_tBoneIndex.Sample(g_sBoneIndex, uv);
    float2 boneUv = float2(boneIndex.y, frac(cb2_bone_tint_params.x));
    float4 boneTint = g_tBoneTint.Sample(g_sBoneTint, boneUv);
    float3 boneContribution = boneTint.xyz * boneTint.w
                            * boneIndex.w * input.boneTint.w * 4.0;
    output.albedo.xyz = albedoSample.xyz * alphaFade + boneContribution;
#else
    output.albedo.xyz = albedoSample.xyz * alphaFade;
#endif

#if !BLEND
    output.auxA.w = 1.0;

    float4 currWorld = float4(input.curr_pos_u.xyz, 1.0);
    float currClipX = dot(CurrFrame_WorldToClip_row0, currWorld);
    float currClipY = dot(CurrFrame_WorldToClip_row1, currWorld);
    float currClipW = dot(CurrFrame_WorldToClip_row3, currWorld);
    float2 currNDC = float2(currClipX, currClipY) / currClipW.xx;

    float4 prevWorld = float4(input.prev_pos_v.xyz, 1.0);
    float prevClipX = dot(PrevFrame_WorldToClip_row0, prevWorld);
    float prevClipY = dot(PrevFrame_WorldToClip_row1, prevWorld);
    float prevClipW = dot(PrevFrame_WorldToClip_row3, prevWorld);
    float2 prevNDC = float2(prevClipX, prevClipY) / prevClipW.xx;

    output.motionVec = (currNDC - prevNDC) * float2(-0.5, 0.5);
#endif
    return output;
}
#else
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_29[30];
    float4 cb12_idx30_global_fade;

    float4 PrevFrame_WorldToClip_row0;
    float4 PrevFrame_WorldToClip_row1;
    float4 PrevFrame_WorldToClip_row2;
    float4 PrevFrame_WorldToClip_row3;

    float4 CameraPosAdjust;
    float4 CameraPreviousPosAdjust;

    float4 CurrFrame_WorldToClip_row0;
    float4 CurrFrame_WorldToClip_row1;
    float4 CurrFrame_WorldToClip_row2;
    float4 CurrFrame_WorldToClip_row3;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2_idx0_scroll_anchor_and_alpha;
    float4 cb2_idx1_specular_tint;
    float4 cb2_idx2_scroll_delta;
    float4 cb2_pad_3;
    float4 cb2_idx4_material_flags;
    float4 cb2_idx5_material_id_and_smoothness;
};

Texture2D<float4> g_tAlbedo : register(t0);
Texture2D<float4> g_tNormalMap : register(t1);
Texture2D<float4> g_tMaterial : register(t2);

SamplerState g_sAlbedo : register(s0);
SamplerState g_sNormalMap : register(s1);
SamplerState g_sMaterial : register(s2);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 curr_pos_u : TEXCOORD3;
    float4 prev_pos_v : TEXCOORD4;
    uint isFrontFace : SV_IsFrontFace;
};

struct PS_OUTPUT
{
    float4 albedo : SV_Target0;
    float2 normalOct : SV_Target1;
    float4 material : SV_Target2;
    float4 auxA : SV_Target3;
    float3 specTint : SV_Target4;
    float2 motionVec : SV_Target5;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    output.albedo.w = cb2_idx0_scroll_anchor_and_alpha.z;

    bool bypassFade = (cb2_idx4_material_flags.w == -1.0);
    float alphaFade = bypassFade
        ? 1.0
        : (1.0 - cb2_idx4_material_flags.w * cb12_idx30_global_fade.x);

    float2 uv = float2(input.curr_pos_u.w, input.prev_pos_v.w);
    float4 albedoSample = g_tAlbedo.Sample(g_sAlbedo, uv);
    output.albedo.xyz = albedoSample.xyz * alphaFade;

    float3 nGeom = normalize(input.normal);
    float4 normalMapSample = g_tNormalMap.Sample(g_sNormalMap, uv);
    float4 materialSample = g_tMaterial.Sample(g_sMaterial, uv);

    float2 nts_xy = normalMapSample.xy * 2.0 - 1.0;
    float nts_xy_lenSq = saturate(dot(nts_xy, nts_xy));
    float nts_z = sqrt(1.0 - nts_xy_lenSq);
    nts_z = (input.isFrontFace != 0) ? nts_z : -nts_z;
    float3 nts = float3(nts_xy, nts_z);

    float3 tNorm = normalize(input.tangent);
    float3 bNorm = normalize(input.bitangent);
    float axisX = dot(tNorm, nts);
    float axisY = dot(bNorm, nts);
    float axisZ = min(dot(nGeom, nts), 0.0);
    float3 nWorldFromTBN = normalize(float3(axisX, axisY, axisZ));

    float octZ = sqrt(nWorldFromTBN.z * -8.0 + 8.0);
    output.normalOct = nWorldFromTBN.xy / octZ.xx + 0.5;

    float matSpan = cb2_idx5_material_id_and_smoothness.w
                  - cb2_idx5_material_id_and_smoothness.z;
    float matGate = (cb2_idx5_material_id_and_smoothness.w < 0.0)
                  ? 0.0
                  : cb12_idx30_global_fade.x;
    float matVal = cb2_idx5_material_id_and_smoothness.y
                 ? (matGate * matSpan + cb2_idx5_material_id_and_smoothness.z)
                 : (matGate * cb2_idx5_material_id_and_smoothness.w);
    output.material.z = sqrt(matVal * 0.02);

    bool bitB = cb2_idx4_material_flags.x;
    bool bitA = cb2_idx4_material_flags.y && cb12_idx30_global_fade.x;
    output.material.x = (bitB || bitA) ? 1.0 : 0.0;

    output.material.y = cb2_idx5_material_id_and_smoothness.x * (1.0 / 255.0);
    output.material.w = saturate(cb2_idx5_material_id_and_smoothness.x);

    float matZxFade = cb12_idx30_global_fade.x
                    * cb2_idx4_material_flags.z;
    float matZxComp = mad(
        -cb2_idx4_material_flags.z,
        cb12_idx30_global_fade.x,
        1.0);
    float lerpedMatX = materialSample.x * matZxComp + matZxFade;

    float2 anchor = cb2_idx0_scroll_anchor_and_alpha.xy;
    float2 target = cb2_idx2_scroll_delta.xy;
    float2 delta = target - anchor;
    float2 svRaw = cb12_idx30_global_fade.xx * delta + anchor;
    float2 sval = svRaw * anchor;
    sval = (target >= 0.0) ? sval : anchor;

    output.auxA.x = materialSample.y * sval.x;
    output.auxA.y = lerpedMatX * sval.y;
    output.auxA.z = cb2_idx0_scroll_anchor_and_alpha.w * 0.01;
    output.auxA.w = 1.0;

    output.specTint = cb2_idx1_specular_tint.xyz;

    float4 currWorld = float4(input.curr_pos_u.xyz, 1.0);
    float currClipX = dot(CurrFrame_WorldToClip_row0, currWorld);
    float currClipY = dot(CurrFrame_WorldToClip_row1, currWorld);
    float currClipW = dot(CurrFrame_WorldToClip_row3, currWorld);
    float2 currNDC = float2(currClipX, currClipY) / currClipW.xx;

    float4 prevWorld = float4(input.prev_pos_v.xyz, 1.0);
    float prevClipX = dot(PrevFrame_WorldToClip_row0, prevWorld);
    float prevClipY = dot(PrevFrame_WorldToClip_row1, prevWorld);
    float prevClipW = dot(PrevFrame_WorldToClip_row3, prevWorld);
    float2 prevNDC = float2(prevClipX, prevClipY) / prevClipW.xx;

    output.motionVec = (currNDC - prevNDC) * float2(-0.5, 0.5);
    return output;
}
#endif
