// SPDX-License-Identifier: GPL-3.0-or-later
#include "Common/MipBias.hlsli"
#ifdef BSLIGHTING_PS_COLOR

#ifdef BSL_BASE_BLEND_TINT
#ifndef BSL_BASE_BLEND
#error "BSL_BASE_BLEND_TINT is a modifier of BSL_BASE_BLEND and cannot be used alone"
#endif
#endif

cbuffer PerMaterial : register(b1)
{
    float4 materialData[8];
};

cbuffer PerGeometry : register(b2)
{
    float4 geometryData[13];
    float4 pointLightPosition[6];
    float4 pointLightColor[6];
    float4 shadowProjection[12];
    float4 splitDistances;
};

SamplerState diffuseSampler : register(s0);
SamplerState normalSampler : register(s1);
SamplerState smoothnessSampler : register(s2);
SamplerState shadowSampler : register(s14);

Texture2D<float4> diffuseTexture : register(t0);
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> smoothnessTexture : register(t2);
Texture2DArray<float4> shadowTexture : register(t14);

struct PSInput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 worldPosition : TEXCOORD4;
    float3 tangentX : TEXCOORD1;
    float3 tangentY : TEXCOORD2;
    float3 tangentZ : TEXCOORD3;
    float3 eyeDirection : TEXCOORD5;
    float4 color : COLOR0;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

float GetOrenNayar(
    float NdotL,
    float NdotV,
    float cosPhi,
    float sinView,
    float A,
    float B)
{
    float s = sqrt(saturate(sinView * (1.0 - NdotL * NdotL)));
    s = s / max(NdotV, NdotL);
    float lambert = max(NdotL, 0.0);
    return lambert * (B * max(cosPhi, 0.0) * s + A);
}

float GetSpecularDirectional(
    float3 viewDirection,
    float3 lightDirection,
    float3 normal,
    float NdotLSat,
    float NdotVSat,
    float specularPower,
    float specularScale)
{
    float3 halfway = normalize(viewDirection + lightDirection);
    float VdotH = saturate(dot(viewDirection, halfway));
    float NdotH = saturate(dot(halfway, normal));

    float normalization = (specularPower + 2.0) * 0.15915494;
    float distribution = normalization * pow(NdotH, specularPower);

    float denom = max(VdotH, 1.1920929e-07);
    float smaller = min(NdotLSat, NdotVSat);
    float twiceNdotH = NdotH + NdotH;
    float geometry;
    if (denom >= smaller * twiceNdotH)
    {
        float ratio = (NdotVSat == smaller) ? 1.0 : NdotLSat / NdotVSat;
        geometry = (twiceNdotH * ratio) / denom;
    }
    else
    {
        geometry = 1.0 / NdotVSat;
    }

    float f = 1.0 - VdotH;
    float fresnel = min(lerp(pow(f, 5), 1.0, 0.2), 1.0);

    return specularScale * min(fresnel * geometry * distribution * 0.25, 15.0);
}

float GetSpecularPoint(
    float3 viewDirection,
    float3 lightDirection,
    float3 normal,
    float NdotLSat,
    float NdotVSat,
    float specularPower,
    float specularScale)
{
    float3 halfway = normalize(viewDirection + lightDirection);
    float VdotH = saturate(dot(viewDirection, halfway));
    float NdotH = saturate(dot(halfway, normal));

    float normalization = (specularPower + 2.0) * 0.15915494;
    float distribution = normalization * pow(NdotH, specularPower);

    float denom = max(VdotH, 1.1920929e-07);
    float smaller = min(NdotVSat, NdotLSat);
    float twiceNdotH = NdotH + NdotH;
    float geometry;
    if (denom >= smaller * twiceNdotH)
    {
        float ratio = (NdotVSat == smaller) ? 1.0 : NdotLSat / NdotVSat;
        geometry = (twiceNdotH * ratio) / denom;
    }
    else
    {
        geometry = 1.0 / NdotVSat;
    }

    float f = 1.0 - VdotH;
    float fresnel = min(lerp(pow(f, 5), 1.0, 0.2), 1.0);

    return specularScale * min(fresnel * geometry * distribution * 0.25, 15.0);
}

PSOutput main(PSInput input)
{
    float3 viewDirection = normalize(input.eyeDirection);

    float4 baseColor = diffuseTexture.SampleBias(diffuseSampler, input.texCoord, FO4CS_MIP_BIAS);

    float2 rawNormalXY =
        normalTexture.SampleBias(normalSampler, input.texCoord, FO4CS_MIP_BIAS).xy * 2.0 - 1.0;
    float3 rawNormal =
        float3(rawNormalXY, sqrt(1.0 - min(dot(rawNormalXY, rawNormalXY), 1.0)));

    float2 material = smoothnessTexture.SampleBias(smoothnessSampler, input.texCoord, FO4CS_MIP_BIAS).xy;
    float glossiness = material.y * geometryData[11].x;
    float specularMask = material.x * geometryData[11].y;
    float translucency =
        3.0 - 3.0 / (exp2(((1.0 - material.y * geometryData[11].x) * 2.0 - 1.0) * 8.65591) + 1.0);
    float specularPower = exp2(glossiness * 10.0 + 1.0);
    float specularScale = specularMask * 3.1415927;

#ifdef BSL_BASE_BLEND
#ifdef BSL_BASE_BLEND_TINT
    float3 blendTerm = baseColor.xyz * materialData[1].xyz;
#else
    float3 blendTerm = baseColor.xyz;
#endif
    blendTerm = blendTerm + blendTerm;
    blendTerm = blendTerm - blendTerm * baseColor.xyz;
    baseColor.xyz = baseColor.xyz * baseColor.xyz + blendTerm;
#endif

    float3 tbnNormal = normalize(
        mul(float3x3(input.tangentX, input.tangentY, input.tangentZ), rawNormal));
    float flipped = input.isFrontFace ? tbnNormal.y : -tbnNormal.y;
    float3 normal = float3(tbnNormal.x, flipped, tbnNormal.z);

    float shadow;
    if (geometryData[10].x == 1.0)
    {
        if (input.worldPosition.w < splitDistances.w)
        {
            float4 row0 = shadowProjection[0];
            float4 row1 = shadowProjection[1];
            float4 row2 = shadowProjection[2];
            float cascade = 0.0;
            bool cascade2 = splitDistances.z < input.worldPosition.w;
            bool cascade1 = splitDistances.y < input.worldPosition.w;
            bool cascade0 = splitDistances.x < input.worldPosition.w;
            if (cascade0)
            {
                row0 = shadowProjection[3];
                row1 = shadowProjection[4];
                row2 = shadowProjection[5];
                cascade = 1.0;
            }
            if (cascade1)
            {
                row0 = shadowProjection[6];
                row1 = shadowProjection[7];
                row2 = shadowProjection[8];
                cascade = 2.0;
            }
            if (cascade2)
            {
                row0 = shadowProjection[9];
                row1 = shadowProjection[10];
                row2 = shadowProjection[11];
                cascade = 3.0;
            }
            float4 shadowPosition = float4(input.worldPosition.xyz, 1.0);
            float shadowU = dot(row0, shadowPosition);
            float shadowV = dot(row1, shadowPosition);
            float shadowZ = dot(row2, shadowPosition);
            shadow = shadowTexture.Sample(
                shadowSampler,
                float3(shadowU, shadowV, cascade)).x >= shadowZ ? 1.0 : 0.0;
        }
        else
        {
            shadow = 1.0;
        }
    }
    else
    {
        shadow = 1.0;
    }

    float roughness = -geometryData[11].x + 1.0;
    float NdotV = dot(viewDirection, normal);
    float NdotL = dot(geometryData[0].xyz, normal);
    float3 viewTangent = viewDirection - normal * NdotV;
    float3 lightTangent = geometryData[0].xyz - normal * NdotL;
    float cosPhi = dot(viewTangent, lightTangent);

    float roughness2 = roughness * roughness;
    float A = 1.0 - (roughness2 / (roughness2 + 0.57)) * 0.5;
    float B = (roughness2 / (roughness2 + 0.09)) * 0.45;

    float sinView = 1.0 - NdotV * NdotV;
    float3 lightsDiffuse =
        GetOrenNayar(NdotL, NdotV, cosPhi, sinView, A, B) * geometryData[1].xyz;

    float NdotLSat = min(max(NdotL, 0.0), 1.0);
    float NdotVSat = saturate(NdotV);
    float rimBase = pow(1.0 - NdotVSat, 0.01);

    float backscatter =
        NdotLSat * (rimBase * saturate(dot(viewDirection, -geometryData[0].xyz)));
    lightsDiffuse = lightsDiffuse * shadow + translucency * (backscatter * geometryData[1].xyz);

    float3 rimColor = baseColor.xyz * materialData[7].y * saturate(-NdotL);
    lightsDiffuse = geometryData[1].xyz * rimColor + lightsDiffuse;

    float softLight =
        max(saturate((NdotL + materialData[7].x) / (materialData[7].x + 1.0)) - NdotLSat, 0.0);
    lightsDiffuse = (softLight * geometryData[1].xyz) * baseColor.xyz + lightsDiffuse;
    lightsDiffuse = shadow * lightsDiffuse;

    float specular = GetSpecularDirectional(
        viewDirection, geometryData[0].xyz, normal,
        NdotLSat, NdotVSat, specularPower, specularScale);
    float3 lightsSpecular = shadow * (NdotLSat * (specular * geometryData[1].xyz));

    float lightCount =
        max(min(pointLightColor[0].w - frac(pointLightColor[0].w), 20.0), 0.0);
    if (0.0 < lightCount)
    {
        for (int lightIndex = 0; lightIndex < lightCount; lightIndex++)
        {
            float3 lightVector =
                pointLightPosition[lightIndex].xyz - input.worldPosition.xyz;
            float lightLengthSq = dot(lightVector, lightVector);
            float attenuation =
                saturate(sqrt(lightLengthSq) / pointLightPosition[lightIndex].w);
            attenuation = pow(1.0 - attenuation * attenuation, 2.2);
            float3 lightDirection = lightVector * rsqrt(lightLengthSq);

            float pointNdotL = dot(lightDirection, normal);
            float3 pointTangent = lightDirection - normal * pointNdotL;
            float pointCosPhi = dot(viewTangent, pointTangent);
            float pointDiffuse =
                GetOrenNayar(pointNdotL, NdotV, pointCosPhi, sinView, A, B);

            float3 pointDiffuseColor = pointDiffuse * pointLightColor[lightIndex].xyz;
            float attenuationSq = attenuation * attenuation;
            lightsDiffuse = attenuationSq * pointDiffuseColor + lightsDiffuse;

            float pointNdotLSat = min(max(pointNdotL, 0.0), 1.0);
            float pointBack =
                pointNdotLSat * (rimBase * saturate(dot(viewDirection, -lightDirection)));
            lightsDiffuse = (translucency * (pointBack * pointLightColor[lightIndex].xyz)) *
                            attenuation + lightsDiffuse;

            float pointSpecular = GetSpecularPoint(
                viewDirection, lightDirection, normal,
                pointNdotLSat, NdotVSat, specularPower, specularScale);
            lightsSpecular = attenuation *
                             (pointNdotLSat * (pointSpecular * pointLightColor[lightIndex].xyz));
        }
    }

    float3 emitColor = geometryData[3].yzw;

    float3 accumulated = lightsDiffuse + emitColor;

    PSOutput psout;
#ifdef BSL_VERTEX_TINT
    psout.color.xyz = accumulated * baseColor.xyz *
                      lerp(1.0.xxx, materialData[1].xyz, input.color.y) + lightsSpecular;
#else
    psout.color.xyz = accumulated * baseColor.xyz * input.color.xyz + lightsSpecular;
#endif

#ifdef BSL_NO_VERTEX_ALPHA
    clip(baseColor.w - geometryData[3].x);
#else
    clip(baseColor.w * input.color.w - geometryData[3].x);
#endif

    psout.color.w = geometryData[2].z;
    return psout;
}
#endif

#ifdef BSLIGHTING_PS_CORE

cbuffer PerMaterial : register(b1)
{
    float4 materialData[8];
};

cbuffer PerGeometry : register(b2)
{
    float4 geometryData[13];
    float4 pointLightPosition[6];
    float4 pointLightColor[6];
    float4 shadowProjection[12];
    float4 splitDistances;
};

SamplerState diffuseSampler : register(s0);
SamplerState normalSampler : register(s1);
SamplerState smoothnessSampler : register(s2);
#ifdef BSL_ENVMAP
SamplerState environmentSampler : register(s4);
#endif
#ifdef BSL_GLOWMAP
SamplerState glowSampler : register(s6);
#endif
SamplerState shadowSampler : register(s14);

Texture2D<float4> diffuseTexture : register(t0);
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> smoothnessTexture : register(t2);
#ifdef BSL_ENVMAP
TextureCube<float4> environmentTexture : register(t4);
#endif
#ifdef BSL_GLOWMAP
Texture2D<float4> glowTexture : register(t6);
#endif
Texture2DArray<float4> shadowTexture : register(t14);

struct PSInput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 worldPosition : TEXCOORD4;
#ifndef BSL_REDUCED_NORMAL
    float3 tangentX : TEXCOORD1;
    float3 tangentY : TEXCOORD2;
    float3 tangentZ : TEXCOORD3;
#endif
    float3 eyeDirection : TEXCOORD5;
    float4 color : COLOR0;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

float GetOrenNayar(
    float NdotL,
    float NdotV,
    float cosPhi,
    float sinView,
    float A,
    float B)
{
    float s = sqrt(saturate(sinView * (1.0 - NdotL * NdotL)));
    s = s / max(NdotV, NdotL);
    float lambert = max(NdotL, 0.0);
    return lambert * (B * max(cosPhi, 0.0) * s + A);
}

float GetSpecularDirectional(
    float3 viewDirection,
    float3 lightDirection,
    float3 normal,
    float NdotLSat,
    float NdotVSat,
    float specularPower,
    float specularScale)
{
    float3 halfway = normalize(viewDirection + lightDirection);
    float VdotH = saturate(dot(viewDirection, halfway));
    float NdotH = saturate(dot(halfway, normal));

    float normalization = (specularPower + 2.0) * 0.15915494;
    float distribution = normalization * pow(NdotH, specularPower);

    float denom = max(VdotH, 1.1920929e-07);
    float smaller = min(NdotLSat, NdotVSat);
    float twiceNdotH = NdotH + NdotH;
    float geometry;
    if (denom >= smaller * twiceNdotH)
    {
        float ratio = (NdotVSat == smaller) ? 1.0 : NdotLSat / NdotVSat;
        geometry = (twiceNdotH * ratio) / denom;
    }
    else
    {
        geometry = 1.0 / NdotVSat;
    }

    float f = 1.0 - VdotH;
    float fresnel = min(lerp(pow(f, 5), 1.0, 0.2), 1.0);

    return specularScale * min(fresnel * geometry * distribution * 0.25, 15.0);
}

float GetSpecularPoint(
    float3 viewDirection,
    float3 lightDirection,
    float3 normal,
    float NdotLSat,
    float NdotVSat,
    float specularPower,
    float specularScale)
{
    float3 halfway = normalize(viewDirection + lightDirection);
    float VdotH = saturate(dot(viewDirection, halfway));
    float NdotH = saturate(dot(halfway, normal));

    float normalization = (specularPower + 2.0) * 0.15915494;
    float distribution = normalization * pow(NdotH, specularPower);

    float denom = max(VdotH, 1.1920929e-07);
    float smaller = min(NdotVSat, NdotLSat);
    float twiceNdotH = NdotH + NdotH;
    float geometry;
    if (denom >= smaller * twiceNdotH)
    {
        float ratio = (NdotVSat == smaller) ? 1.0 : NdotLSat / NdotVSat;
        geometry = (twiceNdotH * ratio) / denom;
    }
    else
    {
        geometry = 1.0 / NdotVSat;
    }

    float f = 1.0 - VdotH;
    float fresnel = min(lerp(pow(f, 5), 1.0, 0.2), 1.0);

    return specularScale * min(fresnel * geometry * distribution * 0.25, 15.0);
}

PSOutput main(PSInput input)
{
    float3 viewDirection = normalize(input.eyeDirection);

    float4 baseColor = diffuseTexture.SampleBias(diffuseSampler, input.texCoord, FO4CS_MIP_BIAS);

#ifdef BSL_REDUCED_NORMAL
    float3 rawNormal =
        normalTexture.SampleBias(normalSampler, input.texCoord, FO4CS_MIP_BIAS).xyz * 2.0 - 1.0;
#else
    float2 rawNormalXY =
        normalTexture.SampleBias(normalSampler, input.texCoord, FO4CS_MIP_BIAS).xy * 2.0 - 1.0;
    float3 rawNormal =
        float3(rawNormalXY, sqrt(1.0 - min(dot(rawNormalXY, rawNormalXY), 1.0)));
#endif

    float2 material = smoothnessTexture.SampleBias(smoothnessSampler, input.texCoord, FO4CS_MIP_BIAS).xy;
    float glossiness = material.y * geometryData[11].x;
    float specularMask = material.x * geometryData[11].y;
    float translucency =
        3.0 - 3.0 / (exp2(((1.0 - material.y * geometryData[11].x) * 2.0 - 1.0) * 8.65591) + 1.0);
    float specularPower = exp2(glossiness * 10.0 + 1.0);
    float specularScale = specularMask * 3.1415927;

#ifdef BSL_REDUCED_NORMAL
    float flipped = input.isFrontFace ? rawNormal.z : -rawNormal.z;
    float3 normal = float3(rawNormal.x, flipped, rawNormal.y);
#else
    float3 tbnNormal = normalize(
        mul(float3x3(input.tangentX, input.tangentY, input.tangentZ), rawNormal));
    float flipped = input.isFrontFace ? tbnNormal.y : -tbnNormal.y;
    float3 normal = float3(tbnNormal.x, flipped, tbnNormal.z);
#endif

    float shadow;
    if (geometryData[10].x == 1.0)
    {
        if (input.worldPosition.w < splitDistances.w)
        {
            float4 row0 = shadowProjection[0];
            float4 row1 = shadowProjection[1];
            float4 row2 = shadowProjection[2];
            float cascade = 0.0;
            bool cascade2 = splitDistances.z < input.worldPosition.w;
            bool cascade1 = splitDistances.y < input.worldPosition.w;
            bool cascade0 = splitDistances.x < input.worldPosition.w;
            if (cascade0)
            {
                row0 = shadowProjection[3];
                row1 = shadowProjection[4];
                row2 = shadowProjection[5];
                cascade = 1.0;
            }
            if (cascade1)
            {
                row0 = shadowProjection[6];
                row1 = shadowProjection[7];
                row2 = shadowProjection[8];
                cascade = 2.0;
            }
            if (cascade2)
            {
                row0 = shadowProjection[9];
                row1 = shadowProjection[10];
                row2 = shadowProjection[11];
                cascade = 3.0;
            }
            float4 shadowPosition = float4(input.worldPosition.xyz, 1.0);
            float shadowU = dot(row0, shadowPosition);
            float shadowV = dot(row1, shadowPosition);
            float shadowZ = dot(row2, shadowPosition);
            shadow = shadowTexture.Sample(
                shadowSampler,
                float3(shadowU, shadowV, cascade)).x >= shadowZ ? 1.0 : 0.0;
        }
        else
        {
            shadow = 1.0;
        }
    }
    else
    {
        shadow = 1.0;
    }

    float roughness = -geometryData[11].x + 1.0;
    float NdotV = dot(viewDirection, normal);
    float NdotL = dot(geometryData[0].xyz, normal);
    float3 viewTangent = viewDirection - normal * NdotV;
    float3 lightTangent = geometryData[0].xyz - normal * NdotL;
    float cosPhi = dot(viewTangent, lightTangent);

    float roughness2 = roughness * roughness;
    float A = 1.0 - (roughness2 / (roughness2 + 0.57)) * 0.5;
    float B = (roughness2 / (roughness2 + 0.09)) * 0.45;

    float sinView = 1.0 - NdotV * NdotV;
    float3 lightsDiffuse =
        GetOrenNayar(NdotL, NdotV, cosPhi, sinView, A, B) * geometryData[1].xyz;

    float NdotLSat = min(max(NdotL, 0.0), 1.0);
    float NdotVSat = saturate(NdotV);
    float rimBase = pow(1.0 - NdotVSat, 0.01);

    float backscatter =
        NdotLSat * (rimBase * saturate(dot(viewDirection, -geometryData[0].xyz)));
    lightsDiffuse = lightsDiffuse * shadow + translucency * (backscatter * geometryData[1].xyz);

#ifdef BSL_ENVMAP
    float3 rimColor = lerp(
        0.0.xxx,
        baseColor.xyz * materialData[7].y,
        saturate(-NdotL));
#else
    float3 rimColor = baseColor.xyz * materialData[7].y * saturate(-NdotL);
#endif
    lightsDiffuse = geometryData[1].xyz * rimColor + lightsDiffuse;

    float softLight =
        max(saturate((NdotL + materialData[7].x) / (materialData[7].x + 1.0)) - NdotLSat, 0.0);
    lightsDiffuse = (softLight * geometryData[1].xyz) * baseColor.xyz + lightsDiffuse;
    lightsDiffuse = shadow * lightsDiffuse;

    float specular = GetSpecularDirectional(
        viewDirection, geometryData[0].xyz, normal,
        NdotLSat, NdotVSat, specularPower, specularScale);
    float3 lightsSpecular = shadow * (NdotLSat * (specular * geometryData[1].xyz));

    float lightCount =
        max(min(pointLightColor[0].w - frac(pointLightColor[0].w), 20.0), 0.0);
    if (0.0 < lightCount)
    {
        for (int lightIndex = 0; lightIndex < lightCount; lightIndex++)
        {
            float3 lightVector =
                pointLightPosition[lightIndex].xyz - input.worldPosition.xyz;
            float lightLengthSq = dot(lightVector, lightVector);
            float attenuation =
                saturate(sqrt(lightLengthSq) / pointLightPosition[lightIndex].w);
            attenuation = pow(1.0 - attenuation * attenuation, 2.2);
            float3 lightDirection = lightVector * rsqrt(lightLengthSq);

            float pointNdotL = dot(lightDirection, normal);
            float3 pointTangent = lightDirection - normal * pointNdotL;
            float pointCosPhi = dot(viewTangent, pointTangent);
            float pointDiffuse =
                GetOrenNayar(pointNdotL, NdotV, pointCosPhi, sinView, A, B);

            float3 pointDiffuseColor = pointDiffuse * pointLightColor[lightIndex].xyz;
            float attenuationSq = attenuation * attenuation;
            lightsDiffuse = attenuationSq * pointDiffuseColor + lightsDiffuse;

            float pointNdotLSat = min(max(pointNdotL, 0.0), 1.0);
            float pointBack =
                pointNdotLSat * (rimBase * saturate(dot(viewDirection, -lightDirection)));
            lightsDiffuse = (translucency * (pointBack * pointLightColor[lightIndex].xyz)) *
                            attenuation + lightsDiffuse;

            float pointSpecular = GetSpecularPoint(
                viewDirection, lightDirection, normal,
                pointNdotLSat, NdotVSat, specularPower, specularScale);
            lightsSpecular = attenuation *
                             (pointNdotLSat * (pointSpecular * pointLightColor[lightIndex].xyz));
        }
    }

#ifdef BSL_GLOWMAP
    float3 emitColor =
        geometryData[3].yzw * glowTexture.SampleBias(glowSampler, input.texCoord, FO4CS_MIP_BIAS).xyz;
#else
    float3 emitColor = geometryData[3].yzw;
#endif

#ifdef BSL_ENVMAP
    float envLod = input.position.z * 0.001953125 + (1.0 - material.y) * 6.0;
    float envMask = material.x * 3.0;
    float envStrength =
        envMask * min(1.0 / rsqrt(saturate(material.y - 0.3)), 1.0) * geometryData[11].y;
    float3 envReflect = -(2.0 * NdotV * normal - viewDirection);
    float3 envColor = environmentTexture.SampleLevel(
        environmentSampler, envReflect, envLod).xyz * envStrength * materialData[2].x;
#endif

    float3 accumulated = lightsDiffuse + emitColor;

    PSOutput psout;
    psout.color.xyz = accumulated * baseColor.xyz * input.color.xyz + lightsSpecular;

#ifdef BSL_ENVMAP
    psout.color.xyz = envColor * accumulated + psout.color.xyz;
#endif

    clip(baseColor.w * input.color.w - geometryData[3].x);

    psout.color.w = geometryData[2].z;
    return psout;
}
#endif

#ifdef BSLIGHTING_PS_RESOURCE

#ifdef BSL_VERTEX_TINT
#ifndef BSL_BASE_LUT
#error "BSL_VERTEX_TINT is a modifier of BSL_BASE_LUT in this file and cannot be used alone"
#endif
#endif

#ifdef BSL_OVERLAY
cbuffer PerFrame : register(b0)
{
    float4 frameData[1];
};
#endif

cbuffer PerMaterial : register(b1)
{
    float4 materialData[8];
};

cbuffer PerGeometry : register(b2)
{
    float4 geometryData[13];
    float4 pointLightPosition[6];
    float4 pointLightColor[6];
    float4 shadowProjection[12];
    float4 splitDistances;
};

SamplerState diffuseSampler : register(s0);
SamplerState normalSampler : register(s1);
SamplerState smoothnessSampler : register(s2);
#ifdef BSL_ENVMAP
SamplerState environmentSampler : register(s4);
#endif
#ifdef BSL_OVERLAY
SamplerState overlaySampler : register(s12);
#endif
SamplerState shadowSampler : register(s14);
#ifdef BSL_BASE_LUT
SamplerState baseLookupSampler : register(s15);
#endif

Texture2D<float4> diffuseTexture : register(t0);
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> smoothnessTexture : register(t2);
#ifdef BSL_ENVMAP
TextureCube<float4> environmentTexture : register(t4);
#endif
#ifdef BSL_OVERLAY
Texture2D<float4> overlayTexture : register(t12);
#endif
Texture2DArray<float4> shadowTexture : register(t14);
#ifdef BSL_BASE_LUT
Texture2D<float4> baseLookupTexture : register(t15);
#endif

struct PSInput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 worldPosition : TEXCOORD4;
    float3 tangentX : TEXCOORD1;
    float3 tangentY : TEXCOORD2;
    float3 tangentZ : TEXCOORD3;
    float3 eyeDirection : TEXCOORD5;
    float4 color : COLOR0;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

float GetOrenNayar(
    float NdotL,
    float NdotV,
    float cosPhi,
    float sinView,
    float A,
    float B)
{
    float s = sqrt(saturate(sinView * (1.0 - NdotL * NdotL)));
    s = s / max(NdotV, NdotL);
    float lambert = max(NdotL, 0.0);
    return lambert * (B * max(cosPhi, 0.0) * s + A);
}

float GetSpecularDirectional(
    float3 viewDirection,
    float3 lightDirection,
    float3 normal,
    float NdotLSat,
    float NdotVSat,
    float specularPower,
    float specularScale)
{
    float3 halfway = normalize(viewDirection + lightDirection);
    float VdotH = saturate(dot(viewDirection, halfway));
    float NdotH = saturate(dot(halfway, normal));

    float normalization = (specularPower + 2.0) * 0.15915494;
    float distribution = normalization * pow(NdotH, specularPower);

    float denom = max(VdotH, 1.1920929e-07);
    float smaller = min(NdotLSat, NdotVSat);
    float twiceNdotH = NdotH + NdotH;
    float geometry;
    if (denom >= smaller * twiceNdotH)
    {
        float ratio = (NdotVSat == smaller) ? 1.0 : NdotLSat / NdotVSat;
        geometry = (twiceNdotH * ratio) / denom;
    }
    else
    {
        geometry = 1.0 / NdotVSat;
    }

    float f = 1.0 - VdotH;
    float fresnel = min(lerp(pow(f, 5), 1.0, 0.2), 1.0);

    return specularScale * min(fresnel * geometry * distribution * 0.25, 15.0);
}

float GetSpecularPoint(
    float3 viewDirection,
    float3 lightDirection,
    float3 normal,
    float NdotLSat,
    float NdotVSat,
    float specularPower,
    float specularScale)
{
    float3 halfway = normalize(viewDirection + lightDirection);
    float VdotH = saturate(dot(viewDirection, halfway));
    float NdotH = saturate(dot(halfway, normal));

    float normalization = (specularPower + 2.0) * 0.15915494;
    float distribution = normalization * pow(NdotH, specularPower);

    float denom = max(VdotH, 1.1920929e-07);
    float smaller = min(NdotVSat, NdotLSat);
    float twiceNdotH = NdotH + NdotH;
    float geometry;
    if (denom >= smaller * twiceNdotH)
    {
        float ratio = (NdotVSat == smaller) ? 1.0 : NdotLSat / NdotVSat;
        geometry = (twiceNdotH * ratio) / denom;
    }
    else
    {
        geometry = 1.0 / NdotVSat;
    }

    float f = 1.0 - VdotH;
    float fresnel = min(lerp(pow(f, 5), 1.0, 0.2), 1.0);

    return specularScale * min(fresnel * geometry * distribution * 0.25, 15.0);
}

PSOutput main(PSInput input)
{
    float3 viewDirection = normalize(input.eyeDirection);

    float4 baseColor = diffuseTexture.SampleBias(diffuseSampler, input.texCoord, FO4CS_MIP_BIAS);

    float2 rawNormalXY =
        normalTexture.SampleBias(normalSampler, input.texCoord, FO4CS_MIP_BIAS).xy * 2.0 - 1.0;
    float3 rawNormal =
        float3(rawNormalXY, sqrt(1.0 - min(dot(rawNormalXY, rawNormalXY), 1.0)));

#ifdef BSL_BASE_LUT
    float vertexGamma = pow(input.color.x, 0.45454547);
    float lookupU = pow(baseColor.y, 0.45454547);
    float lookupV = materialData[1].x - (1.0 - vertexGamma);
    float3 baseTint = baseLookupTexture.SampleLevel(
        baseLookupSampler, float2(lookupU, lookupV), 0.0).xyz;
#else
    float3 baseTint = baseColor.xyz;
#endif

    float2 material = smoothnessTexture.SampleBias(smoothnessSampler, input.texCoord, FO4CS_MIP_BIAS).xy;
    float glossiness = material.y * geometryData[11].x;
    float specularMask = material.x * geometryData[11].y;
    float translucency =
        3.0 - 3.0 / (exp2(((1.0 - material.y * geometryData[11].x) * 2.0 - 1.0) * 8.65591) + 1.0);
    float specularPower = exp2(glossiness * 10.0 + 1.0);
    float specularScale = specularMask * 3.1415927;

    float3 tbnNormal = normalize(
        mul(float3x3(input.tangentX, input.tangentY, input.tangentZ), rawNormal));
    float flipped = input.isFrontFace ? tbnNormal.y : -tbnNormal.y;
    float3 normal = float3(tbnNormal.x, flipped, tbnNormal.z);

    float shadow;
    if (geometryData[10].x == 1.0)
    {
        if (input.worldPosition.w < splitDistances.w)
        {
            float4 row0 = shadowProjection[0];
            float4 row1 = shadowProjection[1];
            float4 row2 = shadowProjection[2];
            float cascade = 0.0;
            bool cascade2 = splitDistances.z < input.worldPosition.w;
            bool cascade1 = splitDistances.y < input.worldPosition.w;
            bool cascade0 = splitDistances.x < input.worldPosition.w;
            if (cascade0)
            {
                row0 = shadowProjection[3];
                row1 = shadowProjection[4];
                row2 = shadowProjection[5];
                cascade = 1.0;
            }
            if (cascade1)
            {
                row0 = shadowProjection[6];
                row1 = shadowProjection[7];
                row2 = shadowProjection[8];
                cascade = 2.0;
            }
            if (cascade2)
            {
                row0 = shadowProjection[9];
                row1 = shadowProjection[10];
                row2 = shadowProjection[11];
                cascade = 3.0;
            }
            float4 shadowPosition = float4(input.worldPosition.xyz, 1.0);
            float shadowU = dot(row0, shadowPosition);
            float shadowV = dot(row1, shadowPosition);
            float shadowZ = dot(row2, shadowPosition);
            shadow = shadowTexture.Sample(
                shadowSampler,
                float3(shadowU, shadowV, cascade)).x >= shadowZ ? 1.0 : 0.0;
        }
        else
        {
            shadow = 1.0;
        }
    }
    else
    {
        shadow = 1.0;
    }

    float roughness = -geometryData[11].x + 1.0;
    float NdotV = dot(viewDirection, normal);
    float NdotL = dot(geometryData[0].xyz, normal);
    float3 viewTangent = viewDirection - normal * NdotV;
    float3 lightTangent = geometryData[0].xyz - normal * NdotL;
    float cosPhi = dot(viewTangent, lightTangent);

    float roughness2 = roughness * roughness;
    float A = 1.0 - (roughness2 / (roughness2 + 0.57)) * 0.5;
    float B = (roughness2 / (roughness2 + 0.09)) * 0.45;

    float sinView = 1.0 - NdotV * NdotV;
    float3 lightsDiffuse =
        GetOrenNayar(NdotL, NdotV, cosPhi, sinView, A, B) * geometryData[1].xyz;

    float NdotLSat = min(max(NdotL, 0.0), 1.0);
    float NdotVSat = saturate(NdotV);
    float rimBase = pow(1.0 - NdotVSat, 0.01);

    float backscatter =
        NdotLSat * (rimBase * saturate(dot(viewDirection, -geometryData[0].xyz)));
    lightsDiffuse = lightsDiffuse * shadow + translucency * (backscatter * geometryData[1].xyz);

#ifdef BSL_ENVMAP
    float3 rimColor = lerp(
        0.0.xxx,
        baseTint * materialData[7].y,
        saturate(-NdotL));
#else
    float3 rimColor = baseTint * materialData[7].y * saturate(-NdotL);
#endif
    lightsDiffuse = geometryData[1].xyz * rimColor + lightsDiffuse;

    float softLight =
        max(saturate((NdotL + materialData[7].x) / (materialData[7].x + 1.0)) - NdotLSat, 0.0);
    lightsDiffuse = (softLight * geometryData[1].xyz) * baseTint + lightsDiffuse;
    lightsDiffuse = shadow * lightsDiffuse;

    float specular = GetSpecularDirectional(
        viewDirection, geometryData[0].xyz, normal,
        NdotLSat, NdotVSat, specularPower, specularScale);
    float3 lightsSpecular = shadow * (NdotLSat * (specular * geometryData[1].xyz));

    float lightCount =
        max(min(pointLightColor[0].w - frac(pointLightColor[0].w), 20.0), 0.0);
    if (0.0 < lightCount)
    {
        for (int lightIndex = 0; lightIndex < lightCount; lightIndex++)
        {
            float3 lightVector =
                pointLightPosition[lightIndex].xyz - input.worldPosition.xyz;
            float lightLengthSq = dot(lightVector, lightVector);
            float attenuation =
                saturate(sqrt(lightLengthSq) / pointLightPosition[lightIndex].w);
            attenuation = pow(1.0 - attenuation * attenuation, 2.2);
            float3 lightDirection = lightVector * rsqrt(lightLengthSq);

            float pointNdotL = dot(lightDirection, normal);
            float3 pointTangent = lightDirection - normal * pointNdotL;
            float pointCosPhi = dot(viewTangent, pointTangent);
            float pointDiffuse =
                GetOrenNayar(pointNdotL, NdotV, pointCosPhi, sinView, A, B);

            float3 pointDiffuseColor = pointDiffuse * pointLightColor[lightIndex].xyz;
            float attenuationSq = attenuation * attenuation;
            lightsDiffuse = attenuationSq * pointDiffuseColor + lightsDiffuse;

            float pointNdotLSat = min(max(pointNdotL, 0.0), 1.0);
            float pointBack =
                pointNdotLSat * (rimBase * saturate(dot(viewDirection, -lightDirection)));
            lightsDiffuse = (translucency * (pointBack * pointLightColor[lightIndex].xyz)) *
                            attenuation + lightsDiffuse;

            float pointSpecular = GetSpecularPoint(
                viewDirection, lightDirection, normal,
                pointNdotLSat, NdotVSat, specularPower, specularScale);
            lightsSpecular = attenuation *
                             (pointNdotLSat * (pointSpecular * pointLightColor[lightIndex].xyz));
        }
    }

    float3 emitColor = geometryData[3].yzw;

#ifdef BSL_ENVMAP
    float envLod = input.position.z * 0.001953125 + (1.0 - material.y) * 6.0;
    float envMask = material.x * 3.0;
    float envStrength =
        envMask * min(1.0 / rsqrt(saturate(material.y - 0.3)), 1.0) * geometryData[11].y;
    float3 envReflect = -(2.0 * NdotV * normal - viewDirection);
    float3 envColor = environmentTexture.SampleLevel(
        environmentSampler, envReflect, envLod).xyz * envStrength * materialData[2].x;
#endif

    float3 accumulated = lightsDiffuse + emitColor;

    PSOutput psout;
#ifdef BSL_BASE_LUT
#ifdef BSL_VERTEX_TINT
    psout.color.xyz = accumulated * baseTint *
                      lerp(1.0.xxx, materialData[1].xyz, input.color.y) + lightsSpecular;
#else
    psout.color.xyz = accumulated * baseTint + lightsSpecular;
#endif
#else
    psout.color.xyz = accumulated * baseTint * input.color.xyz + lightsSpecular;
#endif

#ifdef BSL_ENVMAP
    psout.color.xyz = envColor * accumulated + psout.color.xyz;
#endif

    clip(baseColor.w * input.color.w - geometryData[3].x);

#ifdef BSL_OVERLAY
    float4 overlay = overlayTexture.SampleBias(overlaySampler, input.texCoord, FO4CS_MIP_BIAS);
    float3 overlayColor = pow(overlay.xyz, 2.2) * overlay.w;
    float overlayBlend = 1.0 - overlay.w * frameData[0].x;
    psout.color.xyz = psout.color.xyz * overlayBlend + overlayColor;
#endif

    psout.color.w = geometryData[2].z;
    return psout;
}
#endif

#ifdef BSLIGHTING_VS_DISPLACED

cbuffer PerMaterial : register(b1)
{
    float4 materialData[3];
};

cbuffer PerGeometry : register(b2)
{
    float4 geometryData[13];
};

struct VSInput
{
    float4 position : POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 normal : NORMAL0;
    float4 binormal : BINORMAL0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 worldPosition : TEXCOORD4;
    float3 tangentX : TEXCOORD1;
    float3 tangentY : TEXCOORD2;
    float3 tangentZ : TEXCOORD3;
    float3 eyeDirection : TEXCOORD5;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    const float phase = geometryData[12].w * geometryData[12].y * geometryData[12].x;
    const float offset = dot(input.position.xyz, 1.0);
    float2 wave =
        frac(phase * float2(0.1, 0.25) + offset + 0.5) * 2.0 - 1.0;
    wave = abs(wave);
    wave = wave * wave * (3.0 - 2.0 * wave);

    const float displacement =
        (wave.x + wave.y * 0.1) * (input.color.a * geometryData[12].z);
    const float3 displaced =
        input.position.xyz + (input.normal.xyz * 2.0 - 1.0) * displacement;

    const float4 position = float4(displaced, 1.0);
    const float depth = dot(geometryData[2], position);

    output.position = float4(
        dot(geometryData[0], position),
        dot(geometryData[1], position),
        depth,
        dot(geometryData[3], position));
    output.texCoord =
        input.texCoord * materialData[2].zw + materialData[2].xy;
    output.worldPosition = float4(displaced, depth);
    output.tangentX = float3(
        input.position.w,
        input.binormal.x * 2.0 - 1.0,
        input.normal.x * 2.0 - 1.0);
    output.tangentY.xz = input.normal.wy * 2.0 - 1.0;
    output.tangentY.y = input.binormal.y * 2.0 - 1.0;
    output.tangentZ = float3(
        input.binormal.w * 2.0 - 1.0,
        input.binormal.z * 2.0 - 1.0,
        input.normal.z * 2.0 - 1.0);
    output.eyeDirection = geometryData[7].xyz - displaced;
    output.color = float4(pow(input.color.rgb, 2.2), input.color.a);
    return output;
}
#endif

#ifdef BSLIGHTING_VS_REDUCED

cbuffer PerMaterial : register(b1)
{
    float4 materialData[3];
};

cbuffer PerGeometry : register(b2)
{
    float4 geometryData[8];
};

struct VSInput
{
    float4 position : POSITION0;
    float2 texCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 worldPosition : TEXCOORD4;
    float3 eyeDirection : TEXCOORD5;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    const float4 position = float4(input.position.xyz, 1.0);
    const float depth = dot(geometryData[2], position);

    output.position = float4(
        dot(geometryData[0], position),
        dot(geometryData[1], position),
        depth,
        dot(geometryData[3], position));
    output.texCoord =
        input.texCoord * materialData[2].zw + materialData[2].xy;
    output.worldPosition = float4(input.position.xyz, depth);
    output.eyeDirection = geometryData[7].xyz - input.position.xyz;
    output.color = 1.0;
    return output;
}
#endif

#ifdef BSLIGHTING_VS_SKINNED

cbuffer PerFrame : register(b12)
{
    float4 frameData[36];
};

cbuffer PerMaterial : register(b1)
{
    float4 materialData[3];
};

cbuffer PerGeometry : register(b2)
{
    float4 geometryData[4];
};

cbuffer PerSkin : register(b10)
{
    float4 boneData[180];
};

struct VSInput
{
    float4 position : POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 normal : NORMAL0;
    float4 binormal : BINORMAL0;
#ifdef BSL_VERTEX_COLOR
    float4 color : COLOR0;
#endif
    float4 blendWeight : BLENDWEIGHT0;
    float4 blendIndices : BLENDINDICES0;
};

struct VSOutput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 worldPosition : TEXCOORD4;
    float3 tangentX : TEXCOORD1;
    float3 tangentY : TEXCOORD2;
    float3 tangentZ : TEXCOORD3;
    float3 eyeDirection : TEXCOORD5;
    float4 color : COLOR0;
};

float3x4 boneMatrix(int row, float3 origin)
{
    return float3x4(
        boneData[row] - float4(0.0, 0.0, 0.0, origin.x),
        boneData[row + 1] - float4(0.0, 0.0, 0.0, origin.y),
        boneData[row + 2] - float4(0.0, 0.0, 0.0, origin.z));
}

float3 transformPoint(float3x4 transform, float4 value)
{
    return float3(
        dot(value, transform[0]),
        dot(value, transform[1]),
        dot(value, transform[2]));
}

float3 transformAxis(float3x4 transform, float3 axis)
{
    return normalize(float3(
        dot(axis, transform[0].xyz),
        dot(axis, transform[1].xyz),
        dot(axis, transform[2].xyz)));
}

VSOutput main(VSInput input)
{
    VSOutput output;

    const float4 weights = float4(
        input.blendWeight.xyz,
        1.0 - saturate(input.blendWeight.x + input.blendWeight.y +
                       input.blendWeight.z));
    const int4 rows = (int4)(765.010010 * input.blendIndices);
    const float3 origin = frameData[35].xyz;
    const float3x4 boneTransform =
        weights.x * boneMatrix(rows.x, origin) +
        weights.y * boneMatrix(rows.y, origin) +
        weights.z * boneMatrix(rows.z, origin) +
        weights.w * boneMatrix(rows.w, origin);

    const float3 skinnedPosition =
        transformPoint(boneTransform, float4(input.position.xyz, 1.0));
    const float4 position = float4(skinnedPosition, 1.0);
    const float depth = dot(frameData[10], position);

    output.position = float4(
        dot(frameData[8], position),
        dot(frameData[9], position),
        depth,
        dot(frameData[11], position));
    output.texCoord =
        input.texCoord * materialData[2].zw + materialData[2].xy;
    output.worldPosition = float4(skinnedPosition, depth);
    output.eyeDirection = geometryData[3].xyz - skinnedPosition;

    const float3 axisX = float3(
        input.position.w,
        input.normal.w * 2.0 - 1.0,
        input.binormal.w * 2.0 - 1.0);
    const float3 axisY = input.binormal.xyz * 2.0 - 1.0;
    const float3 axisZ = input.normal.xyz * 2.0 - 1.0;
    const float3 skinnedAxisX = transformAxis(boneTransform, axisX);
    const float3 skinnedAxisY = transformAxis(boneTransform, axisY);
    const float3 skinnedAxisZ = transformAxis(boneTransform, axisZ);

    output.tangentX = float3(skinnedAxisX.x, skinnedAxisY.x, skinnedAxisZ.x);
    output.tangentY = float3(skinnedAxisX.y, skinnedAxisY.y, skinnedAxisZ.y);
    output.tangentZ = float3(skinnedAxisX.z, skinnedAxisY.z, skinnedAxisZ.z);
#ifdef BSL_VERTEX_COLOR
    output.color = float4(pow(input.color.rgb, 2.2), input.color.a);
#else
    output.color = 1.0;
#endif
    return output;
}
#endif

#ifdef BSLIGHTING_VS_STATIC

cbuffer PerMaterial : register(b1)
{
    float4 materialData[3];
};

cbuffer PerGeometry : register(b2)
{
    float4 geometryData[8];
};

struct VSInput
{
    float4 position : POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 normal : NORMAL0;
    float4 binormal : BINORMAL0;
#ifdef BSL_VERTEX_COLOR
    float4 color : COLOR0;
#endif
};

struct VSOutput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 worldPosition : TEXCOORD4;
    float3 tangentX : TEXCOORD1;
    float3 tangentY : TEXCOORD2;
    float3 tangentZ : TEXCOORD3;
    float3 eyeDirection : TEXCOORD5;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    const float4 position = float4(input.position.xyz, 1.0);
    const float depth = dot(geometryData[2], position);

    output.position = float4(
        dot(geometryData[0], position),
        dot(geometryData[1], position),
        depth,
        dot(geometryData[3], position));
    output.texCoord =
        input.texCoord * materialData[2].zw + materialData[2].xy;
    output.worldPosition = float4(input.position.xyz, depth);
    output.tangentX = float3(
        input.position.w,
        input.binormal.x * 2.0 - 1.0,
        input.normal.x * 2.0 - 1.0);
    output.tangentY.xz = input.normal.wy * 2.0 - 1.0;
    output.tangentY.y = input.binormal.y * 2.0 - 1.0;
    output.tangentZ = float3(
        input.binormal.w * 2.0 - 1.0,
        input.binormal.z * 2.0 - 1.0,
        input.normal.z * 2.0 - 1.0);
    output.eyeDirection = geometryData[7].xyz - input.position.xyz;
#ifdef BSL_VERTEX_COLOR
    output.color = float4(pow(input.color.rgb, 2.2), input.color.a);
#else
    output.color = 1.0;
#endif
    return output;
}
#endif

#ifdef BSLIGHTING_VS_WORLD

cbuffer PerMaterial : register(b1)
{
    float4 materialData[3];
};

cbuffer PerGeometry : register(b2)
{
    float4 geometryData[8];
};

struct VSInput
{
    float4 position : POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 normal : NORMAL0;
    float4 binormal : BINORMAL0;
#ifdef BSL_VERTEX_COLOR
    float4 color : COLOR0;
#endif
};

struct VSOutput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
    float4 worldPosition : TEXCOORD4;
    float3 tangentX : TEXCOORD1;
    float3 tangentY : TEXCOORD2;
    float3 tangentZ : TEXCOORD3;
    float3 eyeDirection : TEXCOORD5;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    const float4 position = float4(input.position.xyz, 1.0);
    const float depth = dot(geometryData[2], position);

    output.position = float4(
        dot(geometryData[0], position),
        dot(geometryData[1], position),
        depth,
        dot(geometryData[3], position));
    output.texCoord =
        input.texCoord * materialData[2].zw + materialData[2].xy;

    const float3 worldPosition = float3(
        dot(geometryData[4], position),
        dot(geometryData[5], position),
        dot(geometryData[6], position));
    output.worldPosition = float4(worldPosition, depth);
    output.eyeDirection = geometryData[7].xyz - worldPosition;

    const float3 axisX = float3(
        input.position.w,
        input.normal.w * 2.0 - 1.0,
        input.binormal.w * 2.0 - 1.0);
    const float3 axisY = input.binormal.xyz * 2.0 - 1.0;
    const float3 axisZ = input.normal.xyz * 2.0 - 1.0;

    const float3 worldAxisX = float3(
        dot(axisX, geometryData[4].xyz),
        dot(axisX, geometryData[5].xyz),
        dot(axisX, geometryData[6].xyz));
    const float3 worldAxisY = float3(
        dot(axisY, geometryData[4].xyz),
        dot(axisY, geometryData[5].xyz),
        dot(axisY, geometryData[6].xyz));
    const float3 worldAxisZ = float3(
        dot(axisZ, geometryData[4].xyz),
        dot(axisZ, geometryData[5].xyz),
        dot(axisZ, geometryData[6].xyz));

    output.tangentX = float3(worldAxisX.x, worldAxisY.x, worldAxisZ.x);
    output.tangentY = float3(worldAxisX.y, worldAxisY.y, worldAxisZ.y);
    output.tangentZ = float3(worldAxisX.z, worldAxisY.z, worldAxisZ.z);
#ifdef BSL_VERTEX_COLOR
    output.color = float4(pow(input.color.rgb, 2.2), input.color.a);
#else
    output.color = 1.0;
#endif
    return output;
}
#endif
