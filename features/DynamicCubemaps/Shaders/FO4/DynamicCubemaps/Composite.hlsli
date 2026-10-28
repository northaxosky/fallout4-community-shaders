#ifndef DYNAMIC_CUBEMAPS_COMPOSITE_HLSLI
#define DYNAMIC_CUBEMAPS_COMPOSITE_HLSLI

#include "FO4/ImageBasedLightingConsumer.hlsli"
#include "FO4/SkylightingConsumer.hlsli"

// FO4's composite feature resources occupy the lower slots.
#define DYNAMIC_CUBEMAPS_ENVIRONMENT_REGISTER t39
#define DYNAMIC_CUBEMAPS_REFLECTIONS_REGISTER t40
#include "FO4/DynamicCubemaps/DynamicCubemaps.hlsli"
#undef DYNAMIC_CUBEMAPS_ENVIRONMENT_REGISTER
#undef DYNAMIC_CUBEMAPS_REFLECTIONS_REGISTER

// FO4: only IBL reads the native weight; other builds keep the stock signature.
#ifdef IBL
#	define FO4_PROBE_PARAMS Skylighting::SpecularVisibility skylightingSpecular, float3 nativeWeight
#	define FO4_PROBE_ARGS(skylightingSpecular, nativeWeight) skylightingSpecular, nativeWeight
#	define FO4_SPECULAR_LOBE(skylightingSpecular) skylightingSpecular.specular
#else
#	define FO4_PROBE_PARAMS Skylighting::SpecularVisibility skylightingSpecular = 1.0
#	define FO4_PROBE_ARGS(skylightingSpecular, nativeWeight) skylightingSpecular
#	define FO4_SPECULAR_LOBE(skylightingSpecular) skylightingSpecular
#endif

namespace DynamicCubemaps
{
	float3 GetNormalizedProbe(TextureCube<float3> cube, SamplerState probeSampler, float3 direction, float level)
	{
		float3 irradiance = cube.SampleLevel(probeSampler, direction, level);
		float average = Color::RGBToLuminance(cube.SampleLevel(probeSampler, direction, 15));
		return irradiance / max(average, 0.001);
	}

#ifdef IBL
	// FO4: weights below this carry no light for the native product to restore.
	static const float kMinNativeWeight = 1.0 / 1024.0;

	// Upstream composite IBL branch; the site applies nativeWeight.
	float3 GetIBLProbe(float3 direction, SamplerState probeSampler, float level, float authored,
		Skylighting::SpecularVisibility skylighting, float3 nativeWeight)
	{
		float3 envSample = EnvTexture.SampleLevel(probeSampler, direction, level);
		float3 fullSample = EnvReflectionsTexture.SampleLevel(probeSampler, direction, level);
		// FO4: normalized terms keep native weight; radiance divides it out.
		float3 envNative = 0.0;
		float3 envRadiance = 0.0;
		float3 skySpecular;
		if (SharedData::iblSettings.DALCMode >= 2) {
			// Mode 2/3: DALC-normalized env scaled by DALCAmount + sky overlay
			float envLum = Color::RGBToLuminance(EnvTexture.SampleLevel(probeSampler, direction, 15));
			envNative = ColorManagement::SceneToLinear((envSample / max(envLum, 0.001)) * authored);
			// FO4: native weighting multiplies an LDR authored pattern by surface light.
			envNative /= max(1.0, max(envNative.r, max(envNative.g, envNative.b)));
			envNative *= SharedData::iblSettings.DALCAmount;
			skySpecular = ColorManagement::SceneToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
#	ifdef SKYLIGHTING
			envNative *= (SharedData::iblSettings.DALCMode == 3) ? skylighting.visibility : 1.0;
			skySpecular *= skylighting.specular;
#	endif
		} else {
			// Mode 0/1: IBL ratio-based
			float3 ratio = ImageBasedLighting::GetIBLRatio();
			envRadiance = ColorManagement::SceneToLinear(envSample * ratio) * SharedData::iblSettings.EnvIBLScale;
			skySpecular = ColorManagement::SceneToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
#	ifdef SKYLIGHTING
			skySpecular *= skylighting.specular;
#	endif
		}
		if (SharedData::InInterior) {
			skySpecular = 0;
		}
		// FO4: a zero channel discards the product, so drop radiance there.
		float3 radiance = (nativeWeight > 0.0) ? (envRadiance + skySpecular) / max(nativeWeight, kMinNativeWeight) : 0.0;
		return envNative + radiance;
	}
#endif

	// FO4: vanilla envmaps cannot carry upstream sentinels, so the live cube replaces the authored pattern at its authored brightness.
	float3 GetMaterialEnvironment(TextureCubeArray<float4> nativeCube, SamplerState probeSampler,
		float3 R, float slice, float lod, float glossiness, float nativeRain,
		FO4_PROBE_PARAMS)
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
#ifdef IBL
		[branch] if (SharedData::iblSettings.EnableIBL)
		{
			// FO4: sky (depth 1) has no light for the product to restore.
			if (!any(nativeWeight > 0.0))
				return 0.0;
			uint envWidth, skyWidth, envHeight;
			EnvTexture.GetDimensions(envWidth, envHeight);
			EnvReflectionsTexture.GetDimensions(skyWidth, envHeight);
			if (envWidth == 0 || skyWidth == 0)
				return native;
			float authoredBrightness = Color::RGBToLuminance(nativeCube.SampleLevel(probeSampler, float4(R, slice), 15).xyz);
			return lerp(native, GetIBLProbe(direction, probeSampler, level, authoredBrightness, skylightingSpecular, nativeWeight), blend);
		}
#endif
		if (SharedData::InInterior) {
			EnvTexture.GetDimensions(width, height);
			normalized = GetNormalizedProbe(EnvTexture, probeSampler, direction, level);
		} else {
			EnvReflectionsTexture.GetDimensions(width, height);
#if defined(SKYLIGHTING)
			// FO4: sky visibility only selects the cube; native weighting carries it.
			float3 reflections = 0.0;
			float3 environment = 0.0;
			if (FO4_SPECULAR_LOBE(skylightingSpecular) > 0.0)
				reflections = GetNormalizedProbe(EnvReflectionsTexture, probeSampler, direction, level);
			if (FO4_SPECULAR_LOBE(skylightingSpecular) < 1.0)
				environment = GetNormalizedProbe(EnvTexture, probeSampler, direction, level);
			normalized = lerp(environment, reflections, FO4_SPECULAR_LOBE(skylightingSpecular));
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
