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

	// Optional values override the base setting for this weather category.
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

	// Frame-resolved POD; lutSRV is owned by LUTCache or the base LUT fallback.
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

		// Diagnostics (for ImGui status block). Not consumed by the CB packers.
		WeatherCategory             currentCategory{ WeatherCategory::kUnknown };
		WeatherCategory             previousCategory{ WeatherCategory::kUnknown };
		float                       transitionPct{ 1.0f };
		bool                        weatherProfilesActive{ false };
		bool                        lutCacheMiss{ false };
	};

	// Overlayable Imagespace settings; keeps the resolver independent of Imagespace.h.
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

	// Render-thread Sky snapshot; torn pct reads are clamped, and only kFull enables per-weather.
	struct SkySample
	{
		const RE::TESWeather* current  = nullptr;
		const RE::TESWeather* previous = nullptr;
		float                 transitionPct = 1.0f;
		bool                  modeIsFull = false;
	};

	[[nodiscard]] SkySample SampleSky() noexcept;

	// Classifies weather via user overrides, then vanilla formIDs, then kFlags fallback.
	[[nodiscard]] WeatherCategory Classify(const RE::TESWeather* a_weather,
		const std::unordered_map<std::uint32_t, WeatherCategory>& a_userOverrides) noexcept;

	// Distinct LUT paths referenced by base settings and overlays.
	[[nodiscard]] std::vector<std::string> CollectReferencedLUTs(const std::string& a_baseLutPath,
		const WeatherProfiles& a_profiles);

	class LUTCache;

	// Render-thread resolve; TryGet only, falling back to base when profiles/Sky are inactive.
	[[nodiscard]] ResolvedRuntime Resolve(const ResolveBase& a_base,
		ID3D11ShaderResourceView*  a_baseLutSRV,
		const WeatherProfiles&     a_profiles,
		const SkySample&           a_sample,
		const LUTCache&            a_lutCache) noexcept;

	// Smoke-harness resolve: bypass Sky and apply one category at pct=1.0; TryGet only.
	[[nodiscard]] ResolvedRuntime ResolveForced(const ResolveBase& a_base,
		ID3D11ShaderResourceView*  a_baseLutSRV,
		const WeatherProfiles&     a_profiles,
		WeatherCategory            a_category,
		const LUTCache&            a_lutCache) noexcept;
} // namespace cs::features::imagespace
