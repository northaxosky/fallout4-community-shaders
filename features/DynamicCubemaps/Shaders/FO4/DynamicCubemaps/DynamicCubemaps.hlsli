#ifndef FO4_DYNAMIC_CUBEMAPS_CONSUMER_HLSLI
#define FO4_DYNAMIC_CUBEMAPS_CONSUMER_HLSLI

#include "Common/Color.hlsli"
#include "FO4/FO4ShaderData.hlsli"
// FO4 consumers supply native samplers rather than Skyrim's global color sampler.
#define DYNAMIC_CUBEMAPS_CUSTOM_CONSUMER
#include "DynamicCubemaps/DynamicCubemaps.hlsli"
#undef DYNAMIC_CUBEMAPS_CUSTOM_CONSUMER

namespace DynamicCubemaps
{
	// Debug views replace the environment with a known world-space reference.
	bool TryGetDebugEnvironment(float3 direction, SamplerState cubeSampler, out float3 color)
	{
		color = 0.0;
		if (FO4SharedData::DebugOwner != FullscreenDebugOwner::DynamicCubemaps)
			return false;
		if (FO4SharedData::DebugMode == DynamicCubemapsDebugMode::DirectionCube) {
			float3 d = normalize(direction);
			float3 weights = d * d;
			// +X red, -X cyan, +Y green, -Y magenta, +Z white, -Z black.
			color = weights.x * (d.x >= 0.0 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 1.0)) +
			        weights.y * (d.y >= 0.0 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 1.0)) +
			        weights.z * (d.z >= 0.0 ? float3(1.0, 1.0, 1.0) : float3(0.0, 0.0, 0.0));
			return true;
		}
		if (FO4SharedData::DebugMode == DynamicCubemapsDebugMode::EngineCube) {
			// The host binds the engine cube in the reflections slot for this view.
			color = EnvReflectionsTexture.SampleLevel(cubeSampler, direction, 0);
			return true;
		}
		return false;
	}
}

#endif
