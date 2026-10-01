#ifndef FO4_SHARED_DATA_HLSLI
#define FO4_SHARED_DATA_HLSLI

namespace FO4SharedData
{
	cbuffer FO4SharedData : register(b7)
	{
		uint reserved0;
		uint padTerrain;
		uint DynamicCubemapsDebugVisualization;
		uint EnabledSSR;
		float DeltaTime;
		float3 pad0;
	};
}
#endif
