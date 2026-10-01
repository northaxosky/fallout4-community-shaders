#pragma once

#include <DirectXMath.h>
#include <cstdint>

namespace RE
{
	class TESWeather;
}

namespace cs::engine
{
	struct WeatherSnapshot
	{
		const RE::TESWeather* current = nullptr;
		const RE::TESWeather* previous = nullptr;
		float transitionPct = 1.0f;
		bool currentIsRain = false;
		bool previousIsRain = false;
		std::uint8_t currentBeginPrecip = 0;
		std::uint8_t previousEndPrecip = 0;
		bool available = false;
		bool fullSky = false;
		float currentRainDensity = 0.0f;
		float previousRainDensity = 0.0f;
		DirectX::XMFLOAT4X4 occlusionViewProj{};
	};

	WeatherSnapshot SnapshotWeather() noexcept;
}
