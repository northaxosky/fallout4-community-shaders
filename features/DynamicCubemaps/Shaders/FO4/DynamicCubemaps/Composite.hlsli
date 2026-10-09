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
	float3 GetNormalizedProbe(TextureCube<float3> cube, SamplerState probeSampler, float3 direction, float level)
	{
		float3 irradiance = cube.SampleLevel(probeSampler, direction, level);
		float average = Color::RGBToLuminance(cube.SampleLevel(probeSampler, direction, 15));
		return irradiance / max(average, 0.001);
	}

	// FO4: vanilla envmaps cannot carry upstream sentinels, so the live cube replaces the authored pattern at its authored brightness.
	float3 GetMaterialEnvironment(TextureCubeArray<float4> nativeCube, SamplerState probeSampler,
		float3 R, float slice, float lod, float glossiness, float nativeRain, float skylightingSpecular = 1.0)
	{
		// FO4 probe sites pass -R; DC cubes use upstream reflect(-V, N).
		float3 direction = -R;
		float3 debugColor;
		if (TryGetDebugEnvironment(direction, probeSampler, debugColor))
			return debugColor;
		float3 native = nativeCube.SampleLevel(probeSampler, float4(R, slice), lod).xyz;
		// Native rain boosts and greys this reflection; the live cube would turn that into chrome, so rain keeps the authored cube.
		float blend = FO4SharedData::DynamicMaterialReflections * (1.0 - saturate(nativeRain));
		if (blend <= 0.0)
			return native;
		uint width, height;
		float3 normalized;
		float level = saturate(1.0 - glossiness) * 8.0;
		if (SharedData::InInterior) {
			EnvTexture.GetDimensions(width, height);
			normalized = GetNormalizedProbe(EnvTexture, probeSampler, direction, level);
		} else {
			EnvReflectionsTexture.GetDimensions(width, height);
#if defined(SKYLIGHTING)
			// FO4: sky visibility only selects the cube; native weighting carries it.
			float3 reflections = 0.0;
			float3 environment = 0.0;
			if (skylightingSpecular > 0.0)
				reflections = GetNormalizedProbe(EnvReflectionsTexture, probeSampler, direction, level);
			if (skylightingSpecular < 1.0)
				environment = GetNormalizedProbe(EnvTexture, probeSampler, direction, level);
			normalized = lerp(environment, reflections, skylightingSpecular);
#else
			normalized = GetNormalizedProbe(EnvReflectionsTexture, probeSampler, direction, level);
#endif
		}
		if (width == 0)
			return native;
		float authored = Color::RGBToLuminance(nativeCube.SampleLevel(probeSampler, float4(R, slice), 15).xyz);
		float3 live = normalized * authored;
		// FO4: native weighting multiplies an LDR authored pattern by surface light, so HDR live peaks stay in that range.
		live /= max(1.0, max(live.r, max(live.g, live.b)));
		return lerp(native, live, blend);
	}
}

#endif
