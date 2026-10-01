#ifndef FO4_SHARED_DATA_HLSLI
#define FO4_SHARED_DATA_HLSLI

#include "FO4/DebugViewOwners.h"

namespace FO4SharedData
{
	cbuffer FO4SharedData : register(b7)
	{
		uint DebugOwner;
		uint DebugMode;
		uint EnabledSSR;
		float DeltaTime;
		float4 DebugParams;
		uint EnabledDynamicCubemaps;
		uint3 pad0;
	};
#if defined(FO4CS_SUBSTRATE) && (defined(TERRAIN_SHADOWS_FULLSCREEN_DEBUG) || defined(WATER_EFFECTS_FULLSCREEN_DEBUG))
	Texture2D<float4> DebugTexture : register(t61);
#endif
}
#endif
