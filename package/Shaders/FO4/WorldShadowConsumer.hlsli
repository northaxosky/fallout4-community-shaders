// SPDX-License-Identifier: GPL-3.0-only
#ifndef FO4_WORLD_SHADOW_CONSUMER
#define FO4_WORLD_SHADOW_CONSUMER

#if defined(TERRAIN_SHADOWS) || defined(CLOUD_SHADOWS)
#	define FO4_WORLD_SHADOW

#	include "FO4/FO4ShaderData.hlsli"
#	include "FO4/ShadowSamplingConsumer.hlsli"

namespace WorldShadow
{
	// Upstream's GetWorldShadow: terrain and cloud shadows of the sun.
	float GetMultFromWorldPosition(float3 worldPosition)
	{
		// FO4: a disabled feature writes no settings, so the lookup skips its cost.
		bool active = false;
#	ifdef TERRAIN_SHADOWS
		active = SharedData::terraOccSettings.EnableTerrainShadow != 0;
#	endif
#	ifdef CLOUD_SHADOWS
		active = active || SharedData::cloudShadowsSettings.Opacity > 0.0;
#	endif
		[branch] if (!active) return 1.0;
		// FO4: b4 supplies the absolute origin for upstream heightfield sampling.
		return ShadowSampling::GetWorldShadow(worldPosition, FrameBuffer::CameraPosAdjust.xyz);
	}

	float GetMultFromViewPosition(float3 viewPosition)
	{
		return GetMultFromWorldPosition(FrameBuffer::ViewToWorld(viewPosition));
	}
}
#endif
#endif
