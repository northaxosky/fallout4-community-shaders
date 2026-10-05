#ifndef DYNAMIC_CUBEMAPS_COMPOSITE_HLSLI
#define DYNAMIC_CUBEMAPS_COMPOSITE_HLSLI

// FO4's composite feature resources occupy the lower slots.
#define DYNAMIC_CUBEMAPS_ENVIRONMENT_REGISTER t34
#define DYNAMIC_CUBEMAPS_REFLECTIONS_REGISTER t35
#include "FO4/DynamicCubemaps/DynamicCubemaps.hlsli"
#undef DYNAMIC_CUBEMAPS_ENVIRONMENT_REGISTER
#undef DYNAMIC_CUBEMAPS_REFLECTIONS_REGISTER

namespace DynamicCubemaps
{
	// FO4: vanilla envmaps cannot carry upstream sentinels, so the live cube replaces the authored pattern at its authored brightness.
	float3 GetMaterialEnvironment(TextureCubeArray<float4> nativeCube, SamplerState probeSampler,
		float3 R, float slice, float lod, float glossiness, float nativeRain)
	{
		float3 native = nativeCube.SampleLevel(probeSampler, float4(R, slice), lod).xyz;
		// Native rain boosts and greys this reflection; the live cube would turn that into chrome, so rain keeps the authored cube.
		float blend = FO4SharedData::DynamicMaterialReflections * (1.0 - saturate(nativeRain));
		if (blend <= 0.0)
			return native;
		// FO4 probe sites pass the negated reflection vector; DC cubes use the upstream reflect(-V, N) orientation.
		float3 direction = -R;
		uint width, height;
		float3 irradiance, average;
		float level = saturate(1.0 - glossiness) * 8.0;
		if (SharedData::InInterior) {
			EnvTexture.GetDimensions(width, height);
			irradiance = EnvTexture.SampleLevel(probeSampler, direction, level);
			average = EnvTexture.SampleLevel(probeSampler, direction, 15);
		} else {
			EnvReflectionsTexture.GetDimensions(width, height);
			irradiance = EnvReflectionsTexture.SampleLevel(probeSampler, direction, level);
			average = EnvReflectionsTexture.SampleLevel(probeSampler, direction, 15);
		}
		if (width == 0)
			return native;
		float authored = Color::RGBToLuminance(nativeCube.SampleLevel(probeSampler, float4(R, slice), 15).xyz);
		float3 live = irradiance / max(Color::RGBToLuminance(average), 0.001) * authored;
		// FO4: native weighting multiplies an LDR authored pattern by surface light, so HDR live peaks stay in that range.
		live /= max(1.0, max(live.r, max(live.g, live.b)));
		return lerp(native, live, blend);
	}
}

#endif
