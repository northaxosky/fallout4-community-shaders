// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#ifndef __WETNESS_EFFECTS_DEPENDENCY_HLSL__
#define __WETNESS_EFFECTS_DEPENDENCY_HLSL__

#include "Common/SharedData.hlsli"
#include "Common/Random.hlsli"
#ifdef WETNESS_COMPOSITE_CONSUMER
#include "Common/DeferredPosition.hlsli"
#endif

namespace WetnessEffects
{
#ifdef WETNESS_COMPOSITE_CONSUMER
	// authoritative engine G-buffer normal, rebound per injected composite draw
	Texture2D<float4> GbufferNormal : register(t25);
	// FO4 composites without native depth inputs need a scoped scene-depth binding for shore position.
	Texture2D<float> SceneDepthTexture : register(t36);
#endif

	static const float FilmF0 = 0.02;
	static const float MaxFilmSpecularMagnitude = 15.0;
	// FO4's six reconstructed BSDFLight families normalize traditional specular with pi
	static const float FilmSpecularScale = 3.1415927;
	static const float DotClampEpsilon = 1e-5;
	static const float LengthSquaredEpsilon = 1e-8;
	static const uint DebugModeWetnessTerm = 1;
	static const uint DebugModeWorldUp = 2;

	// FO4 deferred passes use the shading normal because the vertex normal is unavailable.
	float GetWorldUp(float3 normalView, float4 worldUpView)
	{
		return dot(normalView, worldUpView.xyz);
	}

	struct Surface
	{
		float3 normalView;
		float worldUp;
		float wetness;
		float waterRoughness;
		float glossinessAlbedo;
	};

	// FO4 reconstructs absolute world position from b12 rows and camera adjustment; view Z is forward depth.
	Surface GetSurface(float3 normalView, float3 viewPosition,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2, float4 cameraPosAdjust)
	{
		Surface surface;
		surface.normalView = normalView;
		surface.worldUp = GetWorldUp(normalView, viewToWorldRow2);
		float3 worldPosition = float3(
			dot(viewToWorldRow0, float4(viewPosition, 1.0)),
			dot(viewToWorldRow1, float4(viewPosition, 1.0)),
			dot(viewToWorldRow2, float4(viewPosition, 1.0))) + cameraPosAdjust.xyz;
		float nearFactor = smoothstep(4096.0 * 2.5, 0.0, viewPosition.z);

		// FO4 publishes the player cell's water plane instead of upstream's per-tile water data.
		bool hasWater = SharedData::waterEffectsSettings.HasWater != 0;
		float waterHeight = SharedData::waterEffectsSettings.WaterHeight;
		// Calculate shore wetness factors
		float wetnessDistToWater = abs(worldPosition.z - waterHeight);
		float shoreFactor = hasWater ?
			saturate(1.0 - (wetnessDistToWater / SharedData::wetnessEffectsSettings.ShoreRange)) : 0.0;
		float shoreFactorAlbedo = hasWater && worldPosition.z < waterHeight ? 1.0 : shoreFactor;

		// Calculate wetness angle and occlusion
		float minWetnessValue = SharedData::wetnessEffectsSettings.MinRainWetness;
		float minWetnessAngle = saturate(max(minWetnessValue, surface.worldUp));
		// FO4 has no skylighting; these deferred consumers shade world geometry.
		float wetnessOcclusion = 1.0;

		// Calculate different wetness types
		float rainWetness = SharedData::wetnessEffectsSettings.Wetness * minWetnessAngle * SharedData::wetnessEffectsSettings.MaxRainWetness;
		float shoreWetness = shoreFactor * SharedData::wetnessEffectsSettings.MaxShoreWetness;
		float wetness = max(shoreWetness, rainWetness);

		// Calculate puddle effects
		float puddleWetness = SharedData::wetnessEffectsSettings.PuddleWetness * minWetnessAngle;
		float puddle = wetness;
		if (wetness > 0.0 || puddleWetness > 0.0) {
			float3 puddleCoords = (worldPosition * 0.5 + 0.5) * 0.01 / SharedData::wetnessEffectsSettings.PuddleRadius;
			puddle = Random::perlinNoise(puddleCoords) * 0.5 + 0.5;
			puddle = puddle * ((minWetnessAngle / SharedData::wetnessEffectsSettings.PuddleMaxAngle) * SharedData::wetnessEffectsSettings.MaxPuddleWetness * 0.25) + 0.5;
			puddle *= lerp(wetness, puddleWetness, saturate(puddle - 0.25));
		}

		// Apply occlusion and distance factors
		puddle *= saturate(wetnessOcclusion * 2.0) * nearFactor;

		// Calculate wetness glossiness factors
		float wetnessGlossinessAlbedo = max(puddle, shoreFactorAlbedo * SharedData::wetnessEffectsSettings.MaxShoreWetness);
		wetnessGlossinessAlbedo *= wetnessGlossinessAlbedo;

		float wetnessGlossinessSpecular = puddle;
		if (hasWater && worldPosition.z < waterHeight) {
			wetnessGlossinessSpecular *= shoreFactor;
		}

		// Minimum roughness prevents an extreme retroreflective peak (NdotH→1) for near-zero
		// roughness puddles. Real water has ripples and surface tension that keep it from being
		// optically perfect; the ripple normal map adds micro-variation but GGX still peaks
		// sharply without this floor.
		static const float wetnessMinPuddleRoughness = 0.05;
		surface.wetness = wetness;
		surface.waterRoughness = max(saturate(1.0 - wetnessGlossinessSpecular), wetnessMinPuddleRoughness);
		surface.glossinessAlbedo = wetnessGlossinessAlbedo;
		return surface;
	}

	float FilmStrength(float filmRoughness)
	{
		return saturate(1.0 - filmRoughness);
	}

#ifdef WETNESS_COMPOSITE_CONSUMER
	// t25 outside the prepass encode domain (including an explicit null bind) is identity
	Surface GetSurfaceFromScreenPosition(float2 screenPosition,
		float3x4 viewToWorld, float4 cameraPosAdjust,
		float4x4 farReprojection, float4x4 nearReprojection)
	{
		Surface surface;
		surface.normalView = float3(0.0, 0.0, -1.0);
		surface.wetness = 0.0;
		surface.worldUp = 0.0;
		surface.waterRoughness = 1.0;
		surface.glossinessAlbedo = 0.0;

		float2 encoded =
			GbufferNormal.Load(int3(int2(screenPosition), 0)).xy * 4.0 - 2.0;
		float encodedLengthSquared = dot(encoded, encoded);
		// a NaN encoding fails this test and keeps the identity surface
		[branch] if (encodedLengthSquared <= 4.0) {
			float3 normalView = float3(
				encoded * sqrt(1.0 - encodedLengthSquared * 0.25),
				-(1.0 - encodedLengthSquared * 0.5));
			float3 viewPosition;
			if (DeferredPosition::TryGetViewPositionFromScreenPosition(
					SceneDepthTexture, screenPosition, farReprojection, nearReprojection, viewPosition)) {
				surface = GetSurface(normalView, viewPosition,
					viewToWorld[0], viewToWorld[1], viewToWorld[2], cameraPosAdjust);
			}
		}
		return surface;
	}

	bool TryGetDebugColor(Surface surface, out float4 color)
	{
		color = 0.0;
		uint mode = SharedData::wetnessEffectsSettings.DebugVisualization;
		if (mode == DebugModeWetnessTerm) {
			color = float4(surface.wetness.xxx, 1.0);
			return true;
		}
		if (mode == DebugModeWorldUp) {
			color = float4(saturate(surface.worldUp).xxx, 1.0);
			return true;
		}
		return false;
	}

	bool TryGetDebugColorFromScreenPosition(
		float2 screenPosition,
		float3x4 viewToWorld, float4 cameraPosAdjust,
		float4x4 farReprojection, float4x4 nearReprojection,
		out float4 color)
	{
		return TryGetDebugColor(
			GetSurfaceFromScreenPosition(screenPosition,
				viewToWorld, cameraPosAdjust, farReprojection, nearReprojection),
			color);
	}
#endif

	// upstream substrate darkening with material porosity fixed at 1
	float3 WetAlbedo(float3 baseColor, float glossinessAlbedo)
	{
		float3 wetColor = baseColor;
		[branch] if (glossinessAlbedo > 0.0) {
			wetColor = lerp(
				baseColor, pow(abs(baseColor), 1.0 + glossinessAlbedo), 0.5);
		}
		return wetColor;
	}

	float D_GGX(float roughness, float NdotH)
	{
		float a = roughness * roughness;
		float a2 = a * a;
		float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
		return a2 / (3.1415927 * d * d);
	}

	float Vis_SmithJointApprox(float roughness, float NdotV, float NdotL)
	{
		float a = roughness * roughness;
		float visSmithV = NdotL * (NdotV * (1.0 + a) + a);
		float visSmithL = NdotV * (NdotL * (1.0 + a) + a);
		return 0.5 / max(visSmithV + visSmithL, 1e-6);
	}

	float F_Schlick(float f0, float VdotH)
	{
		float fc = pow(1.0 - VdotH, 5);
		return fc + (1.0 - fc) * f0;
	}

	// [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
	float2 EnvBRDF(float roughness, float NdotV)
	{
		const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
		const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
		float4 r = roughness * c0 + c1;
		float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
		return float2(-1.04, 1.04) * a004 + r.zw;
	}

	// per-light water film: attenuates the native lobes and adds its own GGX lobe
	void ApplyDirectCoat(
		float3 normalView,
		float3 viewDir,
		float3 lightDir,
		float3 lightColor,
		float wetness,
		float roughness,
		inout float3 diffuse,
		inout float3 specular)
	{
		[branch] if (wetness > 0.0) {
			float strength = FilmStrength(roughness);

			float3 halfVector = viewDir + lightDir;
			halfVector *= rsqrt(
				max(dot(halfVector, halfVector), LengthSquaredEpsilon));

			float NdotL = clamp(dot(normalView, lightDir), DotClampEpsilon, 1.0);
			float NdotV = saturate(abs(dot(normalView, viewDir)) + DotClampEpsilon);
			float NdotH = saturate(dot(normalView, halfVector));
			float VdotH = saturate(dot(viewDir, halfVector));

			float D = D_GGX(roughness, NdotH);
			float G = Vis_SmithJointApprox(roughness, NdotV, NdotL);
			float fresnel = F_Schlick(FilmF0, VdotH);
			float filmFresnel = fresnel * strength;
			float filmBrdf = min(D * G * fresnel, MaxFilmSpecularMagnitude);

			float3 film =
				filmBrdf * strength * NdotL * lightColor * FilmSpecularScale;

			diffuse *= 1.0 - filmFresnel;
			specular *= 1.0 - filmFresnel;
			specular += film;
		}
	}

	// upstream GetWetnessIndirectLobeWeights: environment-BRDF weighted film Fresnel
	float GetEnvironmentFilmWeight(float3 normalView, float3 viewDir, float wetness, float roughness)
	{
		float weight = 0.0;
		[branch] if (wetness > 0.0) {
			float NdotV = saturate(abs(dot(normalView, viewDir)) + DotClampEpsilon);
			float2 environmentBRDF = EnvBRDF(roughness, NdotV);
			weight = (FilmF0 * environmentBRDF.x + environmentBRDF.y) *
				FilmStrength(roughness);
		}
		return weight;
	}

#if defined(DYNAMIC_CUBEMAPS)
	// FO4 evaluates indirect diffuse in light passes rather than upstream's material pass.
	float GetIndirectDiffuseWeight(float3 normalView, float3 viewDir, float3 viewPosition,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2, float4 cameraPosAdjust)
	{
		// FO4 toggles Dynamic Cubemaps live, so the film only takes energy while its reflection is supplied.
		if (SharedData::dynamicCubemapsSettings.Enabled == 0)
			return 1.0;
		Surface surface = GetSurface(normalView, viewPosition,
			viewToWorldRow0, viewToWorldRow1, viewToWorldRow2, cameraPosAdjust);
		return 1.0 - GetEnvironmentFilmWeight(normalView, viewDir, surface.wetness, surface.waterRoughness);
	}
#endif
}

#endif  // __WETNESS_EFFECTS_DEPENDENCY_HLSL__
