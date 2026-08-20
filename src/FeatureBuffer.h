#pragma once

#include <cstdint>

namespace cs
{
	// Mirrors the HLSL cbuffer FeatureData : register(b6); every field exists whether or not its feature loaded.
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
		std::uint32_t EnableAmbientPass = 0;
		float         AoPower = 0.0f;
		float         BounceStrength = 0.0f;
	};
	static_assert(sizeof(ScreenSpaceGIFeatureData) == 16);

	struct alignas(16) WetnessEffectsFeatureData
	{
		std::uint32_t EnableWetnessEffects = 0;
		std::uint32_t EnableAmbientPass = 0;
		float         Wetness = 0.0f;
		float         pad0 = 0.0f;
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

	// Unloaded, unhealthy or unready contributors leave their block zeroed.
	FeatureDataCB GetFeatureBufferData();
}
