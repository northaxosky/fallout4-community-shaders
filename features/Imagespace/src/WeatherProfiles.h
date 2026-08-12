#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <d3d11.h>

namespace RE { class TESWeather; }

namespace cs::features::imagespace
{
	enum class WeatherCategory : std::uint8_t
	{
		kClear     = 0,
		kOvercast  = 1,
		kFog       = 2,
		kRain      = 3,
		kRadstorm  = 4,
		kSnow      = 5,
		kInterior  = 6,
		kUnknown   = 7,
		kCount     = 8,
	};

	[[nodiscard]] std::string_view CategoryName(WeatherCategory a_cat) noexcept;
	[[nodiscard]] std::optional<WeatherCategory> ParseCategory(std::string_view a_name) noexcept;

	struct WeatherOverlay
	{
		std::optional<float>                exposure;
		std::optional<bool>                 lutEnable;
		std::optional<std::string>          lutPath;
		std::optional<float>                lutStrength;
		std::optional<bool>                 bloomEnable;
		std::optional<float>                bloomThreshold;
		std::optional<float>                bloomIntensity;
		std::optional<std::array<float, 6>> bloomMipWeights;
		std::optional<bool>                 vignetteEnable;
		std::optional<float>                vignetteIntensity;
		std::optional<bool>                 caEnable;
		std::optional<float>                caIntensity;
		std::optional<bool>                 lensFlareEnable;
		std::optional<float>                lensFlareIntensity;
		std::optional<int>                  lensFlareGhosts;
		std::optional<bool>                 dirtEnable;
		std::optional<float>                dirtIntensity;

		[[nodiscard]] std::size_t SetKeyCount() const noexcept;
		void Clear() noexcept { *this = WeatherOverlay{}; }
	};

	struct WeatherProfiles
	{
		bool                                                                       enablePerWeatherProfiles{ false };
		std::array<WeatherOverlay, static_cast<std::size_t>(WeatherCategory::kCount)> overlays{};
		std::unordered_map<std::uint32_t, WeatherCategory>                          userOverrides{};
	};

	// LUTCache or the base fallback owns lutSRV.
	struct ResolvedRuntime
	{
		float                       exposure;
		bool                        lutEnable;
		ID3D11ShaderResourceView*   lutSRV;
		float                       lutStrength;
		bool                        bloomEnable;
		float                       bloomThreshold;
		float                       bloomIntensity;
		std::array<float, 6>        bloomMipWeights;
		bool                        vignetteEnable;
		float                       vignetteIntensity;
		bool                        caEnable;
		float                       caIntensity;
		bool                        lensFlareEnable;
		float                       lensFlareIntensity;
		int                         lensFlareGhosts;
		bool                        dirtEnable;
		float                       dirtIntensity;

		WeatherCategory             currentCategory{ WeatherCategory::kUnknown };
		WeatherCategory             previousCategory{ WeatherCategory::kUnknown };
		float                       transitionPct{ 1.0f };
		bool                        weatherProfilesActive{ false };
		bool                        lutCacheMiss{ false };
	};

	struct ResolveBase
	{
		float                exposure;
		bool                 lutEnable;
		std::string          lutPath;
		float                lutStrength;
		bool                 bloomEnable;
		float                bloomThreshold;
		float                bloomIntensity;
		std::array<float, 6> bloomMipWeights;
		bool                 vignetteEnable;
		float                vignetteIntensity;
		bool                 caEnable;
		float                caIntensity;
		bool                 lensFlareEnable;
		float                lensFlareIntensity;
		int                  lensFlareGhosts;
		bool                 dirtEnable;
		float                dirtIntensity;
	};

	// Clamp torn percentages; only kFull enables profiles.
	struct SkySample
	{
		const RE::TESWeather* current  = nullptr;
		const RE::TESWeather* previous = nullptr;
		float                 transitionPct = 1.0f;
		bool                  modeIsFull = false;
	};

	[[nodiscard]] SkySample SampleSky() noexcept;

	[[nodiscard]] WeatherCategory Classify(const RE::TESWeather* a_weather,
		const std::unordered_map<std::uint32_t, WeatherCategory>& a_userOverrides) noexcept;

	[[nodiscard]] std::vector<std::string> CollectReferencedLUTs(const std::string& a_baseLutPath,
		const WeatherProfiles& a_profiles);

	class LUTCache;

	// Cache misses and inactive profiles use base settings.
	[[nodiscard]] ResolvedRuntime Resolve(const ResolveBase& a_base,
		ID3D11ShaderResourceView*  a_baseLutSRV,
		const WeatherProfiles&     a_profiles,
		const SkySample&           a_sample,
		const LUTCache&            a_lutCache) noexcept;

	// Smoke tests apply one full-strength category.
	[[nodiscard]] ResolvedRuntime ResolveForced(const ResolveBase& a_base,
		ID3D11ShaderResourceView*  a_baseLutSRV,
		const WeatherProfiles&     a_profiles,
		WeatherCategory            a_category,
		const LUTCache&            a_lutCache) noexcept;
}
