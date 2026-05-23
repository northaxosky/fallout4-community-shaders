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

	// Overlayable subset of Imagespace::Settings. Each std::optional present == "override this key for
	// this category"; absent == "fall through to base settings". Sunsprite enable is NOT overlayable
	// because the engine sunbeams vfunc hook reads settings.sunspriteEnable directly.
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
		std::optional<float>                sunspriteIntensity;
		std::optional<float>                sunspriteSize;
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

	// Frame-resolved view consumed by Imagespace::RunFrame. POD-only; no std::string. lutSRV is a raw
	// pointer owned by Imagespace::lutCache (or by Imagespace::lutSRV for the base-LUT fallback).
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
		float                       sunspriteIntensity;
		float                       sunspriteSize;
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

	// Subset of Imagespace::Settings forwarded into Resolve. Keeps WeatherProfiles independent of
	// Imagespace.h so the resolver is unit-testable.
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
		float                sunspriteIntensity;
		float                sunspriteSize;
		bool                 lensFlareEnable;
		float                lensFlareIntensity;
		int                  lensFlareGhosts;
		bool                 dirtEnable;
		float                dirtIntensity;
	};

	// Frame snapshot of Sky state, read on the render thread. Pointer reads are atomic on x64; floats
	// may be torn but the consumer clamps the pct to [0,1] anyway. mode==kFull is required for the
	// per-weather pipeline; kInterior/kSkyDomeOnly fall back to base.
	struct SkySample
	{
		const RE::TESWeather* current  = nullptr;
		const RE::TESWeather* previous = nullptr;
		float                 transitionPct = 1.0f;
		bool                  modeIsFull = false;
	};

	[[nodiscard]] SkySample SampleSky() noexcept;

	// Returns the canonical category for a TESWeather pointer. Applies user overrides first
	// (formID -> category), then the static vanilla-formID table, then the kFlags fallback.
	[[nodiscard]] WeatherCategory Classify(const RE::TESWeather* a_weather,
		const std::unordered_map<std::uint32_t, WeatherCategory>& a_userOverrides) noexcept;

	// Returns the list of distinct LUT paths referenced by base + any overlay. Used by LUTCache::Preload.
	[[nodiscard]] std::vector<std::string> CollectReferencedLUTs(const std::string& a_baseLutPath,
		const WeatherProfiles& a_profiles);

	class LUTCache;

	// Resolves per-frame Imagespace settings under the active weather. On the render thread; uses only
	// LUTCache::TryGet (no synchronous loads). Falls back to base settings when:
	//   profiles.enablePerWeatherProfiles == false, OR sample.modeIsFull == false,
	//   OR sample.current == nullptr.
	[[nodiscard]] ResolvedRuntime Resolve(const ResolveBase& a_base,
		ID3D11ShaderResourceView*  a_baseLutSRV,
		const WeatherProfiles&     a_profiles,
		const SkySample&           a_sample,
		const LUTCache&            a_lutCache) noexcept;

	// Smoke-harness path: bypass Sky entirely, apply overlay[a_category] at pct=1.0. Useful for
	// CI screenshots where we want to validate the overlay/blend/apply pipeline without a live
	// weather change. Render-thread safe (TryGet only).
	[[nodiscard]] ResolvedRuntime ResolveForced(const ResolveBase& a_base,
		ID3D11ShaderResourceView*  a_baseLutSRV,
		const WeatherProfiles&     a_profiles,
		WeatherCategory            a_category,
		const LUTCache&            a_lutCache) noexcept;
} // namespace cs::features::imagespace
