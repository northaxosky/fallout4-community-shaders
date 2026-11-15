#ifndef FO4_VANILLA_PBR_HLSLI
#define FO4_VANILLA_PBR_HLSLI

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/Shading.hlsli"
#include "FO4/FO4ShaderData.hlsli"

namespace VanillaPBR
{
	bool UseGGX(float2 pixelPosition)
	{
		return FO4SharedData::VanillaPBREnable != 0 &&
		       FO4SharedData::VanillaPBREnableGGX != 0 &&
		       (FO4SharedData::VanillaPBRSplitScreen == 0 ||
				   FO4SharedData::VanillaPBRSplitMode == 1 ||
				   pixelPosition.x >= FO4SharedData::VanillaPBRSplitPosition * SharedData::BufferDim.x * FrameBuffer::DynamicResolutionParams1.x);
	}

	bool IsEligible(float materialClass)
	{
		float materialId = materialClass * 255.0;
		return abs(materialId - 255.0) < 0.25 || abs(materialId - 254.0) < 0.25;
	}

	bool IsEye(float materialClass)
	{
		return abs(materialClass * 255.0 - 254.0) < 0.25;
	}

	bool UseEyeSpecialHandling(float materialClass)
	{
		return IsEye(materialClass) && FO4SharedData::VanillaPBREnableEyeSpecialHandling != 0;
	}

	// One gate for the live cube; false keeps the pixel stock.
	bool UseLiveEnvironment(float2 pixelPosition, float materialClass, bool hasEnvmap)
	{
		return hasEnvmap && !UseEyeSpecialHandling(materialClass) &&
		       FO4SharedData::VanillaPBREnable != 0 &&
		       FO4SharedData::VanillaPBRLiveEnvironment != 0 &&
		       (FO4SharedData::VanillaPBRSplitScreen == 0 ||
				   FO4SharedData::VanillaPBRSplitMode == 0 ||
				   pixelPosition.x >= FO4SharedData::VanillaPBRSplitPosition * SharedData::BufferDim.x * FrameBuffer::DynamicResolutionParams1.x);
	}

	float Exponent(float gloss, bool directional, float wetness)
	{
		float exponent = exp2(10.0 * gloss + 1.0);
		if (directional) {
			float q = 1.0 - pow(1.0 - saturate(wetness), 5.0);
			exponent *= 1.0 - 0.98 * q;
		}
		return exponent;
	}

	float Roughness(float gloss, bool directional, float wetness, float materialClass)
	{
		[branch] if (UseEyeSpecialHandling(materialClass)) return clamp(0.1 * FO4SharedData::VanillaPBRRoughnessMultiplier, 0.04, 1.0);
		float rShininess = pow(2.0 / (Exponent(gloss, directional, wetness) + 2.0), 0.25);
		float rSpecular = (1.0 - gloss) * (1.0 - gloss);
		return clamp(lerp(rShininess, rSpecular, FO4SharedData::VanillaPBRSpecularRoughnessBlend * (1.0 - gloss)) *
						 FO4SharedData::VanillaPBRRoughnessMultiplier,
			0.04, 1.0);
	}

	// FO4: the specular intensity is the authored reflectance at normal incidence.
	float F0(float intensity)
	{
		return saturate(max(FO4SharedData::VanillaPBRMinF0, intensity * FO4SharedData::VanillaPBRBaseF0Multiplier));
	}

	// Authored envmap average: last mip at the six face centers.
	float3 NativeProbeAverage(TextureCubeArray<float4> probe, SamplerState probeSampler, float slice)
	{
		float3 sum = probe.SampleLevel(probeSampler, float4(1.0, 0.0, 0.0, slice), 15.0).xyz +
		             probe.SampleLevel(probeSampler, float4(-1.0, 0.0, 0.0, slice), 15.0).xyz +
		             probe.SampleLevel(probeSampler, float4(0.0, 1.0, 0.0, slice), 15.0).xyz +
		             probe.SampleLevel(probeSampler, float4(0.0, -1.0, 0.0, slice), 15.0).xyz +
		             probe.SampleLevel(probeSampler, float4(0.0, 0.0, 1.0, slice), 15.0).xyz +
		             probe.SampleLevel(probeSampler, float4(0.0, 0.0, -1.0, slice), 15.0).xyz;
		return sum / 6.0;
	}

	// FO4: warm saturated envmaps author metal tint; neutral and blue add none.
	float3 AuthoredHue(float3 probeAverage)
	{
		float luminance = max(Color::RGBToLuminance(probeAverage), 1e-4);
		float peak = max(probeAverage.r, max(probeAverage.g, probeAverage.b));
		float saturation = (peak - min(probeAverage.r, min(probeAverage.g, probeAverage.b))) / max(peak, 1e-4);
		float tint = probeAverage.r >= probeAverage.b ? saturate((saturation - 0.4) / 0.3) : 0.0;
		return lerp(1.0, probeAverage / luminance, tint);
	}

	float3 Evaluate(float3 N, float3 V, float3 L, float gloss, float intensity,
		bool directional, float wetness, float materialClass)
	{
		float3 halfVector = L + V;
		halfVector *= rsqrt(max(dot(halfVector, halfVector), 1e-12));
		float NdotL = saturate(dot(N, L));
		float NdotV = saturate(dot(N, V));
		float NdotH = saturate(dot(N, halfVector));
		float VdotH = saturate(dot(V, halfVector));
		if (NdotL <= 0.0 || NdotV <= 0.0)
			return 0.0;
		float roughness = Roughness(gloss, directional, wetness, materialClass);
		// FO4: ineligible classes keep the intensity-masked lobe at base F0.
		bool masked = !IsEligible(materialClass);
		float f0 = UseEyeSpecialHandling(materialClass) ? 0.027 :
		           masked                               ? saturate(FO4SharedData::VanillaPBRBaseF0Multiplier) :
		                                                  F0(intensity);
		float2 envBRDF = BRDF::EnvBRDF(roughness, NdotV);
		float compensation = 1.0 + f0 * (rcp(max(envBRDF.x + envBRDF.y, 1e-4)) - 1.0);
		float3 specular = BRDF::D_GGX(roughness, NdotH) *
		                  BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL) *
		                  BRDF::F_Schlick(f0.xxx, 1.0.xxx, VdotH) * compensation;
		if (masked)
			specular *= intensity;
		// FO4: native specular accumulation is not divided by three.
		return specular * (NdotL * Math::PI);
	}

#ifdef VANILLA_PBR_FULLSCREEN_DEBUG
	bool IsDebugViewActive()
	{
		return FO4SharedData::DebugOwner == FullscreenDebugOwner::VanillaPBR && FO4SharedData::DebugMode != 0;
	}
#endif
}
#endif
