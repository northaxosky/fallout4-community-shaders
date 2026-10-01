#ifndef FO4_SHARED_DATA_HLSLI
#define FO4_SHARED_DATA_HLSLI

namespace FO4SharedData
{
	struct ScreenSpaceGISettings
	{
		bool EnableScreenSpaceGI;
		uint3 pad0;
	};
	struct ExponentialHeightFogSettings
	{
		uint Mode;
		float DensityMultiplier;
		float HeightFalloffMultiplier;
		float pad0;
	};
	cbuffer FO4SharedData : register(b7)
	{
		ScreenSpaceGISettings screenSpaceGISettings;
		ExponentialHeightFogSettings exponentialHeightFogSettings;
		uint WetnessDebugVisualization;
		uint padTerrain;
		uint DynamicCubemapsDebugVisualization;
		uint EnabledSSR;
		float DeltaTime;
		float3 pad0;
	};
}
#endif
