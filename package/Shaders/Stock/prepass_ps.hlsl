// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Exact AE 1.11.221 PrePass admitted pixel-stage permutations.

#ifndef ALPHA_TEST
#define ALPHA_TEST 0
#endif

#ifndef MODELSPACENORMALS
#define MODELSPACENORMALS 0
#endif

#ifndef EARLYDEPTH
#define EARLYDEPTH 0
#endif

#ifndef BLEND
#define BLEND 0
#endif

#ifndef SKEW_SPECULAR_ALPHA
#define SKEW_SPECULAR_ALPHA 0
#endif

#ifndef VC
#define VC 0
#endif

#ifndef GLOWMAP
#define GLOWMAP 0
#endif

#ifndef GRADIENT_REMAP
#define GRADIENT_REMAP 0
#endif

#ifndef ADDITIONAL_ALPHA_MASK
#define ADDITIONAL_ALPHA_MASK 0
#endif

#ifndef SKIN_TINT
#define SKIN_TINT 0
#endif

#ifndef BONE_TINTING
#define BONE_TINTING 0
#endif

#ifndef HAIR
#define HAIR 0
#endif

#ifndef EYE
#define EYE 0
#endif

#ifndef FACE
#define FACE 0
#endif

#ifndef TESSELLATE_DISP_HEIGHT
#define TESSELLATE_DISP_HEIGHT 0
#endif

#ifndef LOD_LANDSCAPE
#define LOD_LANDSCAPE 0
#endif

#ifndef MENU_SCREEN
#define MENU_SCREEN 0
#endif

#ifndef INSTANCED
#define INSTANCED 0
#endif

#ifndef TEXTURE
#define TEXTURE 1
#endif

#ifndef TREE_ANIM
#define TREE_ANIM 0
#endif

#ifndef PIPBOY_SCREEN
#define PIPBOY_SCREEN 0
#endif

#ifndef LANDSCAPE
#define LANDSCAPE 0
#endif

#ifndef LAND_LOD_BLEND
#define LAND_LOD_BLEND 0
#endif

#ifndef COMBINED
#define COMBINED 0
#endif

#ifndef DISMEMBERMENT
#define DISMEMBERMENT 0
#endif

#ifndef DISMEMBERMENT_MEATCUFF
#define DISMEMBERMENT_MEATCUFF 0
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

#if INSTANCED && !LANDSCAPE
struct InstanceRecord
{
    float4 rows[9];
};

cbuffer PerInstance_CB13 : register(b13)
{
    InstanceRecord cb13_instances[455];
};
#endif

#if PIPBOY_SCREEN
cbuffer PerPipboy_CB0 : register(b0)
{
    float4 cb0_pipboy_params;
};
#elif LOD_LANDSCAPE && !BONE_TINTING
cbuffer PerLod_CB0 : register(b0)
{
    float4 cb0_lod_noise_offset;
};
#elif LANDSCAPE && LAND_LOD_BLEND
cbuffer PerLandLod_CB0 : register(b0)
{
    float4 cb0_land_lod_params;
};
#endif

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2_scroll_anchor_and_alpha;
    float4 cb2_specular_tint;
#if BLEND
    float4 cb2_blend_params;
#endif
#if SKIN_TINT
    float4 cb2_skin_tint;
#endif
#if GRADIENT_REMAP
    float4 cb2_gradient_params;
#elif HAIR
    float4 cb2_hair_params;
#endif
    float4 cb2_scroll_delta;
#if LANDSCAPE
    float4 cb2_land_material_gate;
#else
    float4 cb2_pad;
#endif
#if LANDSCAPE
    float4 cb2_land_flags0;
    float4 cb2_land_flags1;
    float4 cb2_land_flags2;
    float4 cb2_land_flags3;
#else
    float4 cb2_material_flags;
#endif
#if EYE && DISMEMBERMENT
    float4 cb2_cut_tangent;
    float4 cb2_cut_bitangent;
    float4 cb2_cut_normal;
    float4 cb2_cut_pad;
#elif EYE && DISMEMBERMENT_MEATCUFF
    float4 cb2_cut_tangent;
    float4 cb2_cut_bitangent;
#endif
#if ADDITIONAL_ALPHA_MASK
    float4 cb2_alpha_mask_params;
#endif
#if BONE_TINTING
    float4 cb2_bone_tint_params;
#endif
    float4 cb2_material_id_and_smoothness;
};

#if EYE && COMBINED
// Combined eye draws take per-instance surface state from t4/t6, not PerCall_CB2.
struct EyeInstance
{
    float4 scroll;
    float3 specularTint;
    float4 alphaRefAndFlags;
    float fadeFlag;
    float4 pad_12_15;
    uint3 textureSlices;
    uint pad_19;
    uint gradientSlice;
    float gradientV;
    float smoothness;
};

struct EyeMaterialRecord
{
    float4 pad_0_23[6];
    float materialIndex;
};

StructuredBuffer<EyeInstance> g_bEyeInstances : register(t4);
StructuredBuffer<EyeMaterialRecord> g_bEyeMaterials : register(t6);

Texture2DArray<float4> g_tEyeAlbedo : register(t0);
Texture2DArray<float4> g_tEyeNormalMap : register(t1);
Texture2DArray<float4> g_tEyeMaterial : register(t2);

SamplerState g_sAlbedo : register(s0);
SamplerState g_sNormalMap : register(s1);
SamplerState g_sMaterial : register(s2);
#elif TEXTURE && !LANDSCAPE
Texture2D<float4> g_tAlbedo : register(t0);
Texture2D<float4> g_tNormalMap : register(t1);
#if HAIR && !BLEND
Texture2D<float4> g_tHairMaterial : register(t3);
#else
Texture2D<float4> g_tMaterial : register(t2);
#endif

SamplerState g_sAlbedo : register(s0);
SamplerState g_sNormalMap : register(s1);
#if HAIR && !BLEND
SamplerState g_sHairMaterial : register(s3);
#else
SamplerState g_sMaterial : register(s2);
#endif
#endif
#if GLOWMAP && (!HAIR || BLEND)
Texture2D<float4> g_tGlowMap : register(t3);
SamplerState g_sGlowMap : register(s3);
#endif
#if GRADIENT_REMAP && EYE && COMBINED
Texture2DArray<float4> g_tEyeGradient : register(t5);
SamplerState g_sGradient : register(s5);
#elif GRADIENT_REMAP
Texture2D<float4> g_tGradient : register(t5);
SamplerState g_sGradient : register(s5);
#endif
#if ADDITIONAL_ALPHA_MASK
Texture2D<float4> g_tAlphaMask : register(t12);
SamplerState g_sAlphaMask : register(s12);
Texture2D<float4> g_tDitherMask : register(t15);
#endif
#if BONE_TINTING
Texture2D<float4> g_tBoneIndex : register(t13);
SamplerState g_sBoneIndex : register(s13);
Texture2D<float4> g_tBoneTint : register(t14);
SamplerState g_sBoneTint : register(s14);
#endif
#if FACE
Texture2D<float4> g_tFaceDetail : register(t8);
SamplerState g_sFaceDetail : register(s8);
#endif
#if EYE && (DISMEMBERMENT || DISMEMBERMENT_MEATCUFF)
Texture2D<float4> g_tCutAlbedo : register(t9);
SamplerState g_sCutAlbedo : register(s9);
Texture2D<float4> g_tCutNormalMap : register(t10);
SamplerState g_sCutNormalMap : register(s10);
Texture2D<float4> g_tCutMaterial : register(t11);
SamplerState g_sCutMaterial : register(s11);
#endif
#if PIPBOY_SCREEN
Texture2D<float4> g_tPipboyScreen : register(t4);
SamplerState g_sPipboyScreen : register(s4);
#elif MENU_SCREEN
Texture2D<float4> g_tMenuOverlay : register(t4);
SamplerState g_sMenuOverlay : register(s4);
#endif
#if LOD_LANDSCAPE && !BONE_TINTING
Texture2D<float4> g_tLodColorNoise : register(t13);
SamplerState g_sLodColorNoise : register(s13);
Texture2D<float4> g_tLodNormalNoise : register(t15);
SamplerState g_sLodNormalNoise : register(s15);
#endif

#if LANDSCAPE && INSTANCED
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
#elif LANDSCAPE
Texture2D<float4> g_tLandAlbedo[4] : register(t0);
Texture2D<float4> g_tLandNormal[4] : register(t4);
Texture2D<float4> g_tLandMaterial[4] : register(t8);
SamplerState g_sLandAlbedo[4] : register(s0);
SamplerState g_sLandNormal[4] : register(s4);
SamplerState g_sLandMaterial[4] : register(s8);
#endif

#if LANDSCAPE && LAND_LOD_BLEND && INSTANCED
Texture2DArray<float4> g_tLandLodAlbedo : register(t3);
SamplerState g_sLandLodAlbedo : register(s3);
#elif LANDSCAPE && LAND_LOD_BLEND
Texture2D<float4> g_tLandLodAlbedo : register(t14);
SamplerState g_sLandLodAlbedo : register(s14);
#endif

#if LANDSCAPE && LAND_LOD_BLEND
Texture2D<float4> g_tLandColorNoise : register(t13);
SamplerState g_sLandColorNoise : register(s13);
Texture2D<float4> g_tLandNormalNoise : register(t15);
SamplerState g_sLandNormalNoise : register(s15);
#endif

struct PS_INPUT
{
    float4 position : SV_POSITION;
#if TESSELLATE_DISP_HEIGHT
    float2 uv : TEXCOORD0;
#endif
#if EYE && DISMEMBERMENT
    float2 cutCoord : TEXCOORD4;
#endif
#if TESSELLATE_DISP_HEIGHT
#if VC
    float4 vertexColor : COLOR0;
#endif
    float3 tangent : TEXCOORD1;
    float3 bitangent : TEXCOORD2;
    float3 normal : TEXCOORD3;
#endif
#if EYE && DISMEMBERMENT
    float2 dismemberWeight : TEXCOORD5;
#endif
#if TESSELLATE_DISP_HEIGHT
    float4 curr_pos_u : POSITION1;
    float4 tessellatedPosition : POSITION2;
#else
    float3 tangent : TEXCOORD0;
#if FACE
    float faceMask : TEXCOORD6;
#endif
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 curr_pos_u : TEXCOORD3;
    float4 prev_pos_v : TEXCOORD4;
#if VC
    float4 vertexColor : COLOR0;
#endif
#if PIPBOY_SCREEN
    float3 pipboyView : TEXCOORD6;
#endif
#if DISMEMBERMENT_MEATCUFF
    float2 dismember : TEXCOORD6;
    float3 cuffRadial : TEXCOORD7;
    float3 cuffAxis : TEXCOORD8;
    float3 cuffNormal : TEXCOORD9;
#endif
#endif
#if BONE_TINTING
    float4 boneTint : COLOR1;
#endif
#if INSTANCED || (EYE && COMBINED)
    nointerpolation uint instanceIndex : COLOR2;
#endif
#if LANDSCAPE
    float2 lodAlbedoUV : TEXCOORD6;
    float4 layerWeights : TEXCOORD7;
    float3 lodBlend : TEXCOORD8;
#endif
#if LOD_LANDSCAPE
    float2 lodUV : TEXCOORD9;
#endif
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
#if !BLEND || TESSELLATE_DISP_HEIGHT || DISMEMBERMENT_MEATCUFF
    float2 motionVec : SV_Target5;
#endif
};

#if EARLYDEPTH && !TREE_ANIM && !DISMEMBERMENT_MEATCUFF
[earlydepthstencil]
#endif
PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

#if TESSELLATE_DISP_HEIGHT
    float2 uv = input.uv;
#else
    float2 uv = float2(input.curr_pos_u.w, input.prev_pos_v.w);
#endif
#if ADDITIONAL_ALPHA_MASK
    if (cb2_alpha_mask_params.y)
    {
        int2 ditherTexel = (int2)(input.position.xy % 4.0);
        float ditherValue = g_tDitherMask.Load(int3(ditherTexel, 0)).x;
        clip(cb2_alpha_mask_params.y * (0.5 - ditherValue)
             + cb2_alpha_mask_params.z - 0.5);
    }
    if (cb2_alpha_mask_params.w)
    {
        float maskAlpha = g_tAlphaMask.Sample(
            g_sAlphaMask,
            uv).w;
        clip(cb2_alpha_mask_params.x - maskAlpha);
    }
#endif
#if LOD_LANDSCAPE && !BONE_TINTING
    float4 lodNoiseUV = (input.lodUV.xyxy + cb0_lod_noise_offset.zwzw)
                      * float4(0.000250, 0.000250, 0.000350, 0.000350);
    float3 lodColorNoise = g_tLodColorNoise.Sample(
        g_sLodColorNoise, lodNoiseUV.xy).xyz;
    float2 lodNormalNoiseXY = g_tLodNormalNoise.Sample(
        g_sLodNormalNoise, lodNoiseUV.zw).xy;
    lodNormalNoiseXY = lodNormalNoiseXY * 2.0 - 1.0;
    lodColorNoise = lodColorNoise * (34.0 / 9.0) - 2.006;
#endif
#if DISMEMBERMENT_MEATCUFF && !GLOWMAP
    float3 tNorm = normalize(input.tangent);
    float3 bNorm = normalize(input.bitangent);
    float3 nGeom = normalize(input.normal);
#endif
#if LANDSCAPE
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
#if LAND_LOD_BLEND && INSTANCED
    float3 landLodAlbedo = g_tLandLodAlbedo.Sample(
        g_sLandLodAlbedo,
        float3(input.lodAlbedoUV,
               g_bLandInstances[input.instanceIndex].lodSlice)).xyz;
#elif LAND_LOD_BLEND
    float3 landLodAlbedo = g_tLandLodAlbedo.Sample(
        g_sLandLodAlbedo, input.lodAlbedoUV).xyz;
#endif
#if LAND_LOD_BLEND
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
#endif
#elif EYE && COMBINED
    float4 albedoSample = g_tEyeAlbedo.Sample(
        g_sAlbedo,
        float3(uv, g_bEyeInstances[input.instanceIndex].textureSlices.x));
#elif DISMEMBERMENT_MEATCUFF
    float4 albedoSample = g_tAlbedo.Sample(g_sAlbedo, uv);
#elif TEXTURE
    float4 albedoSample = g_tAlbedo.Sample(g_sAlbedo, uv);
#else
    float4 albedoSample = 1.0;
#endif
#if LOD_LANDSCAPE && !BONE_TINTING
    albedoSample.xyz *= lodColorNoise;
#endif
#if GRADIENT_REMAP && (!HAIR || ADDITIONAL_ALPHA_MASK)
#if EYE && COMBINED
    float gradientU = pow(albedoSample.y, 1.0 / 2.2);
    float4 gradientSample = g_tEyeGradient.SampleLevel(
        g_sGradient,
        float3(gradientU,
               g_bEyeInstances[input.instanceIndex].gradientV,
               g_bEyeInstances[input.instanceIndex].gradientSlice),
        0.0);
#else
#if VC
    float gradientVcPow = pow(input.vertexColor.x, 1.0 / 2.2);
#endif
    float gradientU = pow(albedoSample.y, 1.0 / 2.2);
#if VC
    float gradientV = cb2_gradient_params.x - (1.0 - gradientVcPow);
#else
    float gradientV = cb2_gradient_params.x;
#endif
    float4 gradientSample = g_tGradient.SampleLevel(
        g_sGradient, float2(gradientU, gradientV), 0.0);
#endif
#endif

#if !LANDSCAPE
#if DISMEMBERMENT_MEATCUFF
    float4 normalMapSample = g_tNormalMap.Sample(g_sNormalMap, uv);
#elif EYE && COMBINED
    float4 normalMapSample = g_tEyeNormalMap.Sample(
        g_sNormalMap,
        float3(uv, g_bEyeInstances[input.instanceIndex].textureSlices.y));
#elif TEXTURE
    float4 normalMapSample = g_tNormalMap.Sample(g_sNormalMap, uv);
#else
    float4 normalMapSample = 0.5;
#endif
#if MODELSPACENORMALS
#if FACE
    float3 modelNormal = normalMapSample.xzy * 2.0 - 1.0;
    float4 faceDetail = g_tFaceDetail.Sample(g_sFaceDetail, uv) * 2.0 - 1.0;
    float3 blendedModelNormal = lerp(
        modelNormal, faceDetail.xzy, input.faceMask);
#else
    float3 nmRaw = normalMapSample.xyz * 2.0 - 1.0;
#endif
#else
    float2 nts_xy = normalMapSample.xy * 2.0 - 1.0;
    float nts_xy_lenSq = saturate(dot(nts_xy, nts_xy));
    float nts_z = sqrt(1.0 - nts_xy_lenSq);
#if FACE
    float4 faceDetail = g_tFaceDetail.Sample(g_sFaceDetail, uv);
    float2 faceNormalXY = faceDetail.xy * 2.0 - 1.0;
    float faceNormalZ = sqrt(
        1.0 - saturate(dot(faceNormalXY, faceNormalXY)));
    float3 faceNormal = normalize(lerp(
        float3(0.0, 0.0, 1.0),
        float3(faceNormalXY, faceNormalZ),
        input.faceMask));
    nts_xy += faceNormal.xy;
    float3 normalizedFaceNts = normalize(float3(nts_xy, nts_z));
#endif
#endif
#endif

#if LANDSCAPE
    float auxRoughness = landMaterial.y;
#elif !(HAIR && !BLEND) && !(GRADIENT_REMAP && HAIR && BLEND && !ADDITIONAL_ALPHA_MASK)
#if DISMEMBERMENT_MEATCUFF
    float auxRoughness = g_tMaterial.Sample(g_sMaterial, uv).y;
    float materialX = g_tMaterial.Sample(g_sMaterial, uv).x;
#elif EYE && COMBINED
    float4 materialSample = g_tEyeMaterial.Sample(
        g_sMaterial,
        float3(uv, g_bEyeInstances[input.instanceIndex].textureSlices.z));
#elif TEXTURE
    float4 materialSample = g_tMaterial.Sample(g_sMaterial, uv);
#else
    float4 materialSample = float4(0.0, 1.0, 0.0, 0.0);
#endif
#if DISMEMBERMENT_MEATCUFF
#if GRADIENT_REMAP
    auxRoughness *= gradientSample.w;
#endif
#else
#if GRADIENT_REMAP
    float auxRoughness = materialSample.y * gradientSample.w;
#else
    float auxRoughness = materialSample.y;
#endif
#endif
#endif
// The cut cap carries its own surface; the measured emission guards each channel separately.
#if EYE && DISMEMBERMENT
    bool dismembered = input.dismemberWeight.y > 0.0;
    if (dismembered)
    {
        auxRoughness = g_tCutMaterial.Sample(g_sCutMaterial, input.cutCoord).y;
    }
    if (dismembered)
    {
        materialSample.x = g_tCutMaterial.Sample(g_sCutMaterial, input.cutCoord).x;
    }
#endif

#if VC
#if LANDSCAPE
    landAlbedo *= input.vertexColor.xyz;
#elif TREE_ANIM && ALPHA_TEST
    albedoSample.xyz *= input.vertexColor.xyz;
#elif EYE && DISMEMBERMENT && BLEND
    albedoSample.a *= input.vertexColor.a;
#elif EYE && DISMEMBERMENT_MEATCUFF && GRADIENT_REMAP
    albedoSample.a *= input.vertexColor.a;
#elif HAIR && (BLEND || GRADIENT_REMAP)
    albedoSample.a *= input.vertexColor.a;
#elif LOD_LANDSCAPE && !BONE_TINTING
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
#if SKIN_TINT
#if ALPHA_TEST
    clip(albedoSample.a - cb2_specular_tint.w);
#endif
// The measured joint cell reaches this arm too, though it clears ADDITIONAL_ALPHA_MASK.
#if ADDITIONAL_ALPHA_MASK || (EYE && (DISMEMBERMENT || DISMEMBERMENT_MEATCUFF))
    float3 skinBase = pow(albedoSample.xyz, 1.0 / 2.2);
    float3 skinColor = pow(cb2_skin_tint.xyz, 1.0 / 2.2);
    bool3 useSkinLow = skinColor < 0.5;
    float3 doubledSkinBase = skinBase + skinBase;
    float3 skinLow = skinBase * skinBase;
    skinLow *= 1.0 - 2.0 * skinColor;
    skinLow = doubledSkinBase * skinColor + skinLow;
    float3 rootedSkinBase = sqrt(skinBase);
    float3 skinHigh = skinColor * 2.0 - 1.0;
    float3 inverseSkinColor = 1.0 - skinColor;
    inverseSkinColor = doubledSkinBase * inverseSkinColor;
    skinHigh = rootedSkinBase * skinHigh + inverseSkinColor;
    float3 skinResult = useSkinLow ? skinLow : skinHigh;
#elif VC || ALPHA_TEST
    float3 skinBase = pow(albedoSample.xyz, 1.0 / 2.2);
    float3 rootedSkinBase = sqrt(skinBase);
    float3 skinColor = pow(cb2_skin_tint.xyz, 1.0 / 2.2);
    float3 skinHigh = skinColor * 2.0 - 1.0;
    float3 skinLow = 1.0 - skinColor;
    float3 doubledSkinBase = skinBase + skinBase;
    skinBase *= skinBase;
    skinLow *= doubledSkinBase;
    skinHigh = rootedSkinBase * skinHigh + skinLow;
    skinLow = doubledSkinBase * skinColor
            + skinBase * (1.0 - 2.0 * skinColor);
    float3 skinResult = (skinColor < 0.5) ? skinLow : skinHigh;
#else
    float3 skinBase = pow(albedoSample.xyz, 1.0 / 2.2);
    float3 skinColor = pow(cb2_skin_tint.xyz, 1.0 / 2.2);
    float3 skinHigh = skinColor * 2.0 - 1.0;
    float3 skinLow = 1.0 - skinColor;
    float3 doubledSkinBase = skinBase + skinBase;
    skinLow *= doubledSkinBase;
    float3 rootedSkinBase = sqrt(skinBase);
    skinBase *= skinBase;
    skinHigh = rootedSkinBase * skinHigh + skinLow;
    skinLow = doubledSkinBase * skinColor
            + skinBase * (1.0 - 2.0 * skinColor);
    float3 skinResult = (skinColor < 0.5) ? skinLow : skinHigh;
#endif
    skinResult = pow(skinResult, 2.2);
    albedoSample.xyz = lerp(
        albedoSample.xyz, skinResult, cb2_skin_tint.w);
#endif
#if EYE && COMBINED
    float4 eyeParams =
        g_bEyeInstances[input.instanceIndex].alphaRefAndFlags;
#endif
#if ALPHA_TEST && !SKIN_TINT
#if EYE && COMBINED
    clip(albedoSample.a - eyeParams.x);
#else
    clip(albedoSample.a - cb2_specular_tint.w);
#endif
#endif
#if GRADIENT_REMAP && HAIR && !ADDITIONAL_ALPHA_MASK
#if VC
    float gradientVcPow = pow(input.vertexColor.x, 1.0 / 2.2);
#endif
#if BLEND
    float gradientScale = albedoSample.y * 1.8;
#endif
    float gradientU = pow(albedoSample.y, 1.0 / 2.2);
#if VC
    float gradientV = cb2_gradient_params.x - (1.0 - gradientVcPow);
#else
    float gradientV = cb2_gradient_params.x;
#endif
    float4 gradientSample = g_tGradient.SampleLevel(
        g_sGradient, float2(gradientU, gradientV), 0.0);
#if BLEND
    gradientSample.xyz *= gradientScale;
    float4 materialSample = g_tMaterial.Sample(g_sMaterial, uv);
    float auxRoughness = materialSample.y * gradientSample.w;
#endif
#endif
#if GLOWMAP && (!HAIR || BLEND)
    float4 glowMapSample = g_tGlowMap.Sample(g_sGlowMap, uv);
#endif

#if !DISMEMBERMENT_MEATCUFF || GLOWMAP
    float3 tNorm = normalize(input.tangent);
    float3 bNorm = normalize(input.bitangent);
    float3 nGeom = normalize(input.normal);
#endif
#if LANDSCAPE
    float3 nts = float3(landNormal.xy,
                        (input.isFrontFace != 0) ? landNormal.z : -landNormal.z);
#elif LOD_LANDSCAPE && !BONE_TINTING
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
#elif MODELSPACENORMALS
#if FACE
    float signedModelNormalZ = (input.isFrontFace != 0)
                             ? blendedModelNormal.z
                             : -blendedModelNormal.z;
    float3 nts = float3(blendedModelNormal.xy, signedModelNormalZ);
#else
    float3 nts = float3(nmRaw.x, nmRaw.z,
                        (input.isFrontFace != 0) ? nmRaw.y : -nmRaw.y);
#endif
#else
#if FACE
    float signedFaceNtsZ = (input.isFrontFace != 0)
                         ? normalizedFaceNts.z
                         : -normalizedFaceNts.z;
    float3 nts = float3(normalizedFaceNts.xy, signedFaceNtsZ);
#else
    float3 nts = float3(nts_xy, (input.isFrontFace != 0) ? nts_z : -nts_z);
#endif
#endif
#if DISMEMBERMENT_MEATCUFF
    float axisX;
    float axisY;
    float axisZ;
    bool outsideCuff =
        input.dismember.x < 0.0 || input.dismember.x > 10.0;
    [branch] if (outsideCuff)
    {
        axisX = dot(tNorm, nts);
        axisY = dot(bNorm, nts);
        axisZ = dot(nGeom, nts);
        clip(-1.0);
    }
    else
    {
        float cuffProjection =
            dot(cb2_cut_tangent.xyz, input.cuffRadial) * 0.5 + 0.5;
        bool cuffPositiveSide =
            dot(cb2_cut_bitangent.xyz, input.cuffRadial) > 0.0;
        float4 cuffCoordinates;
        cuffCoordinates.x = cuffProjection * 0.5;
        cuffCoordinates.z = input.dismember.x * 0.1;
        cuffCoordinates.w = cuffProjection * -0.5 + 1.0;
        float2 cuffNormalUV = cuffPositiveSide
                             ? cuffCoordinates.xz
                             : cuffCoordinates.wz;
        float2 cuffSurfaceUV = 1.0 - cuffNormalUV;
#if GRADIENT_REMAP
        gradientSample =
            g_tCutAlbedo.Sample(g_sCutAlbedo, cuffSurfaceUV);
        albedoSample = gradientSample;
#else
        albedoSample = g_tCutAlbedo.Sample(g_sCutAlbedo, cuffSurfaceUV);
#endif
        auxRoughness =
            g_tCutMaterial.Sample(g_sCutMaterial, cuffSurfaceUV).y;
        materialX =
            g_tCutMaterial.Sample(g_sCutMaterial, cuffSurfaceUV).x;
        float2 cutNtsXY = g_tCutNormalMap.Sample(
            g_sCutNormalMap, cuffNormalUV).xy * 2.0 - 1.0;
        float cutNtsZ = sqrt(1.0 - dot(cutNtsXY, cutNtsXY));
        float3 cutNts = float3(cutNtsXY, cutNtsZ);
#if MODELSPACENORMALS
        float3 cutModelNormal = cutNts.y * input.cuffNormal;
        cutModelNormal =
            cutNts.x * input.cuffAxis + cutModelNormal;
        cutModelNormal =
            cutNts.z * input.cuffRadial + cutModelNormal;
        cutNts = normalize(cutModelNormal);
#endif
        axisX = dot(tNorm, cutNts);
        axisY = dot(bNorm, cutNts);
        axisZ = dot(nGeom, cutNts);
    }
#else
    float axisX = dot(tNorm, nts);
    float axisY = dot(bNorm, nts);
    float axisZ = dot(nGeom, nts);
#endif
// The cut normal is oriented by a constant cap frame, not the interpolated one.
#if EYE && DISMEMBERMENT
    if (dismembered)
    {
        float2 cutNtsXY = g_tCutNormalMap.Sample(
            g_sCutNormalMap, input.cutCoord).xy * 2.0 - 1.0;
        float3 cutNts = float3(cutNtsXY, sqrt(1.0 - dot(cutNtsXY, cutNtsXY)));
        axisX = dot(cb2_cut_tangent.xyz, cutNts);
        axisY = dot(cb2_cut_bitangent.xyz, cutNts);
        axisZ = dot(cb2_cut_normal.xyz, cutNts);
    }
#endif
#if LANDSCAPE
    float4 landFlags = max(cb2_land_flags0, 0.0) * input.layerWeights.x
                     + max(cb2_land_flags1, 0.0) * input.layerWeights.y;
    landFlags += max(cb2_land_flags2, 0.0) * input.layerWeights.z;
    landFlags += max(cb2_land_flags3, 0.0) * input.layerWeights.w;
    float fadeScale = cb12_idx30_global_fade.x * landFlags.w;
#elif !(EYE && COMBINED)
    float fadeScale = cb2_material_flags.w * cb12_idx30_global_fade.x;
#endif

#if !HAIR || BLEND
#if INSTANCED && !LANDSCAPE
    InstanceRecord instanceRecord = cb13_instances[input.instanceIndex];
#elif !(EYE && COMBINED)
    float2 anchor = cb2_scroll_anchor_and_alpha.xy;
    float2 target = cb2_scroll_delta.xy;
    bool2 svScrolls = (target >= 0.0);
    float2 delta = target - anchor;
    float2 svRaw = cb12_idx30_global_fade.xx * delta + anchor;
    float2 sval = svScrolls ? (svRaw * anchor) : anchor;
#endif

#if LANDSCAPE
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
#elif EYE && COMBINED
    float eyeZ = saturate(eyeParams.w);
    float matZxFade = eyeZ * cb12_idx30_global_fade.x;
    float2 eyeAux = float2(eyeParams.z, eyeParams.y);
    bool2 eyeAuxPinned = (eyeAux == -1.0);
    float2 eyeAuxGate = eyeAuxPinned ? 0.0 : cb12_idx30_global_fade.xx;
    float matZxComp = mad(
        -cb12_idx30_global_fade.x,
        eyeZ,
        1.0);
#if DISMEMBERMENT_MEATCUFF
    float lerpedMatX = materialX * matZxComp + matZxFade;
#else
    float lerpedMatX = materialSample.x * matZxComp + matZxFade;
#endif
    float4 eyeScroll = g_bEyeInstances[input.instanceIndex].scroll;
    float2 eyeSval = lerp(eyeScroll.xy, eyeAux, eyeAuxGate);
#else
    float matZxFade = cb12_idx30_global_fade.x
                    * cb2_material_flags.z;
    float matZxComp = mad(
        -cb2_material_flags.z,
        cb12_idx30_global_fade.x,
        1.0);
#if DISMEMBERMENT_MEATCUFF
    float lerpedMatX = materialX * matZxComp + matZxFade;
#else
    float lerpedMatX = materialSample.x * matZxComp + matZxFade;
#endif
#endif

#if LANDSCAPE
    output.auxA.x = auxRoughness * landSval.x;
    output.auxA.y = lerpedMatX * landSval.y;
#elif EYE && COMBINED
    output.auxA.x = auxRoughness * eyeSval.x;
    output.auxA.y = lerpedMatX * eyeSval.y
                  * g_bEyeInstances[input.instanceIndex].scroll.y;
#elif INSTANCED
    output.auxA.x = auxRoughness * instanceRecord.rows[7].x;
    output.auxA.y = lerpedMatX * instanceRecord.rows[7].y;
#else
    output.auxA.x = auxRoughness * sval.x;
    output.auxA.y = lerpedMatX * sval.y;
#endif
#endif

    float3 nWorldFromTBN = normalize(float3(axisX, axisY, min(axisZ, 0.0)));
#if BLEND
    output.normalOct.z = -nWorldFromTBN.z;
#endif
    float octZ = sqrt(nWorldFromTBN.z * -8.0 + 8.0);
    output.normalOct.xy = nWorldFromTBN.xy / octZ.xx + 0.5;
#if !HAIR || BLEND
#if INSTANCED && !LANDSCAPE
    output.auxA.z = 0.01 * instanceRecord.rows[7].w;
#elif EYE && COMBINED
    output.auxA.z = g_bEyeInstances[input.instanceIndex].scroll.w * 0.01;
#else
    output.auxA.z = cb2_scroll_anchor_and_alpha.w * 0.01;
#endif
#endif

#if EYE && COMBINED
    float eyeMaterialIndex = g_bEyeMaterials[input.instanceIndex].materialIndex;
    output.material.z = sqrt(
        g_bEyeInstances[input.instanceIndex].smoothness * 0.02);
#else
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
#endif

#if LANDSCAPE
    output.material.x = (cb12_idx30_global_fade.x != 0.0)
                      ? cb2_land_material_gate.x
                      : 0.0;
#else
    bool bitB = cb2_material_flags.x;
    bool bitA = cb2_material_flags.y && cb12_idx30_global_fade.x;
    output.material.x = (bitB || bitA) ? 1.0 : 0.0;
#endif

#if EYE && COMBINED
    output.material.y = eyeMaterialIndex * (1.0 / 255.0);
    output.material.w = saturate(eyeMaterialIndex);
#else
    output.material.y = cb2_material_id_and_smoothness.x * (1.0 / 255.0);
    output.material.w = saturate(cb2_material_id_and_smoothness.x);
#endif

#if HAIR && !BLEND
    float3 hairMaterialVector = g_tHairMaterial.Sample(
        g_sHairMaterial, uv).xyz * 2.0 - 1.0;
    float3 hairVector = normalize(hairMaterialVector);
    output.auxA.x = dot(tNorm, hairVector);
    output.auxA.y = dot(bNorm, hairVector);
    output.auxA.z = dot(nGeom, hairVector);
#endif

#if PIPBOY_SCREEN && ADDITIONAL_ALPHA_MASK
    bool bypassFade = (cb2_material_flags.w == -1.0);
    float alphaFade = bypassFade ? 1.0 : (1.0 - fadeScale);
#endif

#if PIPBOY_SCREEN
    float3 pipboyView = normalize(-input.pipboyView);
    float3 pipboyNormal = float3(axisX, axisY, axisZ);
    float2 pipboyOffset = -pipboyView.xy
                        / dot(pipboyView, pipboyNormal);
    pipboyOffset = (pipboyOffset + pipboyNormal.xy * 2.0)
                 * cb0_pipboy_params.x;
    float2 pipboyUV = uv + float2(pipboyOffset.x, -pipboyOffset.y)
                    * cb0_pipboy_params.y;
    float3 pipboySample = pow(g_tPipboyScreen.Sample(
        g_sPipboyScreen, pipboyUV).xyz, 2.2);
#if ADDITIONAL_ALPHA_MASK
    output.albedo.xyz = albedoSample.xyz * alphaFade
                      + pipboySample * cb0_pipboy_params.z;
#endif
#endif

#if GLOWMAP && (!HAIR || BLEND)
#if PIPBOY_SCREEN
    output.specTint.xyz = cb2_specular_tint.xyz * glowMapSample.xyz;
#else
    output.specTint.xyz = glowMapSample.xyz * cb2_specular_tint.xyz;
#endif
#elif INSTANCED && !LANDSCAPE
    output.specTint.xyz = instanceRecord.rows[8].xyz;
#elif EYE && COMBINED
    output.specTint.xyz = g_bEyeInstances[input.instanceIndex].specularTint;
#else
    output.specTint.xyz = cb2_specular_tint.xyz;
#endif
#if PIPBOY_SCREEN
    output.specTint.xyz += pipboySample * cb0_pipboy_params.w;
#endif

#if BLEND
    float blendAlpha = (cb2_blend_params.y == 1.0) ? albedoSample.a : 1.0;
    clip(cb2_blend_params.x * blendAlpha - (4.0 / 255.0));
    blendAlpha *= cb2_blend_params.x;
    output.albedo.w = blendAlpha;
    output.normalOct.w = blendAlpha;
#if SKIN_TINT
    output.auxA.w = 0.019608;
#elif HAIR
#if BLEND && GRADIENT_REMAP && ADDITIONAL_ALPHA_MASK
    output.auxA.w = blendAlpha;
#else
    output.auxA.w = min(max(blendAlpha, 5.0 / 255.0), 1.0);
#endif
#elif SKEW_SPECULAR_ALPHA
    output.auxA.w = pow(blendAlpha, 0.1);
#else
    output.auxA.w = blendAlpha;
#endif
    output.specTint.w = blendAlpha;
#else
#if HAIR
#if GRADIENT_REMAP
    output.albedo.w = albedoSample.y;
#else
    output.albedo.w = 0.0;
#endif
#elif INSTANCED && !LANDSCAPE
    output.albedo.w = instanceRecord.rows[7].z;
#elif EYE && COMBINED
    output.albedo.w = eyeScroll.z;
#else
    output.albedo.w = cb2_scroll_anchor_and_alpha.z;
#endif
#endif

#if FACE && ADDITIONAL_ALPHA_MASK
    float faceInfluence = (1.0 - faceDetail.w) * input.faceMask;
    float faceAlbedoScale = faceInfluence * -0.3 + 1.0;
    float3 faceAlbedo = albedoSample.xyz * faceAlbedoScale;
#endif

#if LANDSCAPE
    bool landFadeApplies = (landFlags.w != -1.0);
    float alphaFade = saturate(1.0 - fadeScale);
#elif EYE && COMBINED
    float eyeFadeFlag = g_bEyeInstances[input.instanceIndex].fadeFlag;
    bool eyeFadeApplies = (eyeFadeFlag != -1.0);
    float alphaFade = saturate(
        1.0 - cb12_idx30_global_fade.x * eyeFadeFlag);
#elif !PIPBOY_SCREEN || !ADDITIONAL_ALPHA_MASK
    bool bypassFade = (cb2_material_flags.w == -1.0);
    float alphaFade = bypassFade ? 1.0 : (1.0 - fadeScale);
#endif
// The cut cap swaps the composed colour, so its single fade is applied after the swap.
#if EYE && DISMEMBERMENT
    alphaFade = 1.0;
#endif

#if BONE_TINTING
    float4 boneIndex = g_tBoneIndex.Sample(g_sBoneIndex, uv);
    float2 boneUv = float2(boneIndex.y, frac(cb2_bone_tint_params.x));
    float4 boneTint = g_tBoneTint.Sample(g_sBoneTint, boneUv);
    float3 boneContribution = boneTint.xyz * boneTint.w
                            * boneIndex.w * input.boneTint.w * 4.0;
#endif
#if FACE && !ADDITIONAL_ALPHA_MASK
    float faceInfluence = (1.0 - faceDetail.w) * input.faceMask;
    float faceAlbedoScale = faceInfluence * -0.3 + 1.0;
#endif
#if EYE && COMBINED
#if GRADIENT_REMAP
    float3 eyeAlbedo = gradientSample.xyz;
#else
    float3 eyeAlbedo = albedoSample.xyz;
#endif
    [branch] if (eyeFadeApplies)
    {
        output.albedo.xyz = eyeAlbedo * alphaFade;
    }
    else
    {
        output.albedo.xyz = eyeAlbedo;
    }
#elif GRADIENT_REMAP && HAIR && BLEND && ADDITIONAL_ALPHA_MASK
    float4 finalAlbedo = 0.0;
    finalAlbedo.x = albedoSample.y * 1.8;
    finalAlbedo.xzw = gradientSample.xyzw * finalAlbedo.xxxx;
    float hairAlpha = max(blendAlpha, 5.0 / 255.0);
    output.auxA.w = min(hairAlpha, 1.0);
    bool lateBypassFade = (cb2_material_flags.w == -1.0);
    float lateAlphaFade = lateBypassFade ? 1.0 : (1.0 - fadeScale);
    output.albedo.xyz = finalAlbedo.xzw * lateAlphaFade;
#elif GRADIENT_REMAP
#if BONE_TINTING
    output.albedo.xyz = gradientSample.xyz * alphaFade + boneContribution;
#else
    output.albedo.xyz = gradientSample.xyz * alphaFade;
#endif
#else
#if HAIR && BLEND
    output.albedo.xyz = 0.0;
#else
#if LANDSCAPE
    output.albedo.xyz = landFadeApplies ? (landAlbedo * alphaFade) : landAlbedo;
#elif BONE_TINTING && FACE && ADDITIONAL_ALPHA_MASK
    output.albedo.xyz = faceAlbedo * alphaFade + boneContribution;
#elif BONE_TINTING && FACE
    output.albedo.xyz = albedoSample.xyz * faceAlbedoScale * alphaFade
                      + boneContribution;
#elif BONE_TINTING
    output.albedo.xyz = albedoSample.xyz * alphaFade + boneContribution;
#elif FACE && ADDITIONAL_ALPHA_MASK
    output.albedo.xyz = faceAlbedo * alphaFade;
#elif FACE
    output.albedo.xyz = albedoSample.xyz * faceAlbedoScale * alphaFade;
#elif PIPBOY_SCREEN && !ADDITIONAL_ALPHA_MASK
    output.albedo.xyz = albedoSample.xyz * alphaFade
                      + pipboySample * cb0_pipboy_params.z;
#elif !PIPBOY_SCREEN
    output.albedo.xyz = albedoSample.xyz * alphaFade;
#endif
#endif
#endif
// The cut cap is its own surface: it replaces the composed colour, then takes the fade.
#if EYE && DISMEMBERMENT
    if (dismembered)
    {
        output.albedo.xyz = g_tCutAlbedo.Sample(g_sCutAlbedo, input.cutCoord).xyz;
    }
#if VC && BLEND
    else
    {
        output.albedo.xyz = albedoSample.xyz * input.vertexColor.xyz;
    }
#endif
    output.albedo.xyz *= (cb2_material_flags.w == -1.0)
                       ? 1.0
                       : (1.0 - fadeScale);
#endif

#if !BLEND
#if PIPBOY_SCREEN
    output.auxA.w = 0.0156864;
#elif SKIN_TINT
    output.auxA.w = 0.019608;
#elif HAIR
    output.auxA.w = 0.0039216;
#elif FACE
    output.auxA.w = 0.019608;
#else
    output.auxA.w = 1.0;
#endif
#endif

#if !BLEND || TESSELLATE_DISP_HEIGHT || DISMEMBERMENT_MEATCUFF
#if TESSELLATE_DISP_HEIGHT
    float4 worldPosition = float4(input.curr_pos_u.xyz, 1.0);
    float currClipX = dot(CurrFrame_WorldToClip_row0, worldPosition);
    float currClipY = dot(CurrFrame_WorldToClip_row1, worldPosition);
    float currClipW = dot(CurrFrame_WorldToClip_row3, worldPosition);
    float2 currNDC = float2(currClipX, currClipY) / currClipW.xx;

    float prevClipX = dot(PrevFrame_WorldToClip_row0, worldPosition);
    float prevClipY = dot(PrevFrame_WorldToClip_row1, worldPosition);
    float prevClipW = dot(PrevFrame_WorldToClip_row3, worldPosition);
    float2 prevNDC = float2(prevClipX, prevClipY) / prevClipW.xx;
#else
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
#endif

    output.motionVec = (currNDC - prevNDC) * float2(-0.5, 0.5);
#endif
    return output;
}
