#ifndef DYNAMIC_CUBEMAPS_HLSLI
#define DYNAMIC_CUBEMAPS_HLSLI

#include "Common/SharedData.hlsli"

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
		if (SharedData::dynamicCubemapsSettings.Enabled == 0)
			return nativeSample;

		float3 environmentSample =
			EnvironmentTexture.SampleLevel(cubeSampler, direction, lod);
		float3 reflectionsSample =
			ReflectionsTexture.SampleLevel(cubeSampler, direction, lod);
		// An intentionally out-of-range LOD clamps to the coarsest published mip.
		float3 environmentCoarse =
			EnvironmentTexture.SampleLevel(cubeSampler, direction, 15.0);
		float nativeDirectionalAmbient = Luminance(ToLinear(nativeSample));
		float environmentLuminance = Luminance(ToLinear(environmentCoarse));
		float3 environmentSpecular =
			ToLinear(environmentSample) /
			max(environmentLuminance, 0.001) *
			nativeDirectionalAmbient;
		float3 skySpecular = SharedData::InInterior != 0 ?
			0.0 :
			ToLinear(max(0.0, reflectionsSample - environmentSample));
		return ToGamma(environmentSpecular + skySpecular);
	}

	float3 SampleDynamicEnvironment(
		SamplerState cubeSampler,
		float3 direction,
		float lod)
	{
		float3 environmentSample =
			EnvironmentTexture.SampleLevel(cubeSampler, direction, lod);
		float3 reflectionsSample =
			ReflectionsTexture.SampleLevel(cubeSampler, direction, lod);
		float3 skySample = SharedData::InInterior != 0 ?
			0.0 :
			max(0.0, reflectionsSample - environmentSample);
		return ToGamma(ToLinear(environmentSample) + ToLinear(skySample));
	}

	bool IsForwardSentinel(
		TextureCube<float4> nativeTexture,
		SamplerState cubeSampler)
	{
		if (SharedData::dynamicCubemapsSettings.Enabled == 0)
			return false;
		uint width;
		uint height;
		nativeTexture.GetDimensions(width, height);
		if (width != 1 || height != 1)
			return false;
		float3 sentinel = nativeTexture.SampleLevel(
			cubeSampler, float3(0.0, 1.0, 0.0), 15.0).rgb;
		return all(sentinel == 0.0);
	}

	float3 ApplyFullscreenDebug(float3 color, float3 contribution)
	{
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
		if (SharedData::dynamicCubemapsSettings.DebugVisualization ==
			DebugModeReflectionContribution)
			return SharedData::dynamicCubemapsSettings.Enabled != 0 ?
				max(contribution, 0.0) :
				0.0;
#endif
		return color;
	}
}

#endif
