// AE 1.11.221 - ambient/composite cube-t8 family without t0 and without the
// local-probe (t14) path. Three orthogonal axes cover eight native blobs:
//
//   TILELIGHT   t11/t12          fxp key bit 0x10000
//   FOGSTACK    CB12[47] fog +   fxp key bit 0x40
//               type 2/3 exclusion
//   OUTPUTMASK  t9 + cb2[5]      fxp key bit 0x200
//
//   8fbcce64  0x00000120   -                     -            -
//   79ea9877  0x00010120   TILELIGHT             -            -
//   3c459639  0x00000160   -                     FOGSTACK     -
//   39637a98  0x00010160   TILELIGHT             FOGSTACK     -
//   97335e28  0x00000320   -                     -            OUTPUTMASK
//   3cf8ea09  0x00010320   TILELIGHT             -            OUTPUTMASK
//   ad139695  0x00000360   -                     FOGSTACK     OUTPUTMASK
//   da65c284  0x00010360   TILELIGHT             FOGSTACK     OUTPUTMASK

#ifndef TILELIGHT
#define TILELIGHT 0
#endif
#ifndef FOGSTACK
#define FOGSTACK 0
#endif
#ifndef OUTPUTMASK
#define OUTPUTMASK 0
#endif

#if FOGSTACK
#define AMBIENT_FRAME_COUNT 47
#else
#define AMBIENT_FRAME_COUNT 31
#endif

#if OUTPUTMASK
#define SCREEN_SETUP_COUNT 6
#elif FOGSTACK
#define SCREEN_SETUP_COUNT 3
#else
#define SCREEN_SETUP_COUNT 1
#endif

cbuffer AmbientFrame : register(b12)
{
    float4 ambientFrame[AMBIENT_FRAME_COUNT];
};

cbuffer AmbientPass : register(b0)
{
    float4 ambientPass[1];
};

cbuffer ScreenSetup : register(b2)
{
    float4 screenSetup[SCREEN_SETUP_COUNT];
};

SamplerState normalSampler      : register(s1);
SamplerState materialSampler    : register(s2);
SamplerState surfaceSampler     : register(s3);
SamplerState skinAuxSampler     : register(s4);
SamplerState lightingASampler   : register(s5);
SamplerState skinLightSampler   : register(s6);
SamplerState depthSampler       : register(s7);
SamplerState environmentSampler : register(s8);
#if OUTPUTMASK
SamplerState outputMaskSampler  : register(s9);
#endif
SamplerState ambientBaseSampler : register(s10);
#if TILELIGHT
SamplerState lightingBSampler   : register(s11);
SamplerState skinProbeSampler   : register(s12);
#endif
SamplerState linearDepthSampler : register(s15);

Texture2D<float4>        normalTexture      : register(t1);
Texture2D<float4>        materialTexture    : register(t2);
Texture2D<float4>        surfaceTexture     : register(t3);
Texture2D<float4>        skinAuxTexture     : register(t4);
Texture2D<float4>        lightingATexture   : register(t5);
Texture2D<float4>        skinLightTexture   : register(t6);
Texture2D<float4>        depthTexture       : register(t7);
TextureCubeArray<float4> environmentTexture : register(t8);
#if OUTPUTMASK
Texture2D<float4>        outputMaskTexture  : register(t9);
#endif
Texture2D<float4>        ambientBaseTexture : register(t10);
#if TILELIGHT
Texture2D<float4>        lightingBTexture   : register(t11);
Texture2D<float4>        skinProbeTexture   : register(t12);
#endif
Texture2D<float4>        linearDepthTexture : register(t15);

float3 sampleSkinTap(float2 coordinate, float depthScale, float centerDepth, float3 centerColor)
{
    float materialId = surfaceTexture.SampleLevel(surfaceSampler, coordinate, 0.0).w;
    float3 tapColor;

    if (abs(materialId * 255.0 - 5.0) < 0.25)
    {
        tapColor = ambientBaseTexture.SampleLevel(ambientBaseSampler, coordinate, 0.0).xyz;
        float tapDepth = linearDepthTexture.SampleLevel(linearDepthSampler, coordinate, 0.0).x;
        float depthWeightScale = ambientPass[0].y * 0.1;
        float depthWeight = min(depthWeightScale * abs(centerDepth - tapDepth * depthScale), 1.0);
        tapColor = lerp(tapColor, centerColor, depthWeight);
    }
    else
    {
        tapColor = centerColor;
    }

    return tapColor;
}

float3 reconstructViewPosition(float2 coordinate, float linearizedDepth,
                               float4 row0, float4 row1, float4 row2, float4 row3)
{
    float2 clipPosition = float2(
        coordinate.x * screenSetup[0].z,
        1.0 - coordinate.y * screenSetup[0].w
    ) * 2.0 - 1.0;
    float4 projectedPosition = float4(clipPosition, linearizedDepth, 1.0);
    float4 viewPosition;
    viewPosition.x = dot(row0, projectedPosition);
    viewPosition.y = dot(row1, projectedPosition);
    viewPosition.z = dot(row2, projectedPosition);
    viewPosition.w = dot(row3, projectedPosition);
    return viewPosition.xyz / viewPosition.w;
}

float3 sampleDirectLighting(float2 coordinate)
{
    float3 directLighting = lightingATexture.SampleLevel(lightingASampler, coordinate, 0.0).xyz;
#if TILELIGHT
    directLighting += lightingBTexture.SampleLevel(lightingBSampler, coordinate, 0.0).xyz;
#endif
    return directLighting * 3.0;
}

float3 composeAmbient(float2 coordinate, float3 directLighting, float glossFactor,
                      float gloss, float3 environment, float3 centerColor)
{
    float3 color = environment * glossFactor;
    color *= gloss;
    color = color * directLighting + centerColor;
#if OUTPUTMASK
    color *= outputMaskTexture.Sample(outputMaskSampler, min(coordinate, screenSetup[5].xy)).x;
#endif
    return color;
}

float4 main(float4 position : SV_POSITION) : SV_Target0
{
    float2 coordinate = position.xy * screenSetup[0].xy;
    float3 surface = surfaceTexture.SampleLevel(surfaceSampler, coordinate, 0.0).xyw;
#if !FOGSTACK
    // The non-fog blobs hoist the lighting/gloss terms above the depth branch.
    float3 directLighting = sampleDirectLighting(coordinate);
#endif

    float deviceDepth = depthTexture.SampleLevel(depthSampler, coordinate, 0.0).x;
    bool nearDepth = deviceDepth <= 0.01;
    float linearizedDepth;
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;

    if (nearDepth)
    {
        linearizedDepth = deviceDepth * 100.0;
        row0 = ambientFrame[24];
        row1 = ambientFrame[25];
        row2 = ambientFrame[26];
        row3 = ambientFrame[27];
    }
    else
    {
        linearizedDepth = deviceDepth * 1.01 - 0.01;
        row0 = ambientFrame[20];
        row1 = ambientFrame[21];
        row2 = ambientFrame[22];
        row3 = ambientFrame[23];
    }

#if FOGSTACK
    // The fog stack consumes viewPosition unconditionally, so the FOGSTACK
    // blobs hoist the reconstruction out of the probe branch.
    float3 viewPosition = reconstructViewPosition(coordinate, linearizedDepth, row0, row1, row2, row3);
#endif

    float3 centerColor = ambientBaseTexture.SampleLevel(ambientBaseSampler, coordinate, 0.0).xyz;
#if !FOGSTACK
    float glossFactor = surface.y * 3.0;
    float roughFactor = min(1.0 / rsqrt(saturate(surface.x - 0.3)), 1.0);
    glossFactor *= roughFactor;
#endif
    float2 material = materialTexture.SampleLevel(materialSampler, coordinate, 0.0).yz;
#if !FOGSTACK
    float gloss = material.y * material.y * 50.0;
#endif

    float3 environment = 0.0;
    if (material.x > 0.0019607844296842813)
    {
#if !FOGSTACK
        // The non-fog blobs decode the normal before reconstructing the position.
        float2 encodedNormal = normalTexture.SampleLevel(normalSampler, coordinate, 0.0).xy * 4.0 - 2.0;
        float normalLengthSquared = dot(encodedNormal, encodedNormal);
        float2 normalFactors = 1.0 - normalLengthSquared * float2(0.25, 0.5);
        float3 normal = float3(encodedNormal * sqrt(normalFactors.x), -normalFactors.y);

        float3 viewPosition = reconstructViewPosition(coordinate, linearizedDepth, row0, row1, row2, row3);
#else
        float2 encodedNormal = normalTexture.SampleLevel(normalSampler, coordinate, 0.0).xy * 4.0 - 2.0;
        float normalLengthSquared = dot(encodedNormal, encodedNormal);
        float2 normalFactors = 1.0 - normalLengthSquared * float2(0.25, 0.5);
        float3 normal = float3(encodedNormal * sqrt(normalFactors.x), -normalFactors.y);
#endif

        float3 reflected = reflect(normalize(-viewPosition), normal);
        float3 environmentCoordinate = float3(
            dot(ambientFrame[12].xyz, reflected),
            dot(ambientFrame[13].xyz, reflected),
            dot(ambientFrame[14].xyz, reflected)
        );
        float mipLevel = (1.0 - surface.x) * 6.0;
        mipLevel = viewPosition.z * 0.001953125 + mipLevel;
        float arraySlice = floor(material.x * 255.0 - 1.0);
        environment = environmentTexture.SampleLevel(
            environmentSampler,
            float4(environmentCoordinate, arraySlice),
            mipLevel
        ).xyz;
        float luminance = dot(environment, float3(0.299, 0.587, 0.114));
        environment = lerp(environment, luminance.xxx, ambientFrame[30].y * 0.9);
    }

#if FOGSTACK
    bool3 materialMatches = abs(surface.z * 255.0 - float3(5.0, 2.0, 3.0)) < 0.25;
    bool isSkin = materialMatches.x;
#else
    bool isSkin = abs(surface.z * 255.0 - 5.0) < 0.25;
#endif

    if (isSkin)
    {
        float3 skinAux = skinAuxTexture.Sample(skinAuxSampler, coordinate).xyz;
        float depthScale = 1.0 + (float)nearDepth * ambientPass[0].z;
        float centerDepth = linearDepthTexture.SampleLevel(linearDepthSampler, coordinate, 0.0).x * depthScale;
        float2 tapStep = ambientPass[0].x * float2(0.078125, 0.13889) / centerDepth;

        float4 tapCoordinates = coordinate.xyxy + tapStep.xyxy * float4(-2.0, -2.0, -1.28, -1.28);
        float3 firstTap = sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor);
        float3 blurred = firstTap * float3(0.00471690995618701, 0.0001847709936555475, 0.00005075660010334104);
        // Bitcast barrier: preserves FXC's native mul-then-mad association without changing bits.
        blurred = centerColor * float3(0.560479, 0.669086, 0.784728) + asfloat(asuint(blurred));
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.019283099099993706, 0.002820180030539632, 0.000842139997985214);

        tapCoordinates = coordinate.xyxy + tapStep.xyxy * float4(-0.72, -0.72, -0.32, -0.32);
        blurred += sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor)
            * float3(0.036390, 0.01309990044683218, 0.006436849944293499);
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.08219040185213089, 0.03586079925298691, 0.0209260992705822);

        tapCoordinates = coordinate.xyxy + tapStep.xyxy * float4(-0.08, -0.08, 0.08, 0.08);
        blurred += sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor)
            * float3(0.07718019932508469, 0.113491, 0.07938030362129211);
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.07718019932508469, 0.113491, 0.07938030362129211);

        tapCoordinates = coordinate.xyxy + tapStep.xyxy * float4(0.32, 0.32, 0.72, 0.72);
        blurred += sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor)
            * float3(0.08219040185213089, 0.03586079925298691, 0.0209260992705822);
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.036390, 0.01309990044683218, 0.006436849944293499);

        tapCoordinates.zw = coordinate + tapStep * 1.28;
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.019283099099993706, 0.002820180030539632, 0.000842139997985214);
        tapCoordinates.xy = coordinate + tapStep * 2.0;
        blurred += sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor)
            * float3(0.00471690995618701, 0.0001847709936555475, 0.000050756498239934444);

        float3 skinBase = skinLightTexture.SampleLevel(skinLightSampler, coordinate, 0.0).xyz;
#if TILELIGHT
        skinBase += skinProbeTexture.SampleLevel(skinProbeSampler, coordinate, 0.0).xyz;
#endif
        skinBase += skinAux;
        centerColor = blurred + skinBase;
    }

#if !FOGSTACK
    return float4(composeAmbient(coordinate, directLighting, glossFactor, gloss, environment, centerColor), 1.0);
#else
    float4 output;
    if (!(materialMatches.y || materialMatches.z))
    {
        float3 directLighting = sampleDirectLighting(coordinate);
        float glossFactor = surface.y * 3.0;
        float roughFactor = min(1.0 / rsqrt(saturate(surface.x - 0.3)), 1.0);
        glossFactor *= roughFactor;
        float gloss = material.y * material.y * 50.0;
        float3 color = composeAmbient(coordinate, directLighting, glossFactor, gloss, environment, centerColor);

        float height = dot(ambientFrame[14], float4(viewPosition, 1.0)) + ambientFrame[35].z;
        float distanceSquared = dot(viewPosition, viewPosition);
        float distance = sqrt(distanceSquared);
        float distanceCoordinate = distance * ambientFrame[41].x - ambientFrame[41].z;
        float distanceSaturated = saturate(distanceCoordinate);
        float2 heightWeights = saturate(height * ambientFrame[46].xy - ambientFrame[46].zw);
        float heightWeight = lerp(heightWeights.x, heightWeights.y, distanceSaturated);

        float fogLimit = ambientFrame[43].w;
        if (distanceCoordinate > 0.75)
        {
            fogLimit = min(
                ambientFrame[43].w + (distanceSaturated - 0.75) * 4.0 * (1.0 - ambientFrame[43].w),
                1.0);
        }
        float nearDistanceScale = distanceCoordinate < 0.015 ? distanceSaturated * 66.666672 : 1.0;
        float fogCurve = min(pow(distanceSaturated, ambientFrame[42].w), fogLimit);
        float heightAlpha = 1.0 - heightWeight + heightWeight * ambientFrame[44].w;
        float3 lowFog = lerp(ambientFrame[42].xyz, ambientFrame[44].xyz, fogCurve);
        float3 highFog = lerp(ambientFrame[43].xyz, ambientFrame[45].xyz, fogCurve);
        float3 fogColor = lerp(lowFog, highFog, heightWeight);
        float fogAmount = fogCurve * heightAlpha;
        fogAmount *= nearDistanceScale;

        float3 worldDirection = normalize(viewPosition);
        float sunAmount = pow(max(dot(worldDirection, screenSetup[1].xyz), 0.0), screenSetup[2].w)
            * screenSetup[1].w;
        fogColor = lerp(fogColor, screenSetup[2].xyz, sunAmount);
        if (fogAmount < ambientFrame[43].w)
        {
            float gray = dot(color, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
            fogColor = lerp(fogColor, gray.xxx, gray);
        }

        output = float4(lerp(color, fogColor, fogAmount), 1.0);
    }
    else
    {
        output = 0.0;
    }
    return output;
#endif
}
