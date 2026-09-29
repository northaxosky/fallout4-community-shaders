// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
// Ported from Skyrim Community Shaders d330bf12d DeferredCompositeCS.
#ifndef __SCREEN_SPACE_GI_DEPENDENCY_HLSL__
#define __SCREEN_SPACE_GI_DEPENDENCY_HLSL__

#include "Common/SharedData.hlsli"
#include "Common/Shading.hlsli"

#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "../Common/SphericalHarmonics.hlsli"

namespace ScreenSpaceGI
{
	// occlusion: 0 open, 1 occluded
	Texture2D<float> OcclusionTexture : register(t26);
	Texture2D<float4> BounceLumaTexture : register(t27);
	Texture2D<float2> BounceChromaTexture : register(t28);
	Texture2D<float4> NormalTexture : register(t29);

	float3 DecodeViewNormal(float2 encodedNormal)
	{
		float2 remapped = encodedNormal * 4.0 - 2.0;
		float lengthSquared = dot(remapped, remapped);
		return float3(
			remapped * sqrt(1.0 - lengthSquared * 0.25),
			-(1.0 - lengthSquared * 0.5));
	}

	// Ambient the deferred lights fold into diffuse.
	float3 DirectionalAmbient(float3 worldNormal)
	{
		float4 normal = float4(worldNormal, 1.0);
		float3 encoded = float3(
			dot(SharedData::screenSpaceGISettings.DirectionalAmbient[0], normal),
			dot(SharedData::screenSpaceGISettings.DirectionalAmbient[1], normal),
			dot(SharedData::screenSpaceGISettings.DirectionalAmbient[2], normal));
		return exp2(log2(encoded) * 2.2);
	}

	// diffuseColor excludes specular, as upstream.
	// vertexAOStore is MRT4 alpha: 1 - vertexAO from the prepass.
	float3 ComposeDiffuse(
		float2 screenPosition,
		float3x3 viewToWorld,
		float3 albedo,
		float3 diffuseColor,
		float vertexAOStore)
	{
		if (!SharedData::screenSpaceGISettings.EnableScreenSpaceGI)
			return diffuseColor;

		int3 texel = int3(int2(screenPosition), 0);
		float3 normalVS = DecodeViewNormal(NormalTexture.Load(texel).xy);
		float3 normalWS = normalize(mul(viewToWorld, normalVS));

		float ssgiAo = 1 - OcclusionTexture.Load(texel);
		// Uncovered pixels keep stale alpha, but carry no occlusion there, so the ratio saturates to 1.
		float vertexAO = 1.0 - vertexAOStore;
		ssgiAo = saturate(ssgiAo / max(vertexAO, EPSILON_DIVISION));
		float4 ssgiIlYSh = BounceLumaTexture.Load(texel);
		float ssgiIlY = SphericalHarmonics::SHHallucinateZH3Irradiance(ssgiIlYSh, normalWS);
		float2 ssgiIlCoCg = BounceChromaTexture.Load(texel);
		float3 ssgiIl = max(0, Color::YCoCgToRGB(float3(ssgiIlY, ssgiIlCoCg)));

		float3 linAlbedo = Color::IrradianceToLinear(albedo / Color::PBRLightingScale);
		float3 multiBounceSSGIAo = Shading::MultiBounceAO(linAlbedo, ssgiAo);

		float3 directionalAmbientColor = max(0, DirectionalAmbient(normalWS) * albedo);

		float maxScale = 1.0;
		if (directionalAmbientColor.x > 0.0)
			maxScale = min(maxScale, diffuseColor.x / directionalAmbientColor.x);
		if (directionalAmbientColor.y > 0.0)
			maxScale = min(maxScale, diffuseColor.y / directionalAmbientColor.y);
		if (directionalAmbientColor.z > 0.0)
			maxScale = min(maxScale, diffuseColor.z / directionalAmbientColor.z);
		directionalAmbientColor *= maxScale;

		diffuseColor = max(0.0, diffuseColor - directionalAmbientColor);
		float3 linDiffuseColor = Color::IrradianceToLinear(diffuseColor);
		linDiffuseColor *= sqrt(multiBounceSSGIAo);
		diffuseColor = Color::IrradianceToGamma(linDiffuseColor);
		diffuseColor += Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColor) * multiBounceSSGIAo);
		linDiffuseColor = Color::IrradianceToLinear(diffuseColor);

		linDiffuseColor += ssgiIl * linAlbedo;
		return Color::IrradianceToGamma(linDiffuseColor);
	}
}

#endif  // __SCREEN_SPACE_GI_DEPENDENCY_HLSL__
