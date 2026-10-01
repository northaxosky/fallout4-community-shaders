#include "World/Weather.h"

#include "RE/B/BGSShaderParticleGeometryData.h"
#include "RE/P/Precipitation.h"
#include "RE/S/Sky.h"
#include "RE/T/TESWeather.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cs::engine
{
	namespace
	{
		// FO4 stores precipitation fades and flags in signed weatherData bytes; decode them as unsigned.
		std::uint8_t WeatherByte(const RE::TESWeather* a_weather, RE::TESWeather::WeatherData a_field) noexcept
		{
			return a_weather ? static_cast<std::uint8_t>(
								   a_weather->weatherData[static_cast<std::size_t>(a_field)]) :
			                   0;
		}

		bool IsRain(const RE::TESWeather* a_weather) noexcept
		{
			if (!a_weather || !a_weather->precipitationData) {
				return false;
			}

			const auto flags = WeatherByte(a_weather, RE::TESWeather::WeatherData::kFlags);
			return (flags & static_cast<std::uint8_t>(RE::TESWeather::WeatherDataFlags::kRainy)) != 0;
		}

		float RainDensity(const RE::TESWeather* a_weather, bool a_hasGeometry) noexcept
		{
			const auto* spgd = a_weather ? a_weather->precipitationData : nullptr;
			// FO4: SPGD type zero constructs rain; type one constructs snow (engine-facts Precipitation).
			if (!a_hasGeometry || !IsRain(a_weather) || !spgd || spgd->data.size() <= 11 || spgd->data[9].i != 0)
				return 0.0f;
			return spgd->data[11].f > 0.0f ? std::min(1.0f, spgd->data[11].f / 3.0f) : 0.0f;
		}
	}

	WeatherSnapshot SnapshotWeather() noexcept
	{
		WeatherSnapshot s;
		auto* sky = RE::Sky::GetSingleton();
		if (!sky)
			return s;
		s.current = sky->currentWeather;
		s.previous = sky->lastWeather;
		s.transitionPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		s.currentIsRain = IsRain(s.current);
		s.previousIsRain = IsRain(s.previous);
		s.currentBeginPrecip = WeatherByte(s.current, RE::TESWeather::WeatherData::kBeginPrecip);
		s.previousEndPrecip = WeatherByte(s.previous, RE::TESWeather::WeatherData::kEndPrecip);
		s.available = true;
		s.fullSky = sky->mode.get() == RE::Sky::Mode::kFull;
		s.currentRainDensity = RainDensity(s.current, sky->precip && sky->precip->precipParticleGeometry);
		s.previousRainDensity = RainDensity(s.previous, sky->precip && sky->precip->prevPrecipParticleGeometry);
		if (sky->precip && (sky->precip->precipParticleGeometry || sky->precip->prevPrecipParticleGeometry)) {
			// FO4: native precipitation rows invert Y and do not divide by W.
			constexpr std::uintptr_t offsets[]{ 0x6732B30, 0x3CB5C30, 0x3E71540 };
			const REL::Relocation<const DirectX::XMFLOAT4X4*> matrix{
				REL::Offset(offsets[static_cast<std::size_t>(REX::FModule::GetRuntimeIndex())])
			};
			std::memcpy(&s.occlusionViewProj, matrix.get(), sizeof(s.occlusionViewProj));
			for (auto& value : s.occlusionViewProj.m[1])
				value = -value;
		}
		return s;
	}
}
