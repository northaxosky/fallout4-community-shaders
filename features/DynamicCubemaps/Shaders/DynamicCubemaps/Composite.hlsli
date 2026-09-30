#ifndef DYNAMIC_CUBEMAPS_COMPOSITE_HLSLI
#define DYNAMIC_CUBEMAPS_COMPOSITE_HLSLI

#include "DynamicCubemaps/CubemapCommon.hlsli"
#include "FO4/FO4ShaderData.hlsli"
#include "FO4/WetnessEffects/WetnessEffects.hlsli"

namespace DynamicCubemaps
{
	// FO4's composite feature slots below t34 are occupied, so the uncompressed cubes use t34-t35.
	TextureCube<float3> EnvTexture : register(t34);
	TextureCube<float3> EnvReflectionsTexture : register(t35);

	// FO4 composite sampler slots are exhausted, so DC cubes use the native probe sampler instead of upstream's LinearSampler.
	float3 GetFinalIrradiance(float3 N, float3 V, float roughness, SamplerState LinearSampler)
	{
		float3 R = reflect(-V, N);
		float level = roughness * 8.0;
		float3 finalIrradiance = 0;
		// FO4 directional ambient is already linear, so upstream's Color::Ambient is identity.
		// FO4 reflection normalization consumes linear DALC.
		float directionalAmbientColorSpecular = RGBToLuminance(FO4SharedData::GetAmbientLinear(R)) * ReflectionNormalisationScale;

		// Fallback without IBL: normalize-by-luminance with DALC
		// FO4 shares composite permutations across interiors and exteriors, so INTERIOR becomes SharedData::InInterior.
		if (SharedData::InInterior) {
			float3 specularIrradiance = EnvTexture.SampleLevel(LinearSampler, R, level);
			float specularIrradianceLuminance = RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = IrradianceToLinear(specularIrradiance);
		} else {
			float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level);
			float specularIrradianceLuminance = RGBToLuminance(EnvReflectionsTexture.SampleLevel(LinearSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = IrradianceToLinear(specularIrradiance);
		}
		return finalIrradiance;
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
