// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
cbuffer PerFrame : register(b12)
{
	row_major float4x4 WorldToView;
	row_major float4x4 ViewToClip;
	row_major float4x4 WorldToClip;
	row_major float4x4 ViewToWorld;
	row_major float4x4 ClipToWorld;
	row_major float4x4 ClipToView;
	row_major float4x4 FirstPersonClipToView;
	float4 HairSpecularScaleAndPower;
	float4 HairSpecularShift;
	float4 WorldWetnessAndRainTransition;
	row_major float4x4 PreviousWorldToClip;
	float4 PosAdjust;
	float4 PreviousPosAdjust;
	row_major float4x4 UnjitteredWorldToClip;
	float4 FogDistanceAndLowHeightRamps;
	float4 FogColor0;
	float4 FogColor1;
	float4 FogColor2;
	float4 FogColor3;
	float4 FogHeightRamps;
};

#if defined(WATER_EFFECTS) && !defined(VC) && !defined(LOD) && !defined(STENCIL) && !defined(STENCIL_DISPLACEMENT) && !defined(FOG)
#	define WATER_PARALLAX
#endif

#ifdef BSWATER_VERTEX_SHADER

#	if defined(NORMAL_TEXCOORD) || defined(WADING)
#		define HAS_TEXCOORD
#	endif
#	if !defined(FOG) && !defined(STENCIL) && !defined(STENCIL_DISPLACEMENT)
#		define HAS_SURFACE
#	endif
#	if !defined(STENCIL) && !defined(STENCIL_DISPLACEMENT)
#		if defined(VERTEX_ALPHA_DEPTH) || defined(WADING)
#			define HAS_VERTEX_DEPTH
#		endif
#	endif
#	if defined(WATER_PARALLAX) && defined(SPECULAR) && defined(HAS_VERTEX_DEPTH)
// FO4: specular PS never reads TEXCOORD3; keeps the pair's registers aligned.
#		undef HAS_VERTEX_DEPTH
#	endif
#	if !defined(LOD) && !defined(SPECULAR)
#		define HAS_OBJECT_POSITION
#	endif

struct VS_INPUT
{
	float4 Position: POSITION0;
#	ifdef HAS_TEXCOORD
	float2 TexCoord: TEXCOORD0;
#	endif
#	ifdef VC
	float4 Color: COLOR0;
#	endif
};

struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION;
	float4 TexCoord0: TEXCOORD0;
#	if !defined(OG)
	// OG 1.10.163 has no world-position interpolant; every later output is one register lower.
	float4 WPosition: POSITION0;
#	endif
#	ifdef HAS_OBJECT_POSITION
	float4 TexCoord4: TEXCOORD4;
#	endif
#	ifdef HAS_SURFACE
	float4 TexCoord1: TEXCOORD1;
#		ifndef LOD
	float4 TexCoord2: TEXCOORD2;
#		endif
#	endif
#	ifdef HAS_VERTEX_DEPTH
	float3 TexCoord3: TEXCOORD3;
#	endif
#	ifdef HAS_SURFACE
	float3 TexCoord5: TEXCOORD5;
#	endif
#	ifdef WATER_PARALLAX
	// FO4: PS has no NormalsScale; forwarded as upstream's TEXCOORD8.
	float3 NormalsScale: TEXCOORD8;
#	endif
#	ifdef CLIP_VOLUME
	float ClipDistance: SV_ClipDistance0;
#	endif
};

#	ifdef CLIP_VOLUME
cbuffer PerTechnique : register(b0)
{
	float4 ClipVolume_row0;  // Clip-volume center.
	float4 ClipVolume_row1;  // Clip-volume radius.
};
#	endif

cbuffer PerMaterial : register(b1)
{
	float4 NormalsScroll0_row0;  // Scroll offsets for normal layers 0 and 1.
#	if defined(LOD)
	float4 NormalsScroll0_row1;
#	elif defined(STENCIL) || defined(FOG) || defined(STENCIL_DISPLACEMENT)
	float4 NormalsScroll2;
#	else
	float4 NormalsScroll1;
#	endif
	float3 NormalsScale;
};

cbuffer PerGeometry : register(b2)
{
	row_major float3x4 World;
	row_major float4x4 WorldViewProj;
#	if defined(STENCIL) || defined(FOG) || defined(STENCIL_DISPLACEMENT)
	float WorldViewProj_row4;
#	else
	float ObjectUV;
#	endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 hposition = mul(WorldViewProj, inputPosition);
	vsout.HPosition = hposition;

	float3 worldPosition = mul(World, inputPosition);
	vsout.TexCoord0 = float4(worldPosition, length(worldPosition));
#	if !defined(OG)
	vsout.WPosition.xyz = worldPosition;
#	endif
#	ifdef HAS_OBJECT_POSITION
	vsout.TexCoord4 = inputPosition;
#	endif

#	ifdef HAS_SURFACE
#		ifdef NORMAL_TEXCOORD
	float3 vertexScale = NormalsScale * 0.001;
	float4 vertexUv01 = input.TexCoord.xyxy / vertexScale.xxyy;
	float2 vertexUv2 = input.TexCoord / vertexScale.z;
#		else
	float4 vertexUv01 = 0;
	float2 vertexUv2 = 0;
#		endif
	float2 worldUv = worldPosition.xy + PosAdjust.xy;
	float4 worldUv01 = worldUv.xyxy / NormalsScale.xxyy;
	float2 worldUv2 = worldUv / NormalsScale.z;
	float2 normalUv0 = ObjectUV ? vertexUv01.xy : worldUv01.xy;
	float4 normalUv12 = ObjectUV ? float4(vertexUv01.zw, vertexUv2) : float4(worldUv01.zw, worldUv2);
	vsout.TexCoord1.xy = normalUv0 + NormalsScroll0_row0.xy;
	vsout.TexCoord1.zw = normalUv12.xy + NormalsScroll0_row0.zw;
#		ifndef LOD
	vsout.TexCoord2.xy = normalUv12.zw + NormalsScroll1.xy;
	vsout.TexCoord2.z = hposition.w;
	vsout.TexCoord2.w = 0;
#		endif
#	endif

#	ifdef HAS_VERTEX_DEPTH
#		ifdef WADING
	vsout.TexCoord3.xy = input.TexCoord;
#		else
	vsout.TexCoord3.xy = 0;
#		endif
#		ifdef VERTEX_ALPHA_DEPTH
	vsout.TexCoord3.z = input.Color.w;
#		else
	vsout.TexCoord3.z = 0;
#		endif
#	endif

#	ifdef HAS_SURFACE
	vsout.TexCoord5 = mul(float3x4(WorldToView[0], WorldToView[1], WorldToView[2]), float4(worldPosition, 1.0));
#	endif
#	ifdef WATER_PARALLAX
	vsout.NormalsScale = NormalsScale;
#	endif

#	ifdef CLIP_VOLUME
	vsout.ClipDistance = length((worldPosition - ClipVolume_row0.xyz) / ClipVolume_row1.xyz) - 1.0;
#	endif

	return vsout;
}
#endif

#ifdef BSWATER_PIXEL_SHADER
#	include "FO4/FogConsumer.hlsli"
#	include "FO4/WorldShadowConsumer.hlsli"
#	ifdef VOLUMETRIC_SHADOWS
#		include "FO4/VolumetricShadowsConsumer.hlsli"
#	endif
#	if defined(VOLUMETRIC_SHADOWS) || defined(FO4_WORLD_SHADOW)
// FO4: a disabled VSM falls back to the world term it adds to.
float FO4WaterSunShadow(float3 worldPosition, float2 screenPosition)
{
#		ifdef VOLUMETRIC_SHADOWS
	[branch] if (FO4SharedData::EnabledVolumetricShadows != 0) return FO4VolumetricShadows::GetWaterSurfaceShadow(worldPosition, screenPosition);
#		endif
#		ifdef FO4_WORLD_SHADOW
	return WorldShadow::GetMultFromWorldPosition(worldPosition);
#		else
	return 1.0;
#		endif
}
#		define FO4_WATER_SUN_SHADOW(worldPosition, screenPosition) FO4WaterSunShadow(worldPosition, screenPosition)
#	endif
#	ifdef UNIFIED_WATER
#		include "FO4/FO4ShaderData.hlsli"
#	endif
#	ifdef LIGHT_LIMIT_FIX
#		include "FO4/LightLimitFixForward.hlsli"
#	endif

#	ifdef DYNAMIC_CUBEMAPS
// FO4 water compiles without upstream's WATER permutation define.
#		ifndef WATER
#			define WATER
#		endif
#		include "FO4/DynamicCubemaps/DynamicCubemaps.hlsli"
SamplerState sampler3 : register(s3);
// FO4: the host-rendered engine reflection cube, upstream's CubeMapTex.
TextureCube<float3> CubeMapTex : register(t32);
#	endif
#	if defined(SKYLIGHTING) && defined(DYNAMIC_CUBEMAPS)
#		define SKYLIGHTING_PROBE_REGISTER t50
#		include "Skylighting/Skylighting.hlsli"
#	endif

cbuffer PerGeometry : register(b0)
{
	float4 perGeometry[8];
};

// OG 1.10.163 predates the b0[1] DynamicResolutionParam clamp, so every later register is one lower.
#	if defined(OG)
#		define WATER_PER_GEOMETRY(index) perGeometry[(index) - 1]
#	else
#		define WATER_PER_GEOMETRY(index) perGeometry[index]
#	endif

cbuffer PerMaterial : register(b1)
{
	float4 perMaterial[14];
};

#	ifdef SPECULAR
cbuffer PerTechnique : register(b2)
{
	float4 perTechnique[NUM_SPECULAR_LIGHTS + 15];
};
#	else
cbuffer PerTechnique : register(b2)
{
	float4 perTechnique[5];
};
#	endif

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

#	if defined(STENCIL) || defined(STENCIL_DISPLACEMENT) || defined(FOG)
#		define BSWATER_FLAT_INPUT
#	endif

#	if defined(SSLR) && !defined(REFLECTIONS)
#		define BSWATER_SSLR_RAY
#	endif

#	if !defined(BSWATER_FLAT_INPUT) && !defined(LOD) && !defined(SPECULAR)
#		define BSWATER_SURFACE
#	endif

struct PS_INPUT
{
	float4 screenPosition: SV_POSITION;
	float4 eyeVector: TEXCOORD0;
#	if !defined(OG)
	// OG 1.10.163 has no world-position interpolant; every later input is one register lower.
	float4 worldPosition: POSITION0;
#	endif
#	if !defined(SPECULAR) && !defined(LOD)
	float4 texcoord4: TEXCOORD4;
#	endif
#	ifndef BSWATER_FLAT_INPUT
	float4 normalUv01: TEXCOORD1;
#		ifndef LOD
	float4 normalUv2: TEXCOORD2;
#			if defined(WADING) || defined(VERTEX_ALPHA_DEPTH)
#				ifndef SPECULAR
	float3 displacement: TEXCOORD3;
#				endif
#			endif
#		endif
	float3 eyeToPosition: TEXCOORD5;
#		ifdef WATER_PARALLAX
	float3 normalsScale: TEXCOORD8;
#		endif
#	endif
};

#	ifdef WATER_PARALLAX
#		include "FO4/WaterParallaxConsumer.hlsli"
#	endif

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
		row0 = FirstPersonClipToView[0];
		row1 = FirstPersonClipToView[1];
		row2 = FirstPersonClipToView[2];
		row3 = FirstPersonClipToView[3];
	} else {
		clipDepth = rawDepth * 1.01 - 0.01;
		row0 = ClipToView[0];
		row1 = ClipToView[1];
		row2 = ClipToView[2];
		row3 = ClipToView[3];
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

// FO4: share the optical distance blend across all water consumers.
float WaterDistanceBlendFactor(float distanceFactor)
{
	float distanceBlendFactor = distanceFactor;
#	if defined(UNIFIED_WATER)
	if (FO4SharedData::EnabledUnifiedWater != 0)
		distanceBlendFactor = 1.0f;
#	endif
	return distanceBlendFactor;
}

float3 blendedNormal(float2 uv0, float2 uv1, float2 uv2, float fade)
{
	float3 detail0 = unpackNormal(texture4.Sample(sampler4, uv0).xy);
	float3 normal = lerp(float3(0.0, 0.0, 1.0), detail0, perMaterial[9].x);
	float3 detail1 = unpackNormal(texture5.Sample(sampler5, uv1).xy);
	float3 detail2 = unpackNormal(texture6.Sample(sampler6, uv2).xy);
	detail1 = detail1 * perMaterial[9].y;
	detail2 = detail2 * perMaterial[9].z;
	normal = detail1 * fade + normal;
	normal = detail2 * fade + normal;
	return normal;
}

#	if !defined(LOD) && !defined(BSWATER_FLAT_INPUT)
// FO4: SSLR ray passes rebuild the normal and need the same offset.
float3 surfaceNormal(PS_INPUT input, float fade)
{
	fade = WaterDistanceBlendFactor(fade);
	float2 uv0 = input.normalUv01.xy;
	float2 uv1 = input.normalUv01.zw;
	float2 uv2 = input.normalUv2.xy;
#		ifdef WATER_PARALLAX
	[branch] if (FO4SharedData::EnabledWaterEffects != 0)
	{
		float3 normalScalesRcp = rcp(input.normalsScale);
		float2 parallaxOffset = WaterEffects::GetParallaxOffset(input, perMaterial[9].xyz, normalScalesRcp);
		uv0 += parallaxOffset * normalScalesRcp.x;
		uv1 += parallaxOffset * normalScalesRcp.y;
		uv2 += parallaxOffset * normalScalesRcp.z;
	}
#		endif
	return blendedNormal(uv0, uv1, uv2, fade);
}
#	endif

float normalStrength(float shoreFade)
{
#	ifdef UNDERWATER
	return perMaterial[11].y;
#	else
	return (1.0 - smoothstep(perMaterial[11].w, perMaterial[11].z, shoreFade)) * perMaterial[11].y;
#	endif
}

#	ifdef BSWATER_SURFACE

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
	float3 accepted = (abs(stencil - 2.0) < 0.25 || abs(stencil - 3.0) < 0.25) ? refracted : direct;
#		ifdef FO4CS_SUBSTRATE
	// Stock shore blend skips the mask and pulls in first-person pixels.
	unclipped = accepted;
#		else
	unclipped = refracted;
#		endif
	return accepted;
}

#	endif

float skylightingSpecularVisibility(float3 positionMS, float3 normal, float3 eyeDirection)
{
#	if defined(SKYLIGHTING) && defined(DYNAMIC_CUBEMAPS)
	[branch] if (FO4SharedData::EnabledSkylighting == 0) return 1.0;
	// FO4: callers pass eyeVector.xyz, the camera-relative position OG lacks.
	sh2 skylightingSH = Skylighting::SampleNoBias(positionMS);
	sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(normal, -eyeDirection, 0.0);
	return Skylighting::EvaluateSpecular(skylightingSH, specularLobe, Skylighting::GetFadeOutFactor(positionMS));
#	else
	return 1.0;
#	endif
}

float3 surfaceColor(
	float slope,
	float2 screenUv,
	float3 eyeDirection,
	float3 normal,
	float3 reflectionDirection,
	float cameraDistance,
	float distanceFactor,
	float skylightingSpecular)
{
	float3 color = lerp(perMaterial[3].xyz, perMaterial[4].xyz, saturate(slope + 0.75));
	color = lerp(color, perMaterial[5].xyz, saturate(slope * 1.9 + 0.35));
#	if defined(REFLECTIONS) || defined(DYNAMIC_CUBEMAPS)
#		ifdef DYNAMIC_CUBEMAPS
	uint cubeWidth, cubeHeight, envWidth, envHeight;
	DynamicCubemaps::EnvReflectionsTexture.GetDimensions(cubeWidth, cubeHeight);
	DynamicCubemaps::EnvTexture.GetDimensions(envWidth, envHeight);
	[branch] if (FO4SharedData::EnabledDynamicCubemaps != 0 && cubeWidth != 0 && envWidth != 0)
	{
#			if defined(REFLECTIONS)
		// FO4: without a fresh engine cube the sky gradient stays the far reflection.
		uint engineWidth, engineHeight;
		CubeMapTex.GetDimensions(engineWidth, engineHeight);
		if (engineWidth != 0)
			color = CubeMapTex.SampleLevel(sampler3, reflectionDirection, 0).xyz;
#			endif

		float3 dynamicCubemap;
		if (SharedData::InInterior) {
			dynamicCubemap = DynamicCubemaps::EnvTexture.SampleLevel(sampler3, reflectionDirection, 0).xyz;
		} else {
			float3 specularIrradiance = 0.0;
			if (skylightingSpecular < 1.0)
				specularIrradiance = DynamicCubemaps::EnvTexture.SampleLevel(sampler3, reflectionDirection, 0).xyz;

			float3 specularIrradianceReflections = 0.0;
			if (skylightingSpecular > 0.0)
				specularIrradianceReflections = DynamicCubemaps::EnvReflectionsTexture.SampleLevel(sampler3, reflectionDirection, 0).xyz;

			dynamicCubemap = ColorManagement::SceneColor::LerpInLinear(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
		}

		float reflectionAmount = saturate(cameraDistance / 1024.0);

		if (SharedData::HideSky || SharedData::InInterior)
			reflectionAmount = 0.0;
#			if !defined(REFLECTIONS)
		// Upstream: water without the Reflections flag takes the dynamic cubemap alone.
		reflectionAmount = 0.0;
#			endif

		float3 debugColor;
		if (DynamicCubemaps::TryGetDebugEnvironment(reflectionDirection, sampler3, debugColor)) {
			dynamicCubemap = debugColor;
			reflectionAmount = 0.0;
		}
		color = lerp(dynamicCubemap, color, reflectionAmount);
	}
#		endif
#		if defined(REFLECTIONS) && defined(SSLR)
#			if defined(OG)
	// OG 1.10.163 samples the reflection with the unclamped screen UV.
	float2 clamped = screenUv;
#			else
	float2 clamped = min(screenUv, perGeometry[1].xy);
#			endif
#			if defined(DYNAMIC_CUBEMAPS) && !defined(LOD)
	[branch] if (FO4SharedData::EnabledDynamicCubemaps != 0)
	{
		float pointingDirection = dot(eyeDirection, reflectionDirection) * 0.5 + 0.5;
		float pointingAlignment = dot(reflect(eyeDirection, float3(0, 0, 1)), reflectionDirection) * 0.5 + 0.5;
		float ssrAmount = sqrt(min(pointingAlignment, pointingDirection));
		// FO4: t9 is raw and t10 blurred; the offset UV keeps the stock clamp.
		float2 ssrReflectionUv = min(screenUv + 0.05 * normal.xy, perGeometry[1].xy);
		float4 ssrReflectionColorBlurred = texture10.SampleLevel(sampler10, ssrReflectionUv, 0.0);
		float4 ssrReflectionColorRaw = texture9.SampleLevel(sampler9, ssrReflectionUv, 0.0);
		float4 ssrReflectionColor = lerp(ssrReflectionColorBlurred, ssrReflectionColorRaw, ssrAmount * 0.7);
		float ssrFraction = saturate(ssrReflectionColor.w * distanceFactor * ssrAmount);
		return lerp(color, max(0, ssrReflectionColor.xyz), ssrFraction);
	}
#			endif
	float4 reflection = lerp(texture9.SampleLevel(sampler9, clamped, 0.0), texture10.SampleLevel(sampler10, clamped, 0.0), WATER_PER_GEOMETRY(7).x);
	color = lerp(color, reflection.xyz, reflection.w);
#		endif
#	endif
	return color;
}

float fresnelTerm(float grazing)
{
	float f0 = perMaterial[13].z;
#	if defined(DYNAMIC_CUBEMAPS)
	// FO4 translation: Skyrim-scale F0 is FO4's F0 times its reflection amount.
	if (FO4SharedData::EnabledDynamicCubemaps != 0)
		f0 *= perMaterial[8].y;
#	endif
	return (1.0 - f0) * pow(grazing, 5) + f0;
}

float3 atmosphere(float3 eyeToPosition, out float alpha)
{
	float height = dot(ViewToWorld[2], float4(eyeToPosition, 1.0)) + PosAdjust.z;
	float distance = length(eyeToPosition) * FogDistanceAndLowHeightRamps.x - FogDistanceAndLowHeightRamps.z;
	float fade = saturate(distance);
	float2 heightRange = saturate(height * FogHeightRamps.xy - FogHeightRamps.zw);
	float blend = lerp(heightRange.x, heightRange.y, fade);
	float density = (0.75 < distance) ? min((fade - 0.75) * 4.0 * (1.0 - FogColor1.w) + FogColor1.w, 1.0) : FogColor1.w;
	float scale = (distance < 0.015) ? fade * 66.666672 : 1.0;
	density = min(pow(fade, FogColor0.w), density);

	alpha = 1.0 - blend;
	alpha = blend * FogColor2.w + alpha;
	float3 nearColor = lerp(FogColor0.xyz, FogColor2.xyz, density);
	float3 farColor = lerp(FogColor1.xyz, FogColor3.xyz, density);
	float3 fog = lerp(nearColor, farColor, blend);
	alpha = density * alpha;
	alpha = alpha * scale;
	return FO4_FOG_VANILLA_COLOR(fog);
}

#	ifdef STENCIL
float4 main(PS_INPUT input) : SV_Target0
{
	return float4(0.0, 0.0, 0.0, 0.0078432);
}
#	endif

#	ifdef STENCIL_DISPLACEMENT
float4 main(PS_INPUT input) : SV_Target0
{
	return float4(0.0, 0.0, 0.0, 0.0117648);
}
#	endif

#	ifdef FOG
#		ifdef UNDERWATER
float4 main(PS_INPUT input) : SV_Target0
{
	return float4(0.0, 0.0, 0.0, 0.0);
}
#		else
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
#		endif
#	endif

#	if defined(LOD) && !defined(BSWATER_SSLR_RAY)

float4 main(PS_INPUT input) : SV_Target0
{
	float3 normal = unpackNormal(texture4.Sample(sampler4, input.normalUv01.xy).xy);
	normal = lerp(float3(0.0, 0.0, 1.0), lerp(float3(0.0, 0.0, 1.0), normal, perMaterial[9].x), perMaterial[11].y);
	normal = normalize(normal);

	float3 viewNormal;
	viewNormal.x = dot(WorldToView[0], float4(normal, 1.0));
	viewNormal.y = dot(WorldToView[1], float4(normal, 1.0));
	viewNormal.z = dot(WorldToView[2], float4(normal, 1.0));

	float3 viewDirection = normalize(-input.eyeToPosition);
	float3 reflected = reflect(-viewDirection, viewNormal);
	float sunGlare = pow(max(dot(-viewDirection, WATER_PER_GEOMETRY(2).xyz), 0.0), WATER_PER_GEOMETRY(3).w) * WATER_PER_GEOMETRY(2).w;

	float3 lightColor = WATER_PER_GEOMETRY(2).w * WATER_PER_GEOMETRY(3).xyz;
#		ifdef FO4_WATER_SUN_SHADOW
	lightColor *= FO4_WATER_SUN_SHADOW(FrameBuffer::ViewToWorld(input.eyeToPosition), input.screenPosition.xy);
#		endif
	float3 specular = pow(saturate(dot(reflected, WATER_PER_GEOMETRY(2).xyz)), perMaterial[8].x) * lightColor;
	float3 ambient = pow(saturate(dot(normal, float3(-0.099, -0.099, 0.990))), perMaterial[0].w) * lightColor;
	ambient = ambient * perMaterial[10].z;
	float3 lighting = specular * perMaterial[1].w + ambient;

	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float3 reflectionDirection = reflect(eyeDirection, normal);
	float slope = reflectionDirection.z;
	float3 color = lerp(perMaterial[2].xyz, surfaceColor(slope, input.screenPosition.xy * perGeometry[0].xy, eyeDirection, normal, reflectionDirection, input.eyeVector.w, 0.0, skylightingSpecularVisibility(input.eyeVector.xyz, normal, eyeDirection)), perMaterial[2].w) + lighting;
	float fogAlpha;
	float3 fog = atmosphere(input.eyeToPosition, fogAlpha);
	fog = lerp(fog, WATER_PER_GEOMETRY(3).xyz, sunGlare);
	return float4(lerp(color, fog, fogAlpha), 0.0);
}

#	endif
#	if defined(SPECULAR) && !defined(BSWATER_SSLR_RAY)

float4 main(PS_INPUT input) : SV_Target0
{
	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
	float strength = normalStrength(1.0);

	float3 normal = surfaceNormal(input, fade);
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normalize(normal), strength));

#		ifdef UNDERWATER
	float grazing = 1.0 - saturate(dot(-eyeDirection, -normal));
#		else
	float grazing = 1.0 - saturate(dot(-eyeDirection, normal));
#		endif
	float fresnel = fresnelTerm(grazing);

	float3 specular = 0.0;
#		ifdef LIGHT_LIMIT_FIX
	// FO4: the surface shader's cluster loop replaces these lights.
	[branch] if (FO4LightLimitFix::UseClusteredLights()) return float4(0.0, 0.0, 0.0, 1.0);
#		endif
	for (int i = 0; i < NUM_SPECULAR_LIGHTS; i++) {
		float3 delta = perTechnique[i + 6].xyz - PosAdjust.xyz;
		float3 halfVector = normalize(normalize(delta) - eyeDirection);
		float falloff = saturate(length(delta) / perTechnique[i + 6].w);
		float attenuation = 1.0 - falloff * falloff;
		specular = (perTechnique[i + 14].xyz * pow(saturate(dot(halfVector, normal)), perMaterial[13].w)) * attenuation + specular;
	}

	return float4(specular * fresnel, 1.0);
}

#	endif
#	ifdef BSWATER_SURFACE

#		ifdef WADING
float3 displacedNormal(float2 uv, float3 surface)
{
	float3 wave;
	wave.xy = (texture2.Sample(sampler2, uv).zw - 0.5) * perMaterial[9].w;
	wave.z = 0.04;
	wave = normalize(wave);
	return lerp(wave, surface, wave.z);
}
#		endif

#	endif

#	if defined(BSWATER_SURFACE) && defined(UNDERWATER) && !defined(BSWATER_SSLR_RAY)

float4 main(PS_INPUT input) : SV_Target0
{
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
	float3 blended = surfaceNormal(input, fade);
#		ifdef WADING
	float3 normal = displacedNormal(input.displacement.xy, normalize(blended));
#		else
	float3 normal = normalize(blended);
#		endif
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normal, normalStrength(1.0)));

	float3 unclipped;
	float3 refraction = refractedScene(input.screenPosition.xy, normal, unclipped);
	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float3 reflectionDirection = reflect(eyeDirection, normal);
	float slope = reflectionDirection.z;
	float grazing = 1.0 - saturate(dot(-eyeDirection, -normal));
	float3 water = lerp(perMaterial[0].xyz, surfaceColor(slope, input.screenPosition.xy * perGeometry[0].xy, eyeDirection, normal, reflectionDirection, input.eyeVector.w, WaterDistanceBlendFactor(fade), skylightingSpecularVisibility(input.eyeVector.xyz, normal, eyeDirection)), 0.5);
#		ifdef LIGHT_LIMIT_FIX
	[branch] if (FO4LightLimitFix::UseClusteredLights())
		water += 0.5 * FO4LightLimitFix::GetWaterSpecularLighting(input.eyeToPosition, FrameBuffer::ViewToWorld(input.eyeToPosition), normal, perMaterial[13].w);
#		endif
	return float4(lerp(water, refraction, 1.0 - fresnelTerm(grazing)), 0.0);
}

#	endif
#	ifdef BSWATER_SSLR_RAY

struct PS_OUTPUT_SSLR
{
	float3 ray: SV_Target0;
	float4 depth: SV_Target1;
};

#		if defined(LOD)
#			define BSWATER_RAY_LOD
#		elif defined(UNDERWATER)
#			define BSWATER_RAY_UNDERWATER
#		else
#			define BSWATER_RAY_SURFACE
#		endif

#		ifndef WADING
#			ifdef BSWATER_RAY_SURFACE
float stencilTap(uint2 texel)
{
	return texture11.Load(int3(texel, 0)).w;
}
#			else
bool stencilCovered(uint2 texel)
{
	return abs(texture11.Load(int3(texel, 0)).w * 255.0 - 3.0) < 0.25;
}
#			endif
#		endif

float4 toClip(float3 world)
{
	float4 position = float4(world, 1.0);
	float4 clip;
	clip.x = dot(ViewToClip[0], position);
	clip.y = dot(ViewToClip[1], position);
	clip.z = dot(ViewToClip[2], position);
	clip.w = dot(ViewToClip[3], position);
	return clip;
}

PS_OUTPUT_SSLR main(PS_INPUT input)
{
#		ifdef BSWATER_RAY_SURFACE
	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
#		endif

#		ifdef SPECULAR
	float2 screenUv = perGeometry[0].xy;
#		else
	float2 screenUv = input.screenPosition.xy * perGeometry[0].xy;
#		endif

#		ifndef WADING
	float2 size;
	texture11.GetDimensions(size.x, size.y);
	float2 base = floor(screenUv * size);
#			ifdef BSWATER_RAY_SURFACE
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
#			else
	bool covered = stencilCovered(uint2(base));
	float2 down = base + float2(0.0, 1.0);
	float2 right = base + float2(1.0, 0.0);
	uint2 corner = uint2(base + float2(1.0, 1.0));
	covered = covered && stencilCovered(uint2(down));
	covered = covered && stencilCovered(uint2(right));
	covered = covered && stencilCovered(corner);
#			endif
	clip(covered ? -1.0 : 1.0);
#		endif

#		ifdef BSWATER_RAY_LOD
	float4 startClip = toClip(input.eyeToPosition);
	float3 start = startClip.xyz / startClip.w;
	float3 startScreen = start * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);

	float3 normal = unpackNormal(texture4.Sample(sampler4, input.normalUv01.xy).xy);
	normal = lerp(float3(0.0, 0.0, 1.0), lerp(float3(0.0, 0.0, 1.0), normal, perMaterial[9].x), 0.5 * perMaterial[11].y);
	normal = normalize(normal);
#		elif defined(BSWATER_RAY_UNDERWATER)
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
	float3 blended = surfaceNormal(input, fade);
	float3 normal = normalize(blended);
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normal, 0.5 * normalStrength(1.0)));
#		else
	float3 hit = -eyeDirection * length(sceneDepthPosition(screenUv));
	float planeDistance = dot(hit, perTechnique[4].xyz);
	float submersion = 1.0 - perTechnique[4].w / planeDistance;
	float shore = saturate(1.0 - submersion * abs(planeDistance) / perMaterial[10].w);

	float strength = 0.5 * normalStrength(shore);
	float3 blended = surfaceNormal(input, fade);
#			ifdef WADING
	float3 normal = displacedNormal(input.displacement.xy, normalize(blended));
#			else
	float3 normal = normalize(blended);
#			endif
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normal, strength));
#		endif

#		ifdef BSWATER_RAY_SURFACE
	float3 viewDirection = normalize(-input.eyeToPosition);

	float3 viewNormal;
	viewNormal.x = dot(WorldToView[0], float4(normal, 1.0));
	viewNormal.y = dot(WorldToView[1], float4(normal, 1.0));
	viewNormal.z = dot(WorldToView[2], float4(normal, 1.0));

	float3 reflected = reflect(-viewDirection, viewNormal);

	float4 startClip = toClip(input.eyeToPosition);
	float3 start = startClip.xyz / startClip.w;

	bool visible = WATER_PER_GEOMETRY(7).y < reflected.z;
	float3 far = reflected * 1000.0 + input.eyeToPosition;

	float4 endClip = toClip(far);
	float3 end = endClip.xyz / endClip.w;
	float2 endUv = end.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
	float3 startScreen = start * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);
	float3 endScreen = end * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);
	float3 delta = endScreen - startScreen;
	float2 rayUv = -end.z * (delta.xy * (1.0 / delta.z)) + endUv;
#		else
	float3 viewNormal;
	viewNormal.x = dot(WorldToView[0], float4(normal, 1.0));
	viewNormal.y = dot(WorldToView[1], float4(normal, 1.0));
	viewNormal.z = dot(WorldToView[2], float4(normal, 1.0));

	float3 viewDirection = normalize(-input.eyeToPosition);
	float3 reflected = reflect(-viewDirection, viewNormal);
	float3 far = reflected * 1000.0 + input.eyeToPosition;
	bool visible = WATER_PER_GEOMETRY(7).y < reflected.z;

	float4 endClip = toClip(far);
	float3 end = endClip.xyz / endClip.w;
	float2 endUv = end.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
	float3 endScreen = end * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);
#			ifdef BSWATER_RAY_UNDERWATER
	float4 startClip = toClip(input.eyeToPosition);
	float3 start = startClip.xyz / startClip.w;
	float3 startScreen = start * float3(0.5, -0.5, 1.0) + float3(0.5, 0.5, 0.0);
#			endif
	float3 delta = endScreen - startScreen;
#		endif

	float3 ray;
	ray.xy = -end.z * (delta.xy * (1.0 / delta.z)) + endUv;
	ray.z = input.eyeToPosition.z;
	ray = visible ? ray : 0.0;

	PS_OUTPUT_SSLR output;
	output.ray = ray;
	output.depth = ray.z;
	return output;
}

#	endif
#	if defined(BSWATER_SURFACE) && !defined(UNDERWATER) && !defined(BSWATER_SSLR_RAY)

float4 main(PS_INPUT input) : SV_Target0
{
	float3 eyeDirection = normalize(input.eyeVector.xyz);
	float fade = saturate((input.eyeVector.w - 8192.0) / (perMaterial[10].x - 8192.0));
	float2 screenUv = input.screenPosition.xy * perGeometry[0].xy;

#		if defined(VERTEX_ALPHA_DEPTH)
	float depthAlpha = input.displacement.z;
	float shoreAlpha = input.displacement.z;
#		elif defined(DEPTH)
	float3 behind = -eyeDirection * length(sceneDepthPosition(screenUv));
	float planeDistance = dot(behind, perTechnique[4].xyz);
	float rayLength = length(behind);
	float submersion = 1.0 - perTechnique[4].w / planeDistance;
	float depthAlpha = saturate(1.0 - submersion * rayLength / perMaterial[10].w);
	float shoreAlpha = saturate(1.0 - submersion * abs(planeDistance) / perMaterial[10].w);
#		else
	float depthAlpha = saturate(perMaterial[8].z - (fade - 1.0) * (1.0 - perMaterial[8].z));
	float shoreAlpha = 1.0 - depthAlpha;
#		endif

	float opacity = pow(1.0 - smoothstep(perMaterial[12].y, perMaterial[12].x, depthAlpha), 0.33);
	float tint = lerp(perMaterial[12].z, perMaterial[12].w, opacity);
	float shoreBlend = smoothstep(perMaterial[11].x, 1.0, shoreAlpha);
	float strength = normalStrength(shoreAlpha);
	float3 blended = surfaceNormal(input, fade);
#		ifdef WADING
	float3 normal = displacedNormal(input.displacement.xy, normalize(blended));
#		else
	float3 normal = normalize(blended);
#		endif
	normal = normalize(lerp(float3(0.0, 0.0, 1.0), normal, strength));

	float grazing = 1.0 - saturate(dot(-eyeDirection, normal));
	float fresnel = fresnelTerm(grazing);
	float3 reflectionDirection = reflect(eyeDirection, normal);
	float slope = reflectionDirection.z;
	float3 water = surfaceColor(
		slope,
		screenUv,
		eyeDirection,
		normal,
		reflectionDirection,
		input.eyeVector.w,
		WaterDistanceBlendFactor(fade),
		skylightingSpecularVisibility(input.eyeVector.xyz, normal, eyeDirection));
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
	float sunGlare = pow(max(dot(-viewDirection, WATER_PER_GEOMETRY(2).xyz), 0.0), WATER_PER_GEOMETRY(3).w) * WATER_PER_GEOMETRY(2).w;
	fog = lerp(fog, WATER_PER_GEOMETRY(3).xyz, sunGlare);
#		ifndef INTERIOR
	float3 lightColor = WATER_PER_GEOMETRY(2).w * WATER_PER_GEOMETRY(3).xyz;
#			ifdef FO4_WATER_SUN_SHADOW
	lightColor *= FO4_WATER_SUN_SHADOW(FrameBuffer::ViewToWorld(input.eyeToPosition), input.screenPosition.xy);
#			endif
	float3 ambient = pow(saturate(dot(normal, float3(-0.099, -0.099, 0.990))), perMaterial[0].w) * lightColor;
	ambient = ambient * perMaterial[10].z;

	float3 viewNormal;
	viewNormal.x = dot(WorldToView[0], float4(normal, 1.0));
	viewNormal.y = dot(WorldToView[1], float4(normal, 1.0));
	viewNormal.z = dot(WorldToView[2], float4(normal, 1.0));
	float3 reflected = reflect(-viewDirection, viewNormal);
	float3 specular = pow(saturate(dot(reflected, WATER_PER_GEOMETRY(2).xyz)), perMaterial[8].x) * lightColor;
	float3 lighting = specular * perMaterial[1].w + ambient;
#		endif

	float reflectionWeight = fresnel * perMaterial[8].y;
#		if defined(DYNAMIC_CUBEMAPS)
	if (FO4SharedData::EnabledDynamicCubemaps != 0)
		reflectionWeight = fresnel;
#		endif
	float3 color = lerp(refraction, water, reflectionWeight);
	color = lerp(tinted, color, WaterDistanceBlendFactor(fade));
#		ifdef LIGHT_LIMIT_FIX
	[branch] if (FO4LightLimitFix::UseClusteredLights())
		color += fresnel * FO4LightLimitFix::GetWaterSpecularLighting(input.eyeToPosition, FrameBuffer::ViewToWorld(input.eyeToPosition), normal, perMaterial[13].w);
#		endif
#		ifndef INTERIOR
	color = color + lighting;
#		endif

	color = lerp(color, unclipped, shoreBlend);
	return float4(lerp(color, fog, fogAlpha), 0.0);
}

#	endif
#endif
