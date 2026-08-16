#ifndef COMPOSITE_CB12_COUNT
#define COMPOSITE_CB12_COUNT 47
#endif
#ifndef COMPOSITE_CB2_COUNT
#define COMPOSITE_CB2_COUNT 6
#endif
#ifndef COMPOSITE_FOG_STACK
#define COMPOSITE_FOG_STACK 1
#endif
#ifndef COMPOSITE_MODULATION
#define COMPOSITE_MODULATION 1
#endif
#ifndef COMPOSITE_MATERIAL_EXCLUSION
#define COMPOSITE_MATERIAL_EXCLUSION 1
#endif

cbuffer SceneData : register(b12)
{
    float4 scene[COMPOSITE_CB12_COUNT];
};

cbuffer ScreenData : register(b2)
{
    float4 screenData[COMPOSITE_CB2_COUNT];
};

Texture2D<float4> baseTexture : register(t0);
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> materialTexture : register(t2);
Texture2D<float4> typeTexture : register(t3);
Texture2D<float4> ambientTexture : register(t4);
Texture2D<float4> diffuseTexture : register(t5);
Texture2D<float4> lightTexture : register(t6);
Texture2D<float4> depthTexture : register(t7);
TextureCubeArray<float4> probeTexture : register(t8);
#if COMPOSITE_MODULATION
Texture2D<float4> modulationTexture : register(t9);
#endif
#ifdef TILED_LIGHTS
Texture2D<float4> tileDiffuseTexture : register(t11);
Texture2D<float4> tileLightTexture : register(t12);
#endif

SamplerState baseSampler : register(s0);
SamplerState normalSampler : register(s1);
SamplerState materialSampler : register(s2);
SamplerState typeSampler : register(s3);
SamplerState ambientSampler : register(s4);
SamplerState diffuseSampler : register(s5);
SamplerState lightSampler : register(s6);
SamplerState depthSampler : register(s7);
SamplerState probeSampler : register(s8);
#if COMPOSITE_MODULATION
SamplerState modulationSampler : register(s9);
#endif
#ifdef TILED_LIGHTS
SamplerState tileDiffuseSampler : register(s11);
SamplerState tileLightSampler : register(s12);
#endif

struct PSInput
{
    float4 position : SV_POSITION;
#ifdef COMPOSITE_UNUSED_TEXCOORD
    float3 texcoord : TEXCOORD0;
#endif
};

float4 main(PSInput input) : SV_Target0
{
    float2 uv = input.position.xy * screenData[0].xy;
    float3 typeData = typeTexture.SampleLevel(typeSampler, uv, 0.0).xyw;
    float hardwareDepth = depthTexture.SampleLevel(depthSampler, uv, 0.0).x;
    bool nearDepth = hardwareDepth <= 0.01;

    float linearDepth;
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
    if (nearDepth)
    {
        linearDepth = hardwareDepth * 100.0;
        row0 = scene[24];
        row1 = scene[25];
        row2 = scene[26];
        row3 = scene[27];
    }
    else
    {
        linearDepth = hardwareDepth * 1.01 - 0.01;
        row0 = scene[20];
        row1 = scene[21];
        row2 = scene[22];
        row3 = scene[23];
    }

    float2 projectedXY = float2(
        uv.x * screenData[0].z,
        1.0 - uv.y * screenData[0].w) * 2.0 - 1.0;
    float4 projected = float4(projectedXY, linearDepth, 1.0);
    float3 worldNumerator = float3(
        dot(row0, projected),
        dot(row1, projected),
        dot(row2, projected));
    float worldDenominator = dot(row3, projected);
    float3 worldPosition = worldNumerator / worldDenominator;

    float2 material = materialTexture.SampleLevel(materialSampler, uv, 0.0).yz;
    float3 probeColor = 0.0;
    if (material.x > (0.5 / 255.0))
    {
        float2 encodedNormal =
            normalTexture.SampleLevel(normalSampler, uv, 0.0).xy * 4.0 - 2.0;
        float encodedLengthSquared = dot(encodedNormal, encodedNormal);
        float normalScale = sqrt(1.0 - encodedLengthSquared * 0.25);
        float3 normal = float3(
            encodedNormal * normalScale,
            -(1.0 - encodedLengthSquared * 0.5));
        float3 reflected = reflect(normalize(-worldPosition), normal);
        float3 probeDirection = float3(
            dot(scene[12].xyz, reflected),
            dot(scene[13].xyz, reflected),
            dot(scene[14].xyz, reflected));
        float probeLod = mad(
            1.0 - typeData.x,
            6.0,
            worldPosition.z * 0.001953125);
        float probeSlice = floor(material.x * 255.0 - 1.0);
        probeColor = probeTexture.SampleLevel(
            probeSampler,
            float4(probeDirection, probeSlice),
            probeLod).xyz;
        float probeLuma = dot(probeColor, float3(0.299, 0.587, 0.114));
        probeColor = lerp(probeColor, probeLuma.xxx, scene[30].y * 0.9);
    }

#if COMPOSITE_MATERIAL_EXCLUSION
    bool2 excludedType =
        abs(typeData.z * 255.0 - float2(2.0, 3.0)) < 0.25;
    if (excludedType.x || excludedType.y)
#ifdef COMPOSITE_ALPHA_ONE
        return float4(0.0, 0.0, 0.0, 1.0);
#else
        return 0.0;
#endif
#endif

    float4 base = baseTexture.SampleLevel(baseSampler, uv, 0.0);
    float3 diffuse = diffuseTexture.SampleLevel(diffuseSampler, uv, 0.0).xyz;
#ifdef TILED_LIGHTS
    diffuse += tileDiffuseTexture.SampleLevel(tileDiffuseSampler, uv, 0.0).xyz;
#endif
    diffuse *= 3.0;

    float3 ambient = ambientTexture.Sample(ambientSampler, uv).xyz;
    ambient += lightTexture.SampleLevel(lightSampler, uv, 0.0).xyz;
#ifdef TILED_LIGHTS
    ambient += tileLightTexture.SampleLevel(tileLightSampler, uv, 0.0).xyz;
#endif

    float3 color = mad(diffuse, base.xyz, ambient);
    float gloss = typeData.y * 3.0;
    float roughFactor = min(
        1.0,
        1.0 / rsqrt(saturate(typeData.x - 0.3)));
    gloss *= roughFactor;
    float specularScale = material.y * material.y * 50.0;
    color = mad(probeColor * gloss * specularScale, diffuse, color);

#if COMPOSITE_MODULATION
    float2 modulationUV = min(uv, screenData[5].xy);
    float modulation =
        modulationTexture.Sample(modulationSampler, modulationUV).x;
    color *= modulation;
#endif

#if COMPOSITE_FOG_STACK
    float height =
        dot(scene[14], float4(worldPosition, 1.0)) + scene[35].z;
    float distanceSquared = dot(worldPosition, worldPosition);
    float distance = sqrt(distanceSquared);
    float distanceCoordinate = distance * scene[41].x - scene[41].z;
    float distanceSaturated = saturate(distanceCoordinate);
    float2 heightWeights =
        saturate(height * scene[46].xy - scene[46].zw);
    float heightWeight =
        lerp(heightWeights.x, heightWeights.y, distanceSaturated);

    float fogLimit = scene[43].w;
    if (distanceCoordinate > 0.75)
    {
        fogLimit = min(
            scene[43].w +
                (distanceSaturated - 0.75) * 4.0 * (1.0 - scene[43].w),
            1.0);
    }
    float nearDistanceScale = distanceCoordinate < 0.015
        ? distanceSaturated * 66.666672
        : 1.0;
    float fogCurve =
        min(pow(distanceSaturated, scene[42].w), fogLimit);
    float heightAlpha =
        1.0 - heightWeight + heightWeight * scene[44].w;
    float3 lowFog =
        lerp(scene[42].xyz, scene[44].xyz, fogCurve);
    float3 highFog =
        lerp(scene[43].xyz, scene[45].xyz, fogCurve);
    float3 fogColor = lerp(lowFog, highFog, heightWeight);
    float fogAmount = fogCurve * heightAlpha * nearDistanceScale;

    float3 worldDirection = normalize(worldPosition);
    float sunAmount = pow(
        max(dot(worldDirection, screenData[1].xyz), 0.0),
        screenData[2].w) * screenData[1].w;
    fogColor = lerp(fogColor, screenData[2].xyz, sunAmount);
    if (fogAmount < scene[43].w)
    {
        float gray =
            dot(color, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
        fogColor = lerp(fogColor, gray.xxx, gray);
    }

    color = lerp(color, fogColor, fogAmount);
#endif

#ifdef COMPOSITE_ALPHA_ONE
    return float4(color, 1.0);
#else
    return float4(color, base.w);
#endif
}
