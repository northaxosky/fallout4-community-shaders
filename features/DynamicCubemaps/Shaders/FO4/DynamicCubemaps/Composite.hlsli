#ifndef DYNAMIC_CUBEMAPS_COMPOSITE_HLSLI
#define DYNAMIC_CUBEMAPS_COMPOSITE_HLSLI

// FO4's composite feature resources occupy the lower slots.
#define DYNAMIC_CUBEMAPS_ENVIRONMENT_REGISTER t34
#define DYNAMIC_CUBEMAPS_REFLECTIONS_REGISTER t35
#include "FO4/DynamicCubemaps/DynamicCubemaps.hlsli"
#undef DYNAMIC_CUBEMAPS_ENVIRONMENT_REGISTER
#undef DYNAMIC_CUBEMAPS_REFLECTIONS_REGISTER
#include "FO4/WetnessEffects/WetnessEffects.hlsli"

namespace DynamicCubemaps
{
	// FO4 composite sampler slots are exhausted, so DC cubes use the native probe sampler instead of upstream's LinearSampler.
	float3 GetFinalIrradiance(float3 N, float3 V, float roughness, SamplerState LinearSampler)
	{
		float3 R = reflect(-V, N);
		// FO4 shares composite permutations across interiors and exteriors, so INTERIOR becomes SharedData::InInterior.
		if (SharedData::InInterior) {
			return GetNormalizedSpecularIrradiance(EnvTexture, LinearSampler, R, roughness);
		} else {
			return GetNormalizedSpecularIrradiance(EnvReflectionsTexture, LinearSampler, R, roughness);
		}
	}

	// FO4 has no wet reflectance G-buffer, so the composite evaluates the film and transforms its view-space directions.
	float3 GetWetnessReflection(float3 normalView, float3 viewDir, float wetness, float roughness,
		float3x3 viewToWorld, SamplerState probeSampler)
	{
		float3 color = 0;
		if (SharedData::cubemapCreatorSettings.Enabled == 0)
			return color;
		float reflectance = WetnessEffects::GetEnvironmentFilmWeight(normalView, viewDir, wetness, roughness);
		if (reflectance > 0.0) {
			float3 N = normalize(mul(viewToWorld, normalView));
			float3 V = normalize(mul(viewToWorld, viewDir));
			color += reflectance * GetFinalIrradiance(N, V, roughness, probeSampler);
		}
		return color;
	}
}

#endif
