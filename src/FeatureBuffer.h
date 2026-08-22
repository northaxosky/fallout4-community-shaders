#pragma once

#include <cstddef>
#include <cstdint>

namespace cs
{
	// mirrors HLSL FeatureData at b6; blocks exist even when unloaded
	struct alignas(16) ScreenSpaceShadowsFeatureData
	{
		std::uint32_t EnableScreenSpaceShadows = 0;
		float         ShadowContrast = 0.0f;
		std::uint32_t pad0[2]{};
	};
	static_assert(sizeof(ScreenSpaceShadowsFeatureData) == 16);

	struct alignas(16) ScreenSpaceGIFeatureData
	{
		std::uint32_t EnableScreenSpaceGI = 0;
		std::uint32_t pad0 = 0;
		float         AoPower = 0.0f;
		float         BounceStrength = 0.0f;
	};
	static_assert(sizeof(ScreenSpaceGIFeatureData) == 16);

	struct alignas(16) WetnessEffectsFeatureData
	{
		float Wetness = 0.0f;
		float MaxRainWetness = 0.0f;
		float MinRainWetness = 0.0f;
		float pad0 = 0.0f;
	};
	static_assert(sizeof(WetnessEffectsFeatureData) == 16);

	struct alignas(16) FeatureDataCB
	{
		ScreenSpaceShadowsFeatureData screenSpaceShadowsSettings;
		ScreenSpaceGIFeatureData      screenSpaceGISettings;
		WetnessEffectsFeatureData     wetnessEffectsSettings;
	};
	static_assert(sizeof(FeatureDataCB) == 48);
	static_assert(sizeof(FeatureDataCB) % 16 == 0);
	static_assert(offsetof(FeatureDataCB, wetnessEffectsSettings) == 32);
	static_assert(offsetof(WetnessEffectsFeatureData, Wetness) == 0);
	static_assert(offsetof(WetnessEffectsFeatureData, MaxRainWetness) == 4);
	static_assert(offsetof(WetnessEffectsFeatureData, MinRainWetness) == 8);

	// inactive contributors leave zeroed blocks
	FeatureDataCB GetFeatureBufferData();
}
