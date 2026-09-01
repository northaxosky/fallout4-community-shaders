#ifndef DYNAMIC_CUBEMAPS_HLSLI
#define DYNAMIC_CUBEMAPS_HLSLI

#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
#include "Common/SharedData.hlsli"
#endif

namespace DynamicCubemaps
{
	TextureCube<float3> EnvironmentTexture : register(t16);
	TextureCube<float3> ReflectionsTexture : register(t17);
	static const uint DebugModeReflectionContribution = 3;

	float Luminance(float3 color)
	{
		return dot(color, float3(0.299, 0.587, 0.114));
	}

	float3 ToLinear(float3 color)
	{
		return pow(abs(color), 1.6);
	}

	float3 ToGamma(float3 color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}

	float3 SampleEnvironment(
		TextureCubeArray<float4> nativeTexture,
		SamplerState cubeSampler,
		float3 direction,
		float lod,
		bool hasNativeProbe,
		float nativeSlice)
	{
		if (!hasNativeProbe)
			return 0.0;

		float3 nativeSample = nativeTexture.SampleLevel(
			cubeSampler, float4(direction, nativeSlice), lod).rgb;
		float3 dynamicSample =
			ReflectionsTexture.SampleLevel(cubeSampler, direction, lod);
		float3 dynamicCoarse =
			ReflectionsTexture.SampleLevel(cubeSampler, direction, 8.0);
		float coarseLuminance = Luminance(ToLinear(dynamicCoarse));
		float nativeDirectionalAmbient = Luminance(ToLinear(nativeSample));
		float3 normalized =
			ToLinear(dynamicSample) / max(coarseLuminance, 0.001);
		return ToGamma(normalized * nativeDirectionalAmbient);
	}

	float3 ApplyFullscreenDebug(float3 color, float3 contribution)
	{
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
		if (SharedData::dynamicCubemapsSettings.DebugVisualization ==
			DebugModeReflectionContribution)
			return max(contribution, 0.0);
#endif
		return color;
	}
}

#endif
