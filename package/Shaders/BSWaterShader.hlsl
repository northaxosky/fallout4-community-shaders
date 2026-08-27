// SPDX-License-Identifier: GPL-3.0-or-later
#include "Common/MipBias.hlsli"
cbuffer PerFrame : register(b12)
{
	float4 perFrame[48];
};

#ifdef BSWATER_VERTEX_SHADER

#define DirectionalAmbient (float3x4(perFrame[0], perFrame[1], perFrame[2]))
#define PosAdjust (perFrame[35])

#if defined(NORMAL_TEXCOORD) || defined(WADING)
#define HAS_TEXCOORD
#endif
#if !defined(FOG) && !defined(STENCIL) && !defined(STENCIL_DISPLACEMENT)
#define HAS_SURFACE
#endif
#if !defined(STENCIL) && !defined(STENCIL_DISPLACEMENT)
#if defined(VERTEX_ALPHA_DEPTH) || defined(WADING)
#define HAS_VERTEX_DEPTH
#endif
#endif
#if !defined(LOD) && !defined(SPECULAR)
#define HAS_OBJECT_POSITION
#endif

struct VS_INPUT
{
	float4 Position : POSITION0;
#ifdef HAS_TEXCOORD
	float2 TexCoord : TEXCOORD0;
#endif
#ifdef VC
	float4 Color : COLOR0;
#endif
};

struct VS_OUTPUT
{
	float4 HPosition : SV_POSITION;
	float4 TexCoord0 : TEXCOORD0;
	float4 WPosition : POSITION0;
#ifdef HAS_OBJECT_POSITION
	float4 TexCoord4 : TEXCOORD4;
#endif
#ifdef HAS_SURFACE
	float4 TexCoord1 : TEXCOORD1;
#ifndef LOD
	float4 TexCoord2 : TEXCOORD2;
#endif
#endif
#ifdef HAS_VERTEX_DEPTH
	float3 TexCoord3 : TEXCOORD3;
#endif
#ifdef HAS_SURFACE
	float3 TexCoord5 : TEXCOORD5;
#endif
#ifdef CLIP_VOLUME
	float ClipDistance : SV_ClipDistance0;
#endif
};

#ifdef CLIP_VOLUME
cbuffer PerTechnique : register(b0)
{
	float4 ClipVolumeCenter;
	float4 ClipVolumeRadius;
};
#endif

cbuffer PerMaterial : register(b1)
{
	float4 NormalsScroll01;
	float4 NormalsScroll2;
	float3 NormalsScale;
};

cbuffer PerGeometry : register(b2)
{
	row_major float3x4 World;
	row_major float4x4 WorldViewProj;
	float UseVertexTexCoord;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 hposition = mul(WorldViewProj, inputPosition);
	vsout.HPosition = hposition;

	float3 worldPosition = mul(World, inputPosition);
	vsout.TexCoord0 = float4(worldPosition, length(worldPosition));
	vsout.WPosition.xyz = worldPosition;
#ifdef HAS_OBJECT_POSITION
	vsout.TexCoord4 = inputPosition;
#endif

#ifdef HAS_SURFACE
#ifdef NORMAL_TEXCOORD
	float3 vertexScale = NormalsScale * 0.001;
	float4 vertexUv01 = input.TexCoord.xyxy / vertexScale.xxyy;
	float2 vertexUv2 = input.TexCoord / vertexScale.z;
#else
	float4 vertexUv01 = 0;
	float2 vertexUv2 = 0;
#endif
	float2 worldUv = worldPosition.xy + PosAdjust.xy;
	float4 worldUv01 = worldUv.xyxy / NormalsScale.xxyy;
	float2 worldUv2 = worldUv / NormalsScale.z;
	float2 normalUv0 = UseVertexTexCoord ? vertexUv01.xy : worldUv01.xy;
	float4 normalUv12 = UseVertexTexCoord ? float4(vertexUv01.zw, vertexUv2) : float4(worldUv01.zw, worldUv2);
	vsout.TexCoord1.xy = normalUv0 + NormalsScroll01.xy;
	vsout.TexCoord1.zw = normalUv12.xy + NormalsScroll01.zw;
#ifndef LOD
	vsout.TexCoord2.xy = normalUv12.zw + NormalsScroll2.xy;
	vsout.TexCoord2.z = hposition.w;
	vsout.TexCoord2.w = 0;
#endif
#endif

#ifdef HAS_VERTEX_DEPTH
#ifdef WADING
	vsout.TexCoord3.xy = input.TexCoord;
#else
	vsout.TexCoord3.xy = 0;
#endif
#ifdef VERTEX_ALPHA_DEPTH
	vsout.TexCoord3.z = input.Color.w;
#else
	vsout.TexCoord3.z = 0;
#endif
#endif

#ifdef HAS_SURFACE
	vsout.TexCoord5 = mul(DirectionalAmbient, float4(worldPosition, 1.0));
#endif

#ifdef CLIP_VOLUME
	vsout.ClipDistance = length((worldPosition - ClipVolumeCenter.xyz) / ClipVolumeRadius.xyz) - 1.0;
#endif

	return vsout;
}
#endif

#ifdef BSWATER_PIXEL_SHADER

cbuffer PerGeometry : register(b0)
{
	float4 perGeometry[8];
};

cbuffer PerMaterial : register(b1)
{
	float4 perMaterial[14];
};

#ifdef SPECULAR
cbuffer PerTechnique : register(b2)
{
	float4 perTechnique[NUM_SPECULAR_LIGHTS + 15];
};
#else
cbuffer PerTechnique : register(b2)
{
	float4 perTechnique[5];
};
#endif

SamplerState sampler1 : register(s1);
SamplerState sampler2 : register(s2);
SamplerState sampler4 : register(s4);
SamplerState sampler5 : register(s5);
SamplerState sampler6 : register(s6);
SamplerState sampler7 : register(s7);
SamplerState sampler9 : register(s9);
SamplerState sampler10 : register(s10);

Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);
Texture2D<float4> texture4 : register(t4);
Texture2D<float4> texture5 : register(t5);
Texture2D<float4> texture6 : register(t6);
Texture2D<float4> texture7 : register(t7);
Texture2D<float4> texture9 : register(t9);
Texture2D<float4> texture10 : register(t10);
Texture2D<float4> texture11 : register(t11);

#if defined(STENCIL) || defined(STENCIL_DISPLACEMENT) || defined(FOG)
#define BSWATER_FLAT_INPUT
#endif

#if defined(SSLR) && !defined(REFLECTIONS)
#define BSWATER_SSLR_RAY
#endif

#if !defined(BSWATER_FLAT_INPUT) && !defined(LOD) && !defined(SPECULAR)
#define BSWATER_SURFACE
#endif

struct PS_INPUT
{
	float4 screenPosition : SV_POSITION;
	float4 eyeVector : TEXCOORD0;
	float4 worldPosition : POSITION0;
#if !defined(SPECULAR) && !defined(LOD)
	float4 texcoord4 : TEXCOORD4;
#endif
#ifndef BSWATER_FLAT_INPUT
	float4 normalUv01 : TEXCOORD1;
#ifndef LOD
	float4 normalUv2 : TEXCOORD2;
#if defined(WADING) || defined(VERTEX_ALPHA_DEPTH)
#ifndef SPECULAR
	float3 displacement : TEXCOORD3;
#endif
#endif
#endif
	float3 eyeToPosition : TEXCOORD5;
#endif
};

float3 sceneDepthPosition(float2 screenUv)
{
	float rawDepth = texture7.Sample(sampler7, screenUv).x;
	float4 row0;
	float4 row1;
	float4 row2;
	float4 row3;
	float clipDepth;
	if (0.01 >= rawDepth) {
		clipDepth = rawDepth * 100.0;
		row0 = perFrame[24];
		row1 = perFrame[25];
		row2 = perFrame[26];
		row3 = perFrame[27];
	} else {
		clipDepth = rawDepth * 1.01 - 0.01;
		row0 = perFrame[20];
		row1 = perFrame[21];
		row2 = perFrame[22];
		row3 = perFrame[23];
	}

	float2 clipUv = float2(screenUv.x * perGeometry[0].z, 1.0 - screenUv.y * perGeometry[0].w);
	float4 clipPosition = float4(clipUv * 2.0 - 1.0, clipDepth, 1.0);
	float4 projected;
	projected.x = dot(row0, clipPosition);
	projected.y = dot(row1, clipPosition);
	projected.z = dot(row2, clipPosition);
	projected.w = dot(row3, clipPosition);
	return projected.xyz / projected.w;
}

float3 unpackNormal(float2 texel)
{
	float2 tangent = texel * 2.0 - 1.0;
	return float3(tangent, sqrt(1.0 - min(dot(tangent, tangent), 1.0)));
}

float3 blendedNormal(float2 uv0, float2 uv1, float2 uv2, float fade)
{
	float3 detail0 = unpackNormal(texture4.SampleBias(sampler4, uv0, FO4CS_MIP_BIAS).xy);
	float3 normal = lerp(float3(0.0, 0.0, 1.0), detail0, perMaterial[9].x);
	float3 detail1 = unpackNormal(texture5.SampleBias(sampler5, uv1, FO4CS_MIP_BIAS).xy);
	float3 detail2 = unpackNormal(texture6.SampleBias(sampler6, uv2, FO4CS_MIP_BIAS).xy);
	detail1 = detail1 * perMaterial[9].y;
	detail2 = detail2 * perMaterial[9].z;
	normal = detail1 * fade + normal;
	normal = detail2 * fade + normal;
	return normal;
}

float normalStrength(float shoreFade)
{
#ifdef UNDERWATER
	return perMaterial[11].y;
#else
	return (1.0 - smoothstep(perMaterial[11].w, perMaterial[11].z, shoreFade)) * perMaterial[11].y;
#endif
}

#ifdef BSWATER_SURFACE

float3 refractedScene(float2 screenPosition, float3 normal, out float3 unclipped)
{
	float2 size;
	texture11.GetDimensions(size.x, size.y);
	float2 offset = (normal.z * 0.0625) * normal.xy;
	int2 texel;
	texel.x = (screenPosition.x * perGeometry[0].x - offset.x) * size.x;
	texel.y = (screenPosition.y * perGeometry[0].y + offset.y) * size.y;
	float3 direct = texture1.Sample(sampler1, screenPosition * perGeometry[0].xy).xyz;
	float3 refracted = texture1.Load(int3(texel, 0)).xyz;
	float stencil = texture11.Load(int3(texel, 0)).w * 255.0;
	unclipped = refracted;
	return (abs(stencil - 2.0) < 0.25 || abs(stencil - 3.0) < 0.25) ? refracted : direct;
}

#endif

float3 surfaceColor(float slope, float2 screenUv)
{
	float3 color = lerp(perMaterial[3].xyz, perMaterial[4].xyz, saturate(slope + 0.75));
	color = lerp(color, perMaterial[5].xyz, saturate(slope * 1.9 + 0.35));
#if defined(SSLR) && defined(REFLECTIONS)
	float2 clamped = min(screenUv, perGeometry[1].xy);
	float4 reflection = lerp(texture9.SampleLevel(sampler9, clamped, 0.0), texture10.SampleLevel(sampler10, clamped, 0.0), perGeometry[7].x);
	color = lerp(color, reflection.xyz, reflection.w);
#endif
	return color;
}

float fresnelTerm(float grazing)
{
	return (1.0 - perMaterial[13].z) * pow(grazing, 5) + perMaterial[13].z;
}

float3 atmosphere(float3 eyeToPosition, out float alpha)
{
	float height = dot(perFrame[14], float4(eyeToPosition, 1.0)) + perFrame[35].z;
	float distance = length(eyeToPosition) * perFrame[41].x - perFrame[41].z;
	float fade = saturate(distance);
	float2 heightRange = saturate(height * perFrame[46].xy - perFrame[46].zw);
	float blend = lerp(heightRange.x, heightRange.y, fade);
	float density = (0.75 < distance) ? min((fade - 0.75) * 4.0 * (1.0 - perFrame[43].w) + perFrame[43].w, 1.0) : perFrame[43].w;
	float scale = (distance < 0.015) ? fade * 66.666672 : 1.0;
	density = min(pow(fade, perFrame[42].w), density);

	alpha = 1.0 - blend;
	alpha = blend * perFrame[44].w + alpha;
	float3 nearColor = lerp(perFrame[42].xyz, perFrame[44].xyz, density);
	float3 farColor = lerp(perFrame[43].xyz, perFrame[45].xyz, density);
	float3 fog = lerp(nearColor, farColor, blend);
	alpha = density * alpha;
	alpha = alpha * scale;
	return fog;
}

#ifdef STENCIL
float4 main(PS_INPUT input) : SV_Target0
{
	return float4(0.0, 0.0, 0.0, 0.0078432);
}
#endif

#ifdef STENCIL_DISPLACEMENT
float4 main(PS_INPUT input) : SV_Target0
{
	return float4(0.0, 0.0, 0.0, 0.0117648);
}
#endif

#ifdef FOG
#ifdef UNDERWATER
float4 main(PS_INPUT input) : SV_Target0
{
	return float4(0.0, 0.0, 0.0, 0.0);
}
#else
float4 main(PS_INPUT input) : SV_Target0
{
	float3 viewDirection = normalize(input.eyeVector.xyz);

	float2 screenUv = input.screenPosition.xy * perGeometry[0].xy;
	float3 hit = -viewDirection * length(sceneDepthPosition(screenUv));
	float planeDistance = dot(hit, perTechnique[4].xyz);
	float rayLength = length(hit);
	float submersion = 1.0 - perTechnique[4].w / planeDistance;
	float depthAlpha = saturate(1.0 - submersion * rayLength / perMaterial[10].w);
	float verticalAlpha = saturate(1.0 - submersion * abs(planeDistance) / perMaterial[10].w);

	float4 color;
	float opacity = pow(1.0 - smoothstep(perMaterial[12].y, perMaterial[12].x, depthAlpha), 0.33);
	color.w = lerp(perMaterial[12].z, perMaterial[12].w, opacity);
	color.xyz = lerp(perMaterial[1].xyz, perMaterial[0].xyz, smoothstep(perMaterial[13].y, perMaterial[13].x, verticalAlpha));
	return color;
}
#endif
#endif

#if defined(LOD) && !defined(BSWATER_SSLR_RAY)

float4 main(PS_INPUT input) : SV_Target0
{
	float3 normal = unpackNormal(texture4.SampleBias(sampler4, input.normalUv01.xy, FO4CS_MIP_BIAS).xy);
	normal = lerp(float3(0.0, 0.0, 1.0), lerp(float3(0.0, 0.0, 1.0), normal, perMaterial[9].x), perMaterial[11].y);
	normal = normalize(normal);

	float3 worldNormal;
	worldNormal.x = dot(perFrame[0], float4(normal, 1.0));
	worldNormal.y = dot(perFrame[1], float4(normal, 1.0));
	worldNormal.z = dot(perFrame[2], float4(normal, 1.0));

	float3 viewDirection = normalize(-input.eyeToPosition);
	float3 reflected = reflect(-viewDirection, worldNormal);
	float sunGlare = pow(max(dot(-viewDirection, perGeometry[2].xyz), 0.0), perGeometry[3].w) * perGeometry[2].w;

	float3 lightColor = perGeometry[2].w * perGeometry[3].xyz;
	float3 specular = pow(saturate(dot(reflected, perGeometry[2].xyz)), perMaterial[8].x) * lightColor;
	float3 ambient = pow(saturate(dot(normal, float3(-0.099, -0.099, 0.990))), perMaterial[0].w) * lightColor;
	ambient = ambient * perMaterial[10].z;
	float3 lighting = specular * perMaterial[1].w + ambient;

	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float slope = reflect(eyeDirection, normal).z;
	float3 color = lerp(perMaterial[2].xyz, surfaceColor(slope, input.screenPosition.xy * perGeometry[0].xy), perMaterial[2].w) + lighting;
	float fogAlpha;
	float3 fog = atmosphere(input.eyeToPosition, fogAlpha);
	fog = lerp(fog, perGeometry[3].xyz, sunGlare);
	return float4(lerp(color, fog, fogAlpha), 0.0);
}

#endif
#if defined(SPECULAR) && !defined(BSWATER_SSLR_RAY)

float4 main(PS_INPUT input) : SV_Target0
{
	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
	float strength = normalStrength(1.0);

	float3 normal = blendedNormal(input.normalUv01.xy, input.normalUv01.zw, input.normalUv2.xy, fade);
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normalize(normal), strength));

#ifdef UNDERWATER
	float grazing = 1.0 - saturate(dot(-eyeDirection, -normal));
#else
	float grazing = 1.0 - saturate(dot(-eyeDirection, normal));
#endif
	float fresnel = fresnelTerm(grazing);

	float3 specular = 0.0;
	for (int i = 0; i < NUM_SPECULAR_LIGHTS; i++) {
		float3 delta = perTechnique[i + 6].xyz - perFrame[35].xyz;
		float3 halfVector = normalize(normalize(delta) - eyeDirection);
		float falloff = saturate(length(delta) / perTechnique[i + 6].w);
		float attenuation = 1.0 - falloff * falloff;
		specular = (perTechnique[i + 14].xyz * pow(saturate(dot(halfVector, normal)), perMaterial[13].w)) * attenuation + specular;
	}

	return float4(specular * fresnel, 1.0);
}

#endif
#ifdef BSWATER_SURFACE

#ifdef WADING
float3 displacedNormal(float2 uv, float3 surface)
{
	float3 wave;
	wave.xy = (texture2.SampleBias(sampler2, uv, FO4CS_MIP_BIAS).zw - 0.5) * perMaterial[9].w;
	wave.z = 0.04;
	wave = normalize(wave);
	return lerp(wave, surface, wave.z);
}
#endif

#endif

#if defined(BSWATER_SURFACE) && defined(UNDERWATER) && !defined(BSWATER_SSLR_RAY)

float4 main(PS_INPUT input) : SV_Target0
{
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
	float3 blended = blendedNormal(input.normalUv01.xy, input.normalUv01.zw, input.normalUv2.xy, fade);
#ifdef WADING
	float3 normal = displacedNormal(input.displacement.xy, normalize(blended));
#else
	float3 normal = normalize(blended);
#endif
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normal, normalStrength(1.0)));

	float3 unclipped;
	float3 refraction = refractedScene(input.screenPosition.xy, normal, unclipped);
	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float slope = reflect(eyeDirection, normal).z;
	float grazing = 1.0 - saturate(dot(-eyeDirection, -normal));
	float3 water = lerp(perMaterial[0].xyz, surfaceColor(slope, input.screenPosition.xy * perGeometry[0].xy), 0.5);
	return float4(lerp(water, refraction, 1.0 - fresnelTerm(grazing)), 0.0);
}

#endif
#ifdef BSWATER_SSLR_RAY

struct PS_OUTPUT_SSLR
{
	float3 ray : SV_Target0;
	float4 depth : SV_Target1;
};

#if defined(LOD)
#define BSWATER_RAY_LOD
#elif defined(UNDERWATER)
#define BSWATER_RAY_UNDERWATER
#else
#define BSWATER_RAY_SURFACE
#endif

#ifndef WADING
#ifdef BSWATER_RAY_SURFACE
float stencilTap(uint2 texel)
{
	return texture11.Load(int3(texel, 0)).w;
}
#else
bool stencilCovered(uint2 texel)
{
	return abs(texture11.Load(int3(texel, 0)).w * 255.0 - 3.0) < 0.25;
}
#endif
#endif

float4 toClip(float3 world)
{
	float4 position = float4(world, 1.0);
	float4 clip;
	clip.x = dot(perFrame[4], position);
	clip.y = dot(perFrame[5], position);
	clip.z = dot(perFrame[6], position);
	clip.w = dot(perFrame[7], position);
	return clip;
}

PS_OUTPUT_SSLR main(PS_INPUT input)
{
#ifdef BSWATER_RAY_SURFACE
	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
#endif

#ifdef SPECULAR
	float2 screenUv = perGeometry[0].xy;
#else
	float2 screenUv = input.screenPosition.xy * perGeometry[0].xy;
#endif

#ifndef WADING
	float2 size;
	texture11.GetDimensions(size.x, size.y);
	float2 base = floor(screenUv * size);
#ifdef BSWATER_RAY_SURFACE
	uint2 baseTexel = uint2(base);
	uint2 downTexel = uint2(base + float2(0.0, 1.0));
	uint2 rightTexel = uint2(base + float2(1.0, 0.0));
	uint2 cornerTexel = uint2(base + float2(1.0, 1.0));
	float rawBase = stencilTap(baseTexel);
	float rawDown = stencilTap(downTexel);
	float rawRight = stencilTap(rightTexel);
	float rawCorner = stencilTap(cornerTexel);
	bool coveredBase = abs(rawBase * 255.0 - 3.0) < 0.25;
	bool coveredDown = abs(rawDown * 255.0 - 3.0) < 0.25;
	bool coveredRight = abs(rawRight * 255.0 - 3.0) < 0.25;
	bool coveredCorner = abs(rawCorner * 255.0 - 3.0) < 0.25;
	bool covered = coveredBase && coveredDown && coveredRight && coveredCorner;
#else
	bool covered = stencilCovered(uint2(base));
	float2 down = base + float2(0.0, 1.0);
	float2 right = base + float2(1.0, 0.0);
	uint2 corner = uint2(base + float2(1.0, 1.0));
	covered = covered && stencilCovered(uint2(down));
	covered = covered && stencilCovered(uint2(right));
	covered = covered && stencilCovered(corner);
#endif
	clip(covered ? -1.0 : 1.0);
#endif

#ifdef BSWATER_RAY_LOD
	float4 startClip = toClip(input.eyeToPosition);
	float3 start = startClip.xyz / startClip.w;
	float3 startScreen = start * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);

	float3 normal = unpackNormal(texture4.SampleBias(sampler4, input.normalUv01.xy, FO4CS_MIP_BIAS).xy);
	normal = lerp(float3(0.0, 0.0, 1.0), lerp(float3(0.0, 0.0, 1.0), normal, perMaterial[9].x), 0.5 * perMaterial[11].y);
	normal = normalize(normal);
#elif defined(BSWATER_RAY_UNDERWATER)
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
	float3 blended = blendedNormal(input.normalUv01.xy, input.normalUv01.zw, input.normalUv2.xy, fade);
	float3 normal = normalize(blended);
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normal, 0.5 * normalStrength(1.0)));
#else
	float3 hit = -eyeDirection * length(sceneDepthPosition(screenUv));
	float planeDistance = dot(hit, perTechnique[4].xyz);
	float submersion = 1.0 - perTechnique[4].w / planeDistance;
	float shore = saturate(1.0 - submersion * abs(planeDistance) / perMaterial[10].w);

	float strength = 0.5 * normalStrength(shore);
	float3 blended = blendedNormal(input.normalUv01.xy, input.normalUv01.zw, input.normalUv2.xy, fade);
#ifdef WADING
	float3 normal = displacedNormal(input.displacement.xy, normalize(blended));
#else
	float3 normal = normalize(blended);
#endif
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normal, strength));
#endif

#ifdef BSWATER_RAY_SURFACE
	float3 viewDirection = normalize(-input.eyeToPosition);

	float3 worldNormal;
	worldNormal.x = dot(perFrame[0], float4(normal, 1.0));
	worldNormal.y = dot(perFrame[1], float4(normal, 1.0));
	worldNormal.z = dot(perFrame[2], float4(normal, 1.0));

	float3 reflected = reflect(-viewDirection, worldNormal);

	float4 startClip = toClip(input.eyeToPosition);
	float3 start = startClip.xyz / startClip.w;

	bool visible = perGeometry[7].y < reflected.z;
	float3 far = reflected * 1000.0 + input.eyeToPosition;

	float4 endClip = toClip(far);
	float3 end = endClip.xyz / endClip.w;
	float2 endUv = end.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
	float3 startScreen = start * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);
	float3 endScreen = end * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);
	float3 delta = endScreen - startScreen;
	float2 rayUv = -end.z * (delta.xy * (1.0 / delta.z)) + endUv;
#else
	float3 worldNormal;
	worldNormal.x = dot(perFrame[0], float4(normal, 1.0));
	worldNormal.y = dot(perFrame[1], float4(normal, 1.0));
	worldNormal.z = dot(perFrame[2], float4(normal, 1.0));

	float3 viewDirection = normalize(-input.eyeToPosition);
	float3 reflected = reflect(-viewDirection, worldNormal);
	float3 far = reflected * 1000.0 + input.eyeToPosition;
	bool visible = perGeometry[7].y < reflected.z;

	float4 endClip = toClip(far);
	float3 end = endClip.xyz / endClip.w;
	float2 endUv = end.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
	float3 endScreen = end * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);
#ifdef BSWATER_RAY_UNDERWATER
	float4 startClip = toClip(input.eyeToPosition);
	float3 start = startClip.xyz / startClip.w;
	float3 startScreen = start * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);
#endif
	float3 delta = endScreen - startScreen;
#endif

	float3 ray;
	ray.xy = -end.z * (delta.xy * (1.0 / delta.z)) + endUv;
	ray.z = input.eyeToPosition.z;
	ray = visible ? ray : 0.0;

	PS_OUTPUT_SSLR output;
	output.ray = ray;
	output.depth = ray.z;
	return output;
}

#endif
#if defined(BSWATER_SURFACE) && !defined(UNDERWATER) && !defined(BSWATER_SSLR_RAY)

float4 main(PS_INPUT input) : SV_Target0
{
	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
	float2 screenUv = input.screenPosition.xy * perGeometry[0].xy;

#if defined(VERTEX_ALPHA_DEPTH)
	float depthAlpha = input.displacement.z;
	float shoreAlpha = input.displacement.z;
#elif defined(DEPTH)
	float3 behind = -eyeDirection * length(sceneDepthPosition(screenUv));
	float planeDistance = dot(behind, perTechnique[4].xyz);
	float rayLength = length(behind);
	float submersion = 1.0 - perTechnique[4].w / planeDistance;
	float depthAlpha = saturate(1.0 - submersion * rayLength / perMaterial[10].w);
	float shoreAlpha = saturate(1.0 - submersion * abs(planeDistance) / perMaterial[10].w);
#else
	float depthAlpha = saturate(perMaterial[8].z - (fade - 1.0) * (1.0 - perMaterial[8].z));
	float shoreAlpha = 1.0 - depthAlpha;
#endif

	float opacity = pow(1.0 - smoothstep(perMaterial[12].y, perMaterial[12].x, depthAlpha), 0.33);
	float tint = lerp(perMaterial[12].z, perMaterial[12].w, opacity);
	float shoreBlend = smoothstep(perMaterial[11].x, 1.0, shoreAlpha);
	float strength = normalStrength(shoreAlpha);
	float3 blended = blendedNormal(input.normalUv01.xy, input.normalUv01.zw, input.normalUv2.xy, fade);
#ifdef WADING
	float3 normal = displacedNormal(input.displacement.xy, normalize(blended));
#else
	float3 normal = normalize(blended);
#endif
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normal, strength));

	float grazing = 1.0 - saturate(dot(-eyeDirection, normal));
	float fresnel = fresnelTerm(grazing);
	float slope = reflect(eyeDirection, normal).z;
	float3 water = surfaceColor(slope, screenUv);
	float3 tinted = lerp(perMaterial[2].xyz, water, perMaterial[2].w);

	float3 unclipped;
	float3 refraction = refractedScene(input.screenPosition.xy, normal, unclipped);
	float3 absorbed = lerp(perMaterial[7].xyz, perMaterial[6].xyz, refraction);
	float clarity = 1.0 - tint;
	absorbed = absorbed - refraction;
	refraction = perMaterial[6].w * (clarity * absorbed) + refraction;

	float fogAlpha;
	float3 fog = atmosphere(input.eyeToPosition, fogAlpha);
	float3 viewDirection = normalize(-input.eyeToPosition);
	float sunGlare = pow(max(dot(-viewDirection, perGeometry[2].xyz), 0.0), perGeometry[3].w) * perGeometry[2].w;
	fog = lerp(fog, perGeometry[3].xyz, sunGlare);
#ifndef INTERIOR
	float3 lightColor = perGeometry[2].w * perGeometry[3].xyz;
	float3 ambient = pow(saturate(dot(normal, float3(-0.099, -0.099, 0.990))), perMaterial[0].w) * lightColor;
	ambient = ambient * perMaterial[10].z;

	float3 worldNormal;
	worldNormal.x = dot(perFrame[0], float4(normal, 1.0));
	worldNormal.y = dot(perFrame[1], float4(normal, 1.0));
	worldNormal.z = dot(perFrame[2], float4(normal, 1.0));
	float3 reflected = reflect(-viewDirection, worldNormal);
	float3 specular = pow(saturate(dot(reflected, perGeometry[2].xyz)), perMaterial[8].x) * lightColor;
	float3 lighting = specular * perMaterial[1].w + ambient;
#endif

	float3 color = lerp(refraction, water, fresnel * perMaterial[8].y);
	color = lerp(tinted, color, fade);
#ifndef INTERIOR
	color = color + lighting;
#endif

	color = lerp(color, unclipped, shoreBlend);
	return float4(lerp(color, fog, fogAlpha), 0.0);
}

#endif
#endif
