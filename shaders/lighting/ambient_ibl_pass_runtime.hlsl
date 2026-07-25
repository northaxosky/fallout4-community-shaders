// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Reconstruction of the current exterior ambient/IBL PS:
// Shaders011.fxp blob 3560, sha1 2b6e36c08aca7ff0a3bd10da326e00b3b0367383.
// Unlike the proven blob-3559 reference, this CB12[47] permutation applies
// the fog/color stack after AO and consumes t12 in the material-5 blur path.

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_11[12];
    float4 ViewToWorld_row0;
    float4 ViewToWorld_row1;
    float4 ViewToWorld_row2;
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
    float4 IblDesaturation;
    float4 cb12_pad_31_34[4];
    float4 CameraPosAdjust;
    float4 cb12_pad_36_40[5];
    float4 FogDistanceRamp;
    float4 FogNearLowColorAndPower;
    float4 FogNearHighColorAndClamp;
    float4 FogFarLowColorAndDensity;
    float4 FogFarHighColor;
    float4 FogHeightRamp;
};

cbuffer PerCall_CB0 : register(b0)
{
    float4 ScreenBlurParameters;
    float4 LitSceneWeight;
    float4 LitSceneAlpha;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 SunDirectionAndIntensity;
    float4 SunColorAndSpecPower;
    float4 cb2_pad_3_4[2];
    float4 LitSceneUvClamp;
};

Texture2D<float4> g_tGbufferNormal       : register(t1);
Texture2D<float4> g_tGbufferMaterial     : register(t2);
Texture2D<float4> g_tGbufferShadingData  : register(t3);
Texture2D<float4> g_tSkinAuxColor        : register(t4);
Texture2D<float4> g_tAmbientDiffuseA     : register(t5);
Texture2D<float4> g_tAmbientProbeA       : register(t6);
Texture2D<float4> g_tMainDepth           : register(t7);
TextureCubeArray<float4> g_tIblProbeCube : register(t8);
Texture2D<float4> g_tSsao                : register(t9);
Texture2D<float4> g_tBlurSource          : register(t10);
Texture2D<float4> g_tAmbientDiffuseB     : register(t11);
// Frame 9219 binds the fullscreen draw's blurred SSLR source here. Blob 3560
// samples it only inside the explicit material-5 blur branch.
Texture2D<float4> g_tBlurredSslr         : register(t12);
Texture2D<float4> g_tLitScene            : register(t14);
Texture2D<float4> g_tBlurDepthRef        : register(t15);

#ifdef SSGI
Texture2D<float4> g_tSSGIBounce : register(t0);
#endif

SamplerState g_sGbufferNormal      : register(s1);
SamplerState g_sGbufferMaterial    : register(s2);
SamplerState g_sGbufferShadingData : register(s3);
SamplerState g_sSkinAuxColor       : register(s4);
SamplerState g_sAmbientDiffuseA    : register(s5);
SamplerState g_sAmbientProbeA      : register(s6);
SamplerState g_sMainDepth          : register(s7);
SamplerState g_sIblProbeCube       : register(s8);
SamplerState g_sSsao               : register(s9);
SamplerState g_sBlurSource         : register(s10);
SamplerState g_sAmbientDiffuseB    : register(s11);
SamplerState g_sBlurredSslr        : register(s12);
SamplerState g_sLitScene           : register(s14);
SamplerState g_sBlurDepthRef       : register(s15);

static const float2 SSSS_RING_OFFSETS[10] =
{
    float2(-2.000, -2.000),
    float2(-1.280, -1.280),
    float2(-0.720, -0.720),
    float2(-0.320, -0.320),
    float2(-0.080, -0.080),
    float2( 0.080,  0.080),
    float2( 0.320,  0.320),
    float2( 0.720,  0.720),
    float2( 1.280,  1.280),
    float2( 2.000,  2.000),
};

static const float3 SSSS_RING_WEIGHTS[10] =
{
    float3(0.004717, 0.000185, 0.000051),
    float3(0.019283, 0.002820, 0.000842),
    float3(0.036390, 0.013100, 0.006437),
    float3(0.077180, 0.113491, 0.079380),
    float3(0.082190, 0.035861, 0.020926),
    float3(0.082190, 0.035861, 0.020926),
    float3(0.077180, 0.113491, 0.079380),
    float3(0.036390, 0.013100, 0.006437),
    float3(0.019283, 0.002820, 0.000842),
    float3(0.004717, 0.000185, 0.000051),
};

static const float3 SSSS_CENTER_WEIGHT =
    float3(0.560479, 0.669086, 0.784728);

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
    float2 uv = input.position.xy * ScreenSize.xy;

    float3 shadingData =
        g_tGbufferShadingData.SampleLevel(g_sGbufferShadingData, uv, 0).xyw;
    float depth = g_tMainDepth.SampleLevel(g_sMainDepth, uv, 0).y;
    bool isNearPath = depth <= 0.01;
    float linearizedDepth =
        isNearPath ? depth * 100.0 : depth * 1.01 - 0.01;
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    float3 uvRemapped;
    uvRemapped.x = uv.x * ScreenSize.z;
    uvRemapped.z = -uv.y * ScreenSize.w + 1.0;
    float4 positionInput =
        float4(uvRemapped.xz * 2.0 - 1.0, linearizedDepth, 1.0);
    float4 positionViewH;
    positionViewH.x = dot(reprojRow0, positionInput);
    positionViewH.y = dot(reprojRow1, positionInput);
    positionViewH.z = dot(reprojRow2, positionInput);
    positionViewH.w = dot(reprojRow3, positionInput);
    float3 positionView = positionViewH.xyz / positionViewH.www;

    float3 blurSourceCenter =
        g_tBlurSource.SampleLevel(g_sBlurSource, uv, 0).xyz;
    float4 material =
        g_tGbufferMaterial.SampleLevel(g_sGbufferMaterial, uv, 0);
    bool hasIbl = material.y > 0.001961;
    float3 iblColor = float3(0.0, 0.0, 0.0);
    if (hasIbl)
    {
        float2 encodedNormal =
            g_tGbufferNormal.SampleLevel(g_sGbufferNormal, uv, 0).xy * 4.0 - 2.0;
        float encodedLengthSquared = dot(encodedNormal, encodedNormal);
        float3 normalView = float3(
            encodedNormal * sqrt(1.0 - encodedLengthSquared * 0.25),
            -(1.0 - encodedLengthSquared * 0.5));
        float3 viewDirection =
            -positionView * rsqrt(dot(positionView, positionView));
        float3 reflectionView =
            viewDirection - normalView * (2.0 * dot(viewDirection, normalView));
        float3 reflectionWorld;
        reflectionWorld.x = dot(ViewToWorld_row0.xyz, reflectionView);
        reflectionWorld.y = dot(ViewToWorld_row1.xyz, reflectionView);
        reflectionWorld.z = dot(ViewToWorld_row2.xyz, reflectionView);
        float mipLevel =
            (1.0 - shadingData.x) * 6.0 + linearizedDepth * 0.001953;
        float arraySlice = floor(material.y * 255.0 - 1.0);
        float3 cubeSample = g_tIblProbeCube.SampleLevel(
            g_sIblProbeCube, float4(reflectionWorld, arraySlice), mipLevel).xyz;
        float luminance = dot(cubeSample, float3(0.299, 0.587, 0.114));
        iblColor = lerp(
            cubeSample, luminance.xxx, IblDesaturation.y * 0.9);
    }

    float materialId = shadingData.z * 255.0;
    bool isMaterial5 = abs(materialId - 5.0) < 0.25;
    bool isMaterial2 = abs(materialId - 2.0) < 0.25;
    bool isMaterial3 = abs(materialId - 3.0) < 0.25;

    float3 ambientAccum = blurSourceCenter;
    if (isMaterial5)
    {
        float3 skinAux =
            g_tSkinAuxColor.Sample(g_sSkinAuxColor, uv).xyz;
        float blurDepthScale =
            (depth >= 0.01 ? 1.0 : 0.0) * ScreenBlurParameters.z + 1.0;
        float centerRef =
            blurDepthScale *
            g_tBlurDepthRef.SampleLevel(g_sBlurDepthRef, uv, 0).y;
        float2 tapBase =
            ScreenBlurParameters.xx * float2(0.078125, 0.138890) / centerRef;
        float3 blurAccum = float3(0.0, 0.0, 0.0);
        [unroll]
        for (int i = 0; i < 10; ++i)
        {
            float2 tapUv = uv + tapBase * SSSS_RING_OFFSETS[i];
            float tapMaterialId = g_tGbufferShadingData.SampleLevel(
                g_sGbufferShadingData, tapUv, 0).w * 255.0 - 5.0;
            float3 tapColor = blurSourceCenter;
            if (abs(tapMaterialId) < 0.25)
            {
                float3 sampledColor =
                    g_tBlurSource.SampleLevel(g_sBlurSource, tapUv, 0).xyz;
                float tapDepth =
                    g_tBlurDepthRef.SampleLevel(g_sBlurDepthRef, tapUv, 0).y;
                float depthWeight = min(
                    abs(-tapDepth * blurDepthScale + centerRef) *
                        ScreenBlurParameters.y * 0.1,
                    1.0);
                tapColor = lerp(sampledColor, blurSourceCenter, depthWeight);
            }
            blurAccum += tapColor * SSSS_RING_WEIGHTS[i];
        }
        blurAccum += blurSourceCenter * SSSS_CENTER_WEIGHT;

        float3 probeA =
            g_tAmbientProbeA.SampleLevel(g_sAmbientProbeA, uv, 0).xyz;
        float3 blurredSslr =
            g_tBlurredSslr.SampleLevel(g_sBlurredSslr, uv, 0).xyz;
        ambientAccum = probeA + blurredSslr + skinAux + blurAccum;
    }

    if (isMaterial2 || isMaterial3)
    {
        output.color = float4(0.0, 0.0, 0.0, 0.0);
        return output;
    }

    float3 ambientPair =
        (g_tAmbientDiffuseA.SampleLevel(g_sAmbientDiffuseA, uv, 0).xyz +
         g_tAmbientDiffuseB.SampleLevel(g_sAmbientDiffuseB, uv, 0).xyz) * 3.0;
    float glossFactor =
        shadingData.y * 3.0 * min(sqrt(saturate(shadingData.x - 0.3)), 1.0);
    float glossSquaredScaled = material.z * material.z * 50.0;
    float4 litScene = g_tLitScene.Sample(
        g_sLitScene, min(uv, LitSceneUvClamp.xy));
    float litAlpha = min(litScene.w * LitSceneAlpha.z, 1.0);
    float3 iblLitBlend = lerp(
        iblColor, litScene.xyz * LitSceneWeight.x, litAlpha);
    float3 modulated =
        ambientAccum + glossSquaredScaled * glossFactor * iblLitBlend * ambientPair;
    float ao = g_tSsao.Sample(g_sSsao, uv).x;
    float3 aoColor = ao * modulated;
#ifdef SSGI
    aoColor += g_tSSGIBounce.Load(int3(int2(input.position.xy), 0)).rgb;
#endif

    float fogPlaneDistance =
        dot(ViewToWorld_row2, float4(positionView, 1.0)) + CameraPosAdjust.z;
    float positionLengthSquared = dot(positionView, positionView);
    float distanceRamp =
        sqrt(positionLengthSquared) * FogDistanceRamp.x - FogDistanceRamp.z;
    float distanceFactor = saturate(distanceRamp);
    float2 fogRemapPair =
        saturate(fogPlaneDistance.xx * FogHeightRamp.xy - FogHeightRamp.zw);
    float fogBlend = lerp(
        fogRemapPair.x, fogRemapPair.y, distanceFactor);

    float fogIntensityClamp;
    if (distanceRamp > 0.75)
    {
        float distanceTail = (distanceFactor - 0.75) * 4.0;
        fogIntensityClamp = min(
            distanceTail * (1.0 - FogNearHighColorAndClamp.w) +
                FogNearHighColorAndClamp.w,
            1.0);
    }
    else
    {
        fogIntensityClamp = FogNearHighColorAndClamp.w;
    }

    float nearEscape =
        distanceRamp < 0.015 ? distanceFactor * 66.666672 : 1.0;
    float fogIntensity = min(
        fogIntensityClamp,
        pow(distanceFactor, FogNearLowColorAndPower.w));
    float fogBlendWeight =
        fogBlend * FogFarLowColorAndDensity.w + (1.0 - fogBlend);
    float3 fogColorLow = lerp(
        FogNearLowColorAndPower.xyz,
        FogFarLowColorAndDensity.xyz,
        fogIntensity);
    float3 fogColorHigh = lerp(
        FogNearHighColorAndClamp.xyz,
        FogFarHighColor.xyz,
        fogIntensity);
    float3 fogColor = lerp(fogColorLow, fogColorHigh, fogBlend);
    float fogMixFactor =
        fogBlendWeight * fogIntensity * nearEscape;

    float3 viewDirection =
        positionView * rsqrt(positionLengthSquared);
    float sunFactor = pow(
        max(dot(viewDirection, SunDirectionAndIntensity.xyz), 0.0),
        SunColorAndSpecPower.w) * SunDirectionAndIntensity.w;
    float3 sunlitFog =
        lerp(fogColor, SunColorAndSpecPower.xyz, sunFactor);
    float grayscale = dot(aoColor, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
    float3 grayscaleStack =
        sunlitFog + grayscale * (grayscale.xxx - sunlitFog);
    float3 colorStack =
        fogMixFactor < FogNearHighColorAndClamp.w
            ? grayscaleStack
            : sunlitFog;

    output.color.xyz = lerp(aoColor, colorStack, fogMixFactor);
    output.color.w = 1.0;
    return output;
}

// Static contract: CB12[47], CB0[3], CB2[6], 33 declarations, 44 samples.
