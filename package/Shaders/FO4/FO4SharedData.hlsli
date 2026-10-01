#ifndef FO4_SHARED_DATA_HLSLI
#define FO4_SHARED_DATA_HLSLI

namespace FO4SharedData
{
	struct ScreenSpaceGISettings
	{
		bool EnableScreenSpaceGI;
		uint3 pad0;
	};
	struct InverseSquareLightingSettings
	{
		uint Mode;
		float ExteriorStrength;
		float InteriorStrength;
		float NearFieldDistance;
	};
	cbuffer FO4SharedData : register(b7)
	{
		ScreenSpaceGISettings screenSpaceGISettings;
		InverseSquareLightingSettings inverseSquareLightingSettings;
		uint WetnessDebugVisualization;
		uint padTerrain;
		uint DynamicCubemapsDebugVisualization;
		uint EnabledSSR;
		float DeltaTime;
		float3 pad0;
	};
}
#endif
