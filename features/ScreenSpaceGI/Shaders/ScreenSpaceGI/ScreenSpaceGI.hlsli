// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#ifndef __SCREEN_SPACE_GI_DEPENDENCY_HLSL__
#define __SCREEN_SPACE_GI_DEPENDENCY_HLSL__

#include "Common/SharedData.hlsli"
#include "Common/Shading.hlsli"

#include "Common/Color.hlsli"
#include "Common/SphericalHarmonics.hlsli"

#ifdef WETNESS_EFFECTS
#include "WetnessEffects/WetnessEffects.hlsli"
#endif

namespace ScreenSpaceGI
{
	// occlusion: 0 open, 1 occluded
	Texture2D<float> OcclusionTexture : register(t26);
	Texture2D<float4> BounceLumaTexture : register(t27);
	Texture2D<float2> BounceChromaTexture : register(t28);
	Texture2D<float4> AlbedoTexture : register(t29);

	float3 DecodeViewNormal(float2 encodedNormal)
	{
		float2 remapped = encodedNormal * 4.0 - 2.0;
		float lengthSquared = dot(remapped, remapped);
		return float3(
			remapped * sqrt(1.0 - lengthSquared * 0.25),
			-(1.0 - lengthSquared * 0.5));
	}

	// direct and ambient arrive gamma-encoded
	float3 ComposeAmbient(
		float2 screenPosition,
		float3 viewNormal,
		float3x3 viewToWorld,
		float3 directLighting,
		float3 directionalAmbient,
		float engineAmbientOcclusion,
		float wetness = 0.0)
	{
		if (!SharedData::screenSpaceGISettings.EnableScreenSpaceGI)
			return (directLighting + directionalAmbient) * engineAmbientOcclusion;

		int3 texel = int3(int2(screenPosition), 0);
		float visibility = pow(
			saturate(1.0 - OcclusionTexture.Load(texel)),
			SharedData::screenSpaceGISettings.AoPower);

#ifdef WETNESS_EFFECTS
		float3 albedo = saturate(
			WetnessEffects::WetAlbedo(AlbedoTexture.Load(texel).rgb, wetness));
#else
		float3 albedo = saturate(AlbedoTexture.Load(texel).rgb);
#endif
		float3 linearAlbedo =
			Color::IrradianceToLinear(albedo / Color::PBRLightingScale);
		float3 multiBounceAO = Shading::MultiBounceAO(linearAlbedo, visibility);

		float3 directColor = Color::IrradianceToGamma(
			Color::IrradianceToLinear(directLighting) * sqrt(multiBounceAO));
		float3 ambientColor = Color::IrradianceToGamma(
			Color::IrradianceToLinear(directionalAmbient) * multiBounceAO);

		float3 bounce = 0.0;
		float4 luma = BounceLumaTexture.Load(texel);
		if (luma.x > 1e-6) {
			float2 chroma = BounceChromaTexture.Load(texel);
			float3 worldNormal = normalize(mul(viewToWorld, viewNormal));
			float irradianceY =
				SphericalHarmonics::SHHallucinateZH3Irradiance(luma, worldNormal);
			float3 irradiance = max(0.0, Color::YCoCgToRGB(float3(irradianceY, chroma)));
			bounce = irradiance * linearAlbedo *
				SharedData::screenSpaceGISettings.BounceStrength;
		}

		return Color::IrradianceToGamma(
			Color::IrradianceToLinear(directColor + ambientColor) + bounce);
	}
}

#endif  // __SCREEN_SPACE_GI_DEPENDENCY_HLSL__
