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

// FO4: IBL builds pair the specular lobe with visibility.
#ifdef IBL
#	define FO4_SPECULAR_LOBE(skylightingSpecular) skylightingSpecular.specular
#else
#	define FO4_SPECULAR_LOBE(skylightingSpecular) skylightingSpecular
#endif

namespace DynamicCubemaps
{
	// FO4: box-mipped twin of the reflections cube for native envmaps.
	TextureCube<float3> SharpReflectionsTexture : register(t41);

	struct Surface
	{
		float3 positionView;
		float3 normalView;
	};

	struct Environment
	{
		float3 environment;
		float3 sky;
	};

	Environment SampleEnvironmentDirection(float3 direction, float roughness, SamplerState probeSampler,
		Skylighting::SpecularVisibility skylighting)
	{
		Environment result;
		result.environment = 0.0;
		result.sky = 0.0;
		float level = saturate(roughness) * 8.0;
		float3 debugColor;
		if (TryGetDebugEnvironment(direction, probeSampler, debugColor)) {
			result.environment = debugColor;
			return result;
		}
		float3 envSample = EnvTexture.SampleLevel(probeSampler, direction, level);
		float3 fullSample = EnvReflectionsTexture.SampleLevel(probeSampler, direction, level);
		float3 skySample = max(0.0, fullSample - envSample);
#ifdef IBL
		[branch] if (SharedData::iblSettings.EnableIBL)
		{
			if (SharedData::iblSettings.DALCMode >= 2) {
				float envLum = Color::RGBToLuminance(EnvTexture.SampleLevel(probeSampler, direction, 15));
				result.environment = ColorManagement::SceneToLinear(envSample / max(envLum, 0.001) * FO4SharedData::GetAmbientLinear(direction));
				result.environment *= SharedData::iblSettings.DALCAmount;
#	ifdef SKYLIGHTING
				if (SharedData::iblSettings.DALCMode == 3)
					result.environment *= skylighting.visibility;
#	endif
			} else {
				float3 ratio = ImageBasedLighting::GetIBLRatio();
				result.environment = ColorManagement::SceneToLinear(envSample * ratio) *
				                     SharedData::iblSettings.EnvIBLScale;
			}
			result.sky = ColorManagement::SceneToLinear(skySample) * SharedData::iblSettings.SkyIBLScale;
#	ifdef SKYLIGHTING
			result.sky *= skylighting.specular;
#	endif
		}
		else
#endif
		{
			result.environment = ColorManagement::SceneToLinear(envSample);
			result.sky = ColorManagement::SceneToLinear(skySample);
#ifdef SKYLIGHTING
			result.sky *= FO4_SPECULAR_LOBE(skylighting);
#endif
		}
		if (SharedData::InInterior) {
			result.sky = 0.0;
		}
		return result;
	}

	Environment SampleEnvironment(Surface surface, float roughness, SamplerState probeSampler)
	{
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(surface.normalView, false));
		float3 viewWS = normalize(FrameBuffer::ViewToWorld(-surface.positionView, false));
		float3 direction = reflect(-viewWS, normalWS);
		return SampleEnvironmentDirection(direction, roughness, probeSampler,
			FO4_SKYLIGHTING_SPECULAR_ROUGHNESS(surface.positionView, surface.normalView, roughness));
	}
}

#endif
