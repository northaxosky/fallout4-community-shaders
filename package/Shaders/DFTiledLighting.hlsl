#ifndef DFTILEDLIGHTING_VARIANT
#error "DFTILEDLIGHTING_VARIANT must be 1 or 2"
#endif

#if DFTILEDLIGHTING_VARIANT != 1 && DFTILEDLIGHTING_VARIANT != 2
#error "DFTILEDLIGHTING_VARIANT must be 1 or 2"
#endif

#ifdef INVERSE_SQUARE_LIGHTING
#include "InverseSquareLighting/InverseSquareLighting.hlsli"
#endif

cbuffer TiledLightingParameters : register(b0)
{
#if DFTILEDLIGHTING_VARIANT == 1
    float4 TiledParams[4];
#else
    float4 TiledParams[7];
#endif
};

cbuffer DeferredPerFrame : register(b12)
{
    float4 PerFrame[30];
};

Texture2D<float4> MainDepth : register(t0);
Texture2D<float4> GBufferAlbedo : register(t1);
Texture2D<float4> GBufferNormal : register(t2);
Texture2D<float4> GBufferMaterial : register(t4);

struct TiledLight
{
    uint Flags;
    float4 PositionRadius;
    float3 Color;
    float3 Attenuation;
    float Padding;
};

struct TileLightList
{
    uint Count;
    uint Indices[127];
};

StructuredBuffer<TiledLight> Lights : register(t6);
StructuredBuffer<TileLightList> TileLists : register(t7);
RWTexture2D<float4> DiffuseOutput : register(u0);
RWTexture2D<float4> SpecularOutput : register(u1);

float3 DecodeOctahedralNormal(float2 encoded)
{
    float2 octahedral = encoded * 4.0 - 2.0;
    float lengthSquared = dot(octahedral, octahedral);
    float2 reconstruction =
        1.0 - lengthSquared * float2(0.25, 0.5);
    float scale = sqrt(reconstruction.x);
    float2 decodedXY = octahedral * scale;
    float decodedZ = -reconstruction.y;
    return float3(decodedXY, decodedZ);
}

#if DFTILEDLIGHTING_VARIANT == 2
float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded;
    encoded.x = dot(TiledParams[4], directionH);
    encoded.y = dot(TiledParams[5], directionH);
    encoded.z = dot(TiledParams[6], directionH);
    return exp2(log2(encoded) * 2.2);
}
#endif

[numthreads(8, 8, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint tileIndex = groupId.y * (uint)TiledParams[2].z + groupId.x;
    uint2 pixel = groupId.xy * 8 + groupThreadId.xy;

    float3 diffuseAccum = 0.0;
    float3 specularAccum = 0.0;

#if DFTILEDLIGHTING_VARIANT == 2
    float depth = MainDepth.Load(uint3(pixel, 0)).x;
    bool nearDepth = depth <= 0.01;
    float linearDepth;
    float4 reprojection0;
    float4 reprojection1;
    float4 reprojection2;
    float4 reprojection3;
    if (nearDepth)
    {
        linearDepth = depth * 100.0;
        reprojection0 = PerFrame[24];
        reprojection1 = PerFrame[25];
        reprojection2 = PerFrame[26];
        reprojection3 = PerFrame[27];
    }
    else
    {
        linearDepth = depth * 1.01 - 0.01;
        reprojection0 = PerFrame[20];
        reprojection1 = PerFrame[21];
        reprojection2 = PerFrame[22];
        reprojection3 = PerFrame[23];
    }
    float2 pixelFloat = (float2)pixel;
    float4 ndcParts =
        TiledParams[3].xxyy * pixelFloat.xxyy;
    ndcParts.z = 1.0 - ndcParts.z;
    float2 ndc = ndcParts.xz * 2.0 - 1.0;
    float4 projected = float4(ndc, linearDepth, 1.0);
    float4 positionH;
    positionH.x = dot(reprojection0, projected);
    positionH.y = dot(reprojection1, projected);
    positionH.z = dot(reprojection2, projected);
    positionH.w = dot(reprojection3, projected);
    float3 positionView = positionH.xyz / positionH.www;

    float3 normalView = DecodeOctahedralNormal(
        GBufferNormal.Load(uint3(pixel, 0)).xy);
    float4 viewData;
    viewData.x = rsqrt(dot(-positionView, -positionView));
    viewData.yzw = -positionView * viewData.x;
    float positionViewInverseLength = viewData.x;
    float3 viewDirection = viewData.yzw;
    float4 material = GBufferMaterial.Load(uint3(pixel, 0));

    bool materialOne = abs(material.w * 255.0 - 1.0) < 0.25;
    float materialSpecular = materialOne ? 0.0 : material.y;
    diffuseAccum = EvaluateAmbientGradient(normalView);

    float normalDotView = dot(normalView, viewDirection);
    float3 reflectionDirection =
        2.0 * normalDotView * normalView - viewDirection;
    float oneMinusNormalDotView = 1.0 - saturate(normalDotView);
    float ambientExponent = 3.0 - material.x;
    float oneMinusNormalDotViewLog =
        log2(oneMinusNormalDotView);
    float ambientSpecularFactor =
        exp2(ambientExponent * oneMinusNormalDotViewLog) * 0.25;

    float3 ambientReflection =
        EvaluateAmbientGradient(reflectionDirection);
    ambientReflection *= ambientSpecularFactor;
    ambientReflection *= materialSpecular;
    specularAccum = ambientReflection;
#endif

    uint lightCount = TileLists[tileIndex].Count;
    if (lightCount != 0)
    {
#if DFTILEDLIGHTING_VARIANT == 1
        float depth = MainDepth.Load(uint3(pixel, 0)).x;
        bool nearDepth = depth <= 0.01;
        float linearDepth;
        float4 reprojection0;
        float4 reprojection1;
        float4 reprojection2;
        float4 reprojection3;
        if (nearDepth)
        {
            linearDepth = depth * 100.0;
            reprojection0 = PerFrame[24];
            reprojection1 = PerFrame[25];
            reprojection2 = PerFrame[26];
            reprojection3 = PerFrame[27];
        }
        else
        {
            linearDepth = depth * 1.01 - 0.01;
            reprojection0 = PerFrame[20];
            reprojection1 = PerFrame[21];
            reprojection2 = PerFrame[22];
            reprojection3 = PerFrame[23];
        }
        float2 pixelFloat = (float2)pixel;
        float4 ndcParts =
            TiledParams[3].xxyy * pixelFloat.xxyy;
        ndcParts.z = 1.0 - ndcParts.z;
        float2 ndc = ndcParts.xz * 2.0 - 1.0;
        float4 projected = float4(ndc, linearDepth, 1.0);
        float4 positionH;
        positionH.x = dot(reprojection0, projected);
        positionH.y = dot(reprojection1, projected);
        positionH.z = dot(reprojection2, projected);
        positionH.w = dot(reprojection3, projected);
        float3 positionView = positionH.xyz / positionH.www;

        float3 normalView = DecodeOctahedralNormal(
            GBufferNormal.Load(uint3(pixel, 0)).xy);
        float positionViewInverseLength =
            rsqrt(dot(-positionView, -positionView));
        float3 viewDirection =
            -positionView * positionViewInverseLength;
        float4 material = GBufferMaterial.Load(uint3(pixel, 0));
#endif

        float roughness = 1.0 - material.x;
#if DFTILEDLIGHTING_VARIANT == 1
        float specularScale = material.y * 3.1415927;
        bool materialOne = abs(material.w * 255.0 - 1.0) < 0.25;
#endif
        float albedoAlpha = GBufferAlbedo.Load(uint3(pixel, 0)).w;
        float specularExponent = exp2(material.x * 10.0 + 1.0);
        lightCount = min(lightCount, 127u);

        float materialNormalDotView = dot(material.xyz, viewDirection);
        float materialViewSine =
            sqrt(1.0 - min(materialNormalDotView * materialNormalDotView, 1.0));
        float normalDotView = dot(normalView, viewDirection);

#if DFTILEDLIGHTING_VARIANT == 2
        float rimExponent =
            oneMinusNormalDotViewLog * 0.01;
#endif
        float sinPrimary;
        float cosPrimary;
        float sinSecondary;
        float cosSecondary;
        sincos(PerFrame[29].y, sinPrimary, cosPrimary);
        sincos(PerFrame[29].x, sinSecondary, cosSecondary);

        float3 viewTangent =
            viewDirection - normalView * normalDotView;
        float oneMinusNormalDotViewSquared =
            1.0 - normalDotView * normalDotView;
        float normalDotViewSaturated = saturate(normalDotView);
        float specularNormalization =
            (specularExponent + 2.0) * 0.15915494;
        float inverseNormalDotView = 1.0 / normalDotViewSaturated;
#if DFTILEDLIGHTING_VARIANT == 2
        float specularScale = materialSpecular * 3.1415927;
        float rimView = exp2(rimExponent);
#else
        float rimView =
            exp2(log2(1.0 - normalDotViewSaturated) * 0.01);
#endif

        [loop]
        for (uint i = 0; i < lightCount; ++i)
        {
            TiledLight light = Lights[TileLists[tileIndex].Indices[i]];
            float3 toLight = light.PositionRadius.xyz - positionView;
            float distanceSquared = dot(toLight, toLight);
            float3 diffuse = light.Color;
            float3 specular;

            if ((light.Flags & 8u) == 0)
            {
                bool ignoreRim = (light.Flags & 4u) != 0;
                float inverseDistance = rsqrt(distanceSquared);
                float3 lightDirection = toLight * inverseDistance;
                float normalDotLight = dot(normalView, lightDirection);
                float normalDotLightPositive = max(normalDotLight, 0.0);
                float3 normalDotLightSaturated =
                    min(normalDotLightPositive.xxx, 1.0.xxx);

                if (materialOne)
                {
                    float materialNormalDotLight =
                        dot(material.xyz, lightDirection);
                    float materialLightSine =
                        sqrt(1.0 - min(
                            materialNormalDotLight * materialNormalDotLight,
                            1.0));

                    float primary =
                        -materialNormalDotLight * cosPrimary -
                        materialLightSine * sinPrimary;
                    float primaryPerpendicular =
                        sqrt(1.0 - primary * primary);
                    float primaryVisibility = max(
                        primary * materialNormalDotView +
                        materialViewSine * primaryPerpendicular,
                        0.0);
                    float primaryPower =
                        exp2(log2(primaryVisibility) * PerFrame[28].w);
                    float primaryIntensity = saturate(
                        PerFrame[28].z * primaryPower +
                        normalDotLightPositive);
                    primaryIntensity = min(albedoAlpha, primaryIntensity);
                    diffuse = light.Color * primaryIntensity;

                    [branch]
                    if ((light.Flags & 16u) != 0)
                    {
                        float secondary =
                            -materialNormalDotLight * cosSecondary -
                            materialLightSine * sinSecondary;
                        float secondaryPerpendicular =
                            sqrt(1.0 - secondary * secondary);
                        float secondaryVisibility = max(
                            secondary * materialNormalDotView +
                            materialViewSine * secondaryPerpendicular,
                            0.0);
                        float secondaryPower =
                            exp2(
                                log2(secondaryVisibility) *
                                PerFrame[28].y) *
                            PerFrame[28].x;
                        specular =
                            light.Color * secondaryPower *
                            normalDotLightSaturated.z;
                    }
                    else
                    {
                        specular = 0.0;
                    }
                }
                else
                {
                    float activeRoughness =
                        (light.Flags & 2u) != 0 ? 0.0 : roughness;
                    float3 diffuseFactors;
                    [branch]
                    if (activeRoughness != 0.0)
                    {
                        float3 lightTangent =
                            lightDirection -
                            normalView * normalDotLight;
                        float tangentDot =
                            dot(viewTangent, lightTangent);

                        float roughnessSquared =
                            activeRoughness * activeRoughness;
                        float2 visibilityRatios =
                            roughnessSquared.xx /
                            (roughnessSquared.xx +
                             float2(0.57, 0.09));
                        float visibilityA = visibilityRatios.x;
                        float visibilityB = visibilityRatios.y;
                        visibilityA =
                            1.0 - 0.5 * visibilityA;
                        visibilityB *= 0.45;
                        float tangentSine = sqrt(saturate(
                            oneMinusNormalDotViewSquared *
                            (1.0 - normalDotLight * normalDotLight)));
                        float tangentRatio =
                            tangentSine /
                            max(normalDotView, normalDotLight);
                        float visibility =
                            visibilityB * max(tangentDot, 0.0) *
                            tangentRatio +
                            visibilityA;
                        diffuseFactors =
                            normalDotLightPositive * visibility;
                    }
                    else
                    {
                        diffuseFactors = normalDotLightSaturated;
                    }
                    diffuse = light.Color * diffuseFactors;

                    [branch]
                    if ((light.Flags & 16u) != 0)
                    {
                        float3 halfway =
                            lightDirection -
                            positionView * positionViewInverseLength;
                        halfway *= rsqrt(dot(halfway, halfway));
                        float viewDotHalf =
                            saturate(dot(viewDirection, halfway));
                        float normalDotHalf =
                            saturate(dot(halfway, normalView));
                        float distribution =
                            specularNormalization *
                            exp2(
                                log2(normalDotHalf) *
                                specularExponent);

                        float viewDotHalfNonnegative =
                            max(viewDotHalf, 1.1920929e-07);
                        float minimumNormal =
                            min(
                                normalDotLightSaturated.z,
                                normalDotViewSaturated);
                        float twiceNormalDotHalf =
                            normalDotHalf + normalDotHalf;
                        bool usePeakRatio =
                            viewDotHalfNonnegative >=
                            minimumNormal * twiceNormalDotHalf;
                        bool useUnityRatio =
                            normalDotViewSaturated == minimumNormal;
                        float ratio =
                            useUnityRatio ?
                            1.0 :
                            normalDotLightSaturated.z /
                            normalDotViewSaturated;
                        float geometry =
                            twiceNormalDotHalf * ratio /
                            viewDotHalfNonnegative;
                        geometry =
                            usePeakRatio ?
                            geometry :
                            inverseNormalDotView;

                        float oneMinusViewDotHalf =
                            1.0 - viewDotHalf;
                        float oneMinusViewDotHalfSquared =
                            oneMinusViewDotHalf *
                            oneMinusViewDotHalf;
                        float oneMinusViewDotHalfFourth =
                            oneMinusViewDotHalfSquared *
                            oneMinusViewDotHalfSquared;
                        float oneMinusViewDotHalfFifth =
                            oneMinusViewDotHalf *
                            oneMinusViewDotHalfFourth;
                        float fresnel =
                            (1.0 - oneMinusViewDotHalfFifth) *
                            0.2 +
                            oneMinusViewDotHalfFifth;
                        fresnel = min(fresnel, 1.0);

                        float specularMagnitude =
                            fresnel * geometry * distribution;
                        specularMagnitude *= 0.25;
                        specularMagnitude =
                            min(specularMagnitude, 15.0);
                        specularMagnitude *= specularScale;
                        specular =
                            light.Color *
                            specularMagnitude *
                            normalDotLightSaturated.z;
                    }
                    else
                    {
                        specular = 0.0;
                    }
                }

                [branch]
                if (!ignoreRim)
                {
                    float rim = saturate(
                        dot(viewDirection, -lightDirection));
                    rim *= rimView;
                    rim *= normalDotLightSaturated.z;
                    rim *= roughness;
                    diffuse += light.Color * rim;
                }
            }
            else
            {
                specular = 0.0;
            }

            float normalizedDistance = saturate(
                sqrt(distanceSquared) / light.PositionRadius.w);
            float falloffPower =
                exp2(
#if DFTILEDLIGHTING_VARIANT == 2
                    light.Attenuation.z *
                    log2(normalizedDistance));
#else
                    log2(normalizedDistance) *
                    light.Attenuation.z);
#endif
            float falloff = saturate(
                light.Attenuation.y * falloffPower +
                light.Attenuation.x);
            float attenuation =
                exp2(log2(1.0 - falloff) * 2.2);
#ifdef INVERSE_SQUARE_LIGHTING
            // FO4 forced divergence: tiled lights have no verified per-light eligibility flag.
            attenuation = InverseSquareLighting::GetAttenuation(
                attenuation,
                sqrt(distanceSquared),
                light.PositionRadius.w,
                pixel.x);
#endif

            diffuseAccum += diffuse * attenuation;
            specularAccum += specular * attenuation;
        }
    }

    diffuseAccum *= TiledParams[2].y * 0.333333343;
    DiffuseOutput[pixel] = float4(diffuseAccum, 0.0);
    SpecularOutput[pixel] = float4(specularAccum, 0.0);
}
