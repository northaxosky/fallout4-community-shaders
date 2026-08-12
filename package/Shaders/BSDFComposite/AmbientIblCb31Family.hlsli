// Family axes measured from native DXBC; defaults reproduce blob 7460585e.
#ifndef AMBIENT_DIFFUSE_SET_B
#define AMBIENT_DIFFUSE_SET_B 1
#endif
#ifndef AMBIENT_SUBSURFACE_BLUR
#define AMBIENT_SUBSURFACE_BLUR 1
#endif
#ifndef AMBIENT_SSAO
#define AMBIENT_SSAO 1
#endif
#ifndef AMBIENT_UNUSED_TEXCOORD
#define AMBIENT_UNUSED_TEXCOORD 0
#endif
cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_11[12];
    float4 ViewToWorld_row0;  // = cb12[12]
    float4 ViewToWorld_row1;  // = cb12[13]
    float4 ViewToWorld_row2;  // = cb12[14]
    float4 cb12_pad_15_19[5];
    float4 FarReproj_row0;
    float4 FarReproj_row1;
    float4 FarReproj_row2;
    float4 FarReproj_row3;
    float4 NearReproj_row0;
    float4 NearReproj_row1;
    float4 NearReproj_row2;
    float4 NearReproj_row3;
    float4 cb12_pad_28_29[2];
    float4 cb12_idx30_ibl_desaturation;
};
cbuffer PerCall_CB0 : register(b0)
{
    float4 cb0_idx0_screen_scale_and_blur_tolerance;
    float4 cb0_idx1_lit_scene_weight;
    float4 cb0_idx2_lit_scene_alpha;
};
cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 cb2_pad_1_4[4];
    float4 cb2_idx5_lit_scene_uv_clamp;
};
Texture2D<float4> g_tGbufferNormal : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tGbufferShadingData : register(t3);
#if AMBIENT_SUBSURFACE_BLUR
Texture2D<float4> g_tSkinAuxColor : register(t4);
#endif
Texture2D<float4> g_tAmbientDiffuseA : register(t5);
#if AMBIENT_SUBSURFACE_BLUR
Texture2D<float4> g_tAmbientProbeA : register(t6);
#endif
Texture2D<float4> g_tMainDepth : register(t7);
TextureCubeArray<float4> g_tIBLProbeCube : register(t8);
#if AMBIENT_SSAO
Texture2D<float4> g_tSSAO : register(t9);
#endif
Texture2D<float4> g_tBlurSource : register(t10);
#if AMBIENT_DIFFUSE_SET_B
Texture2D<float4> g_tAmbientDiffuseB : register(t11);
#if AMBIENT_SUBSURFACE_BLUR
Texture2D<float4> g_tAmbientProbeB : register(t12);
#endif
#endif
#ifdef WETNESS_EFFECTS
Texture2D<float> g_tWetnessMask : register(t13);
#endif
Texture2D<float4> g_tLitScene : register(t14);
#if AMBIENT_SUBSURFACE_BLUR
Texture2D<float4> g_tBlurDepthRef : register(t15);
#endif
#ifdef SSGI
Texture2D<float4> g_tSSGIBounce : register(t0);
#endif
SamplerState g_sGbufferNormal      : register(s1);
SamplerState g_sGbufferMaterial    : register(s2);
SamplerState g_sGbufferShadingData : register(s3);
#if AMBIENT_SUBSURFACE_BLUR
SamplerState g_sSkinAuxColor       : register(s4);
#endif
SamplerState g_sAmbientDiffuseA    : register(s5);
#if AMBIENT_SUBSURFACE_BLUR
SamplerState g_sAmbientProbeA      : register(s6);
#endif
SamplerState g_sMainDepth          : register(s7);
SamplerState g_sIBLProbeCube       : register(s8);
#if AMBIENT_SSAO
SamplerState g_sSSAO               : register(s9);
#endif
SamplerState g_sBlurSource         : register(s10);
#if AMBIENT_DIFFUSE_SET_B
SamplerState g_sAmbientDiffuseB    : register(s11);
#if AMBIENT_SUBSURFACE_BLUR
SamplerState g_sAmbientProbeB      : register(s12);
#endif
#endif
SamplerState g_sLitScene           : register(s14);
#if AMBIENT_SUBSURFACE_BLUR
SamplerState g_sBlurDepthRef       : register(s15);
#endif
#if AMBIENT_SUBSURFACE_BLUR
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
static const float3 SSSS_RING_WEIGHTS[10] =
{
    float3(0.0047169099561870098, 0.0001847709936555475, 5.07566e-005),      // -2.0
    float3(0.019283099099993706,  0.0028201800305396318, 0.00084213999798521399), // -1.28
    float3(0.036390,              0.01309990044683218,   0.006436849944293499),   // -0.72
    float3(0.08219040185213089,   0.035860799252986908,  0.020926099270582199),   // -0.32
    float3(0.077180199325084686,  0.113491,              0.079380303621292114),   // -0.08
    float3(0.077180199325084686,  0.113491,              0.079380303621292114),   // +0.08
    float3(0.08219040185213089,   0.035860799252986908,  0.020926099270582199),   // +0.32
    float3(0.036390,              0.01309990044683218,   0.006436849944293499),   // +0.72
    float3(0.019283099099993706,  0.0028201800305396318, 0.00084213999798521399), // +1.28
    float3(0.0047169099561870098, 0.0001847709936555475, 5.07565e-005),      // +2.0
};
static const float3 SSSS_CENTER_WEIGHT = float3(0.560479, 0.669086, 0.784728);
#endif
struct PS_INPUT
{
    float4 position : SV_POSITION;
#if AMBIENT_UNUSED_TEXCOORD
    float3 texcoord : TEXCOORD0;
#endif
};
struct PS_OUTPUT
{
    float4 color : SV_Target0;
};
PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
#ifdef WETNESS_EFFECTS
    float wetness = g_tWetnessMask.Load(int3(int2(input.position.xy), 0)).x;
#endif
    float2 uv = input.position.xy * ScreenSize.xy;
    float3 shadingData    = g_tGbufferShadingData.SampleLevel(g_sGbufferShadingData, uv, 0).xyw;
    float3 ambientA       = g_tAmbientDiffuseA.SampleLevel(g_sAmbientDiffuseA, uv, 0).xyz;
#if AMBIENT_DIFFUSE_SET_B
    float3 ambientB       = g_tAmbientDiffuseB.SampleLevel(g_sAmbientDiffuseB, uv, 0).xyz;
    float3 ambientPairSum = (ambientA + ambientB) * 3.0;
#else
    float3 ambientPairSum = ambientA * 3.0;
#endif
    float depth = g_tMainDepth.SampleLevel(g_sMainDepth, uv, 0).x;
    bool isNearPath = (depth <= 0.01);
    float4 pos;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    if (isNearPath)
    {
        pos.z = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        pos.z = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }
    float3 blurSourceCenter = g_tBlurSource.SampleLevel(g_sBlurSource, uv, 0).xyz;
    float yTripled    = shadingData.y * 3.0;
    float rough01     = saturate(shadingData.x - 0.3);
    float roughFactor = min(1.0 / rsqrt(rough01), 1.0);
    float glossFactor = yTripled * roughFactor;
    float4 matRaw = g_tGbufferMaterial.SampleLevel(g_sGbufferMaterial, uv, 0);
    float matSliceFloat = matRaw.y;
    float matGlossOrSpec = matRaw.z;
    float glossSquaredScaled = matGlossOrSpec * matGlossOrSpec * 50.0;
    bool  hasIBL = (matSliceFloat > 0.5 / 255.0);
    float3 iblColor = float3(0, 0, 0);
    if (hasIBL)
    {
        float2 enc = g_tGbufferNormal.SampleLevel(g_sGbufferNormal, uv, 0).xy * 4.0 - 2.0;
        float  encDotEnc = dot(enc, enc);
        float  zRecon = 1.0 - encDotEnc * 0.25;
        float3 normalView = float3(enc * sqrt(zRecon), -(1.0 - encDotEnc * 0.5));
        float2 sn = float2(uv.x * ScreenSize.z, 1.0 - uv.y * ScreenSize.w);
        pos.xy = sn * 2.0 - 1.0;
        pos.w  = 1.0;
        float3 posViewXYZ = float3(dot(reprojRow0, pos),
                                   dot(reprojRow1, pos),
                                   dot(reprojRow2, pos));
        float  posViewW   = dot(reprojRow3, pos);
        pos.xyz = posViewXYZ / posViewW;
        float3 viewDirNeg = normalize(-pos.xyz);
        float  ndotv2     = dot(viewDirNeg, normalView);
        ndotv2 = ndotv2 + ndotv2;
        float3 reflView   = normalView * -ndotv2 + viewDirNeg;
        float3 reflWorld;
        reflWorld.x = dot(ViewToWorld_row0.xyz, reflView);
        reflWorld.y = dot(ViewToWorld_row1.xyz, reflView);
        reflWorld.z = dot(ViewToWorld_row2.xyz, reflView);
        float mipLevel = (1.0 - shadingData.x) * 6.0;
        mipLevel = pos.z * 0.001953125 + mipLevel;
        float arraySlice = floor(matSliceFloat * 255.0 - 1.0);
        float3 cubeSample = g_tIBLProbeCube.SampleLevel(g_sIBLProbeCube,
                                                        float4(reflWorld, arraySlice),
                                                        mipLevel).xyz;
        float  luma   = dot(cubeSample, float3(0.299, 0.587, 0.114));
        float  desatW = cb12_idx30_ibl_desaturation.y * 0.9;
        iblColor      = lerp(cubeSample, luma.xxx, desatW);
    }
    else
    {
        iblColor = float3(0, 0, 0);
    }
    float2 uvClamped     = min(uv, cb2_idx5_lit_scene_uv_clamp.xy);
    float4 litRaw        = g_tLitScene.Sample(g_sLitScene, uvClamped);
    float  litAlpha      = min(litRaw.w * cb0_idx2_lit_scene_alpha.z, 1.0);
    float3 iblLitBlend   = lerp(iblColor,
                                 litRaw.xyz * cb0_idx1_lit_scene_weight.x,
                                 litAlpha);
    bool isSkin = (abs(shadingData.z * 255.0 - 5.0) < 0.25);
    float3 ambientAccum;
#if AMBIENT_SUBSURFACE_BLUR
    if (isSkin)
    {
        float3 skinAux = g_tSkinAuxColor.Sample(g_sSkinAuxColor, uv).xyz;
        float  depthMaskF = isNearPath ? 1.0 : 0.0;
        float  blurDepthScale = depthMaskF * cb0_idx0_screen_scale_and_blur_tolerance.z + 1.0;
        float  refDepth = g_tBlurDepthRef.SampleLevel(g_sBlurDepthRef, uv, 0).x;
        float  centerRef = blurDepthScale * refDepth;
        float2 tapBase = cb0_idx0_screen_scale_and_blur_tolerance.xx
                       * float2(0.078125, 0.138890)
                       / centerRef;
        float3 blurAccum = SSSS_CENTER_WEIGHT * blurSourceCenter;
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
                                       g_sBlurDepthRef, tapUV, 0).x;
                float  tol = cb0_idx0_screen_scale_and_blur_tolerance.y * 0.1;
                float  dt = min(tol * abs(-tapDepth * blurDepthScale + centerRef), 1.0);
                tapBlended = lerp(tapColor, blurSourceCenter, dt);
            }
            else
            {
                tapBlended = blurSourceCenter;
            }
            blurAccum += tapBlended * SSSS_RING_WEIGHTS[i];
        }
        float3 probeA = g_tAmbientProbeA.SampleLevel(g_sAmbientProbeA, uv, 0).xyz;
#if AMBIENT_DIFFUSE_SET_B
        float3 probeB = g_tAmbientProbeB.SampleLevel(g_sAmbientProbeB, uv, 0).xyz;
        ambientAccum  = blurAccum + (probeA + probeB + skinAux);
#else
        ambientAccum  = blurAccum + (probeA + skinAux);
#endif
    }
    else
    {
        ambientAccum = blurSourceCenter;
    }
#else
    ambientAccum = blurSourceCenter;
#endif
    float3 spec = iblLitBlend;
    spec *= glossFactor;
    spec *= glossSquaredScaled;
#ifdef WETNESS_EFFECTS
    ambientAccum *= lerp(1.0, 0.5, wetness);
    spec *= lerp(1.0, 2.0, wetness);
#endif
    float3 modulated = spec * ambientPairSum + ambientAccum;
#if AMBIENT_SSAO
    float aoFactor = g_tSSAO.Sample(g_sSSAO, uvClamped).x;
#else
    const float aoFactor = 1.0;
#endif
#ifdef SSGI
    int3 ssgiPx = int3(int2(input.position.xy), 0);
    float3 giBounce = g_tSSGIBounce.Load(ssgiPx).rgb;
    output.color.xyz = modulated * aoFactor + giBounce;
#else
    output.color.xyz = modulated * aoFactor;
#endif
    output.color.w = 1.0;
    return output;
}
