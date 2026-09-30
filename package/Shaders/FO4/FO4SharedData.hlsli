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
	struct WaterEffectsSettings
	{
		uint Mode;
		uint HasWater;
		float WaterHeight;
		float pad0;
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
		InverseSquareLightingSettings inverseSquareLightingSettings;
		WaterEffectsSettings waterEffectsSettings;
		ExponentialHeightFogSettings exponentialHeightFogSettings;
		uint WetnessDebugVisualization;
		uint TerrainShadowMode;
		uint DynamicCubemapsDebugVisualization;
		uint EnabledSSR;
		float2 HeightRange;
		float2 DebugHeightRange;
		float DeltaTime;
		float3 pad0;
	};
}
#endif
