#ifndef FO4_SHARED_DATA_HLSLI
#define FO4_SHARED_DATA_HLSLI

namespace FO4SharedData
{
	struct ScreenSpaceShadowsSettings
	{
		bool EnableScreenSpaceShadows;
		float ShadowContrast;
		uint2 pad0;
	};
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
	struct ExponentialHeightFogSettings
	{
		uint Mode;
		float DensityMultiplier;
		float HeightFalloffMultiplier;
		float pad0;
	};
	cbuffer FO4SharedData : register(b7)
	{
		ScreenSpaceShadowsSettings screenSpaceShadowsSettings;
		ScreenSpaceGISettings screenSpaceGISettings;
		InverseSquareLightingSettings inverseSquareLightingSettings;
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
