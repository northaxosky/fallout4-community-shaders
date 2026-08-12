#include "WeatherProfiles.h"

#include <algorithm>
#include <vector>

#include "RE/S/Sky.h"
#include "RE/T/TESWeather.h"

namespace cs::features::imagespace
{
	namespace
	{
		struct VanillaEntry
		{
			std::uint32_t   formID;
			WeatherCategory category;
		};

		// Unknown categories fall back to weather flags.
		constexpr VanillaEntry kVanillaWeathers[] = {
			{ 0x0000116Bu, WeatherCategory::kInterior  },  // FXDiamondSunlightBounce
			{ 0x0000116Du, WeatherCategory::kInterior  },  // DiamondWeather
			{ 0x0000116Eu, WeatherCategory::kInterior  },  // DiamondWeatherPastel
			{ 0x000F1033u, WeatherCategory::kOvercast  },  // CommonwealthGSOvercast
			{ 0x000FF98Fu, WeatherCategory::kInterior  },  // PrewarPlayerHouseInteriorWeather
			{ 0x0010D573u, WeatherCategory::kInterior  },  // FXInstituteDayNightCycleKey
			{ 0x0010F781u, WeatherCategory::kOvercast  },  // TCommonwealthMarshOvercast
			{ 0x00108640u, WeatherCategory::kInterior  },  // FXInstituteDayNightCycle
			{ 0x0010E3D4u, WeatherCategory::kInterior  },  // EditorCloudPreview
			{ 0x00115C64u, WeatherCategory::kInterior  },  // GoodneighborWeatherBase
			{ 0x001209AFu, WeatherCategory::kOvercast  },  // NeutralOvercast
			{ 0x001256FBu, WeatherCategory::kRadstorm  },  // FXNukeWeather
			{ 0x0012A18Eu, WeatherCategory::kClear     },  // CommonwealthSanctuaryClear
			{ 0x00171621u, WeatherCategory::kInterior  },  // DefaultInteriorWeatherNoLUT
			{ 0x001A65E5u, WeatherCategory::kInterior  },  // ConcMuseumWeather
			{ 0x001A65F0u, WeatherCategory::kInterior  },  // DefaultInteriorWeather
			{ 0x001A6994u, WeatherCategory::kRadstorm  },  // CommonwealthSanctuaryClearNukeFog
			{ 0x001BD481u, WeatherCategory::kFog       },  // CommonwealthGSFoggy
			{ 0x001C3473u, WeatherCategory::kFog       },  // CommonwealthFoggy
			{ 0x001C3D5Eu, WeatherCategory::kRadstorm  },  // CommonwealthGSRadstorm
			{ 0x001C8556u, WeatherCategory::kOvercast  },  // CommonwealthOvercast
			{ 0x001CA7E4u, WeatherCategory::kRain      },  // CommonwealthRain
			{ 0x001CC186u, WeatherCategory::kFog       },  // CommonwealthMisty
			{ 0x001CD096u, WeatherCategory::kRain      },  // CommonwealthMistyRainy
			{ 0x001D670Eu, WeatherCategory::kClear     },  // CommonwealthClearestSkies
			{ 0x001E5E60u, WeatherCategory::kOvercast  },  // CommonwealthDarkSkies
			{ 0x001EB2FFu, WeatherCategory::kOvercast  },  // CommonwealthPolluted
			{ 0x001F2529u, WeatherCategory::kOvercast  },  // CommOvercastTest2
			{ 0x001F61A1u, WeatherCategory::kOvercast  },  // CommonwealthDusty
			{ 0x001F61FDu, WeatherCategory::kRadstorm  },  // CGPrewarNukeFXWeather
			{ 0x0020F46Cu, WeatherCategory::kOvercast  },  // CommonwealthOvercastBackup
			{ 0x00211221u, WeatherCategory::kInterior  },  // VideoVaultExit
			{ 0x002115D7u, WeatherCategory::kRain      },  // CommonwealthMistyRainyBackup
			{ 0x00216A98u, WeatherCategory::kClear     },  // CommonwealthClearBackup
			{ 0x0021A563u, WeatherCategory::kClear     },  // CommonwealthClearTrailer1
			{ 0x0021A564u, WeatherCategory::kClear     },  // CommonwealthClearTrailer2
			{ 0x00222394u, WeatherCategory::kRadstorm  },  // CommonwealthGSRadstormOld
			{ 0x0022239Au, WeatherCategory::kRain      },  // CommonwealthRainBackup
			{ 0x00225922u, WeatherCategory::kClear     },  // CommonwealthSanctuaryClearNoAttach
			{ 0x00226448u, WeatherCategory::kOvercast  },  // CommonwealthDarkSkies3
			{ 0x002385FBu, WeatherCategory::kOvercast  },  // CommonwealthDarkSkies2
			{ 0x002385FDu, WeatherCategory::kClear     },  // CommonwealthClear2
			{ 0x002392A2u, WeatherCategory::kFog       },  // CommonwealthFoggyBackup
			{ 0x002392A3u, WeatherCategory::kRadstorm  },  // CommonwealthGSRadstormBackup
			{ 0x002392A4u, WeatherCategory::kFog       },  // CommonwealthGSFoggyBackup
			{ 0x002392A5u, WeatherCategory::kFog       },  // CommonwealthMistyBackup
			{ 0x002392A6u, WeatherCategory::kClear     },  // CommonwealthSanctuaryClearBackup
			{ 0x0023AB9Cu, WeatherCategory::kClear     },  // CommonwealthClearBackup2
			{ 0x002486A4u, WeatherCategory::kFog       },  // CommonwealthClear_VBFog
			{ 0x002486A5u, WeatherCategory::kFog       },  // CommonwealthOvercast_VBFog
			{ 0x0024A3C0u, WeatherCategory::kRadstorm  },  // VRWorkshopShared_CommonwealthGSRadstormNoHazard
			{ 0x0024A3C1u, WeatherCategory::kOvercast  },  // VRWorkshopShared_CommonwealthGSOvercastNoHazard
			{ 0x0002B52Au, WeatherCategory::kClear     },  // CommonwealthClear
		};
		constexpr std::size_t kVanillaCount = sizeof(kVanillaWeathers) / sizeof(kVanillaWeathers[0]);

		// Sort at runtime; keep source order reviewable.
		const std::array<VanillaEntry, kVanillaCount>& GetSortedTable() noexcept
		{
			static const std::array<VanillaEntry, kVanillaCount> kSorted = [] {
				std::array<VanillaEntry, kVanillaCount> arr{};
				for (std::size_t i = 0; i < kVanillaCount; ++i) arr[i] = kVanillaWeathers[i];
				std::sort(arr.begin(), arr.end(), [](const VanillaEntry& a, const VanillaEntry& b) {
					return a.formID < b.formID;
				});
				return arr;
			}();
			return kSorted;
		}
	}

	std::string_view CategoryName(WeatherCategory a_cat) noexcept
	{
		switch (a_cat) {
		case WeatherCategory::kClear:    return "clear";
		case WeatherCategory::kOvercast: return "overcast";
		case WeatherCategory::kFog:      return "fog";
		case WeatherCategory::kRain:     return "rain";
		case WeatherCategory::kRadstorm: return "radstorm";
		case WeatherCategory::kSnow:     return "snow";
		case WeatherCategory::kInterior: return "interior";
		case WeatherCategory::kUnknown:  return "unknown";
		default:                         return "unknown";
		}
	}

	std::optional<WeatherCategory> ParseCategory(std::string_view a_name) noexcept
	{
		if (a_name == "clear")    return WeatherCategory::kClear;
		if (a_name == "overcast") return WeatherCategory::kOvercast;
		if (a_name == "fog")      return WeatherCategory::kFog;
		if (a_name == "rain")     return WeatherCategory::kRain;
		if (a_name == "radstorm") return WeatherCategory::kRadstorm;
		if (a_name == "snow")     return WeatherCategory::kSnow;
		if (a_name == "interior") return WeatherCategory::kInterior;
		if (a_name == "unknown")  return WeatherCategory::kUnknown;
		// Accept export-vocabulary aliases.
		if (a_name == "interior_or_location") return WeatherCategory::kInterior;
		if (a_name == "cloudy")  return WeatherCategory::kOvercast;
		if (a_name == "foggy")   return WeatherCategory::kFog;
		return std::nullopt;
	}

	std::size_t WeatherOverlay::SetKeyCount() const noexcept
	{
		std::size_t n = 0;
		n += exposure.has_value();
		n += lutEnable.has_value();
		n += lutPath.has_value();
		n += lutStrength.has_value();
		n += bloomEnable.has_value();
		n += bloomThreshold.has_value();
		n += bloomIntensity.has_value();
		n += bloomMipWeights.has_value();
		n += vignetteEnable.has_value();
		n += vignetteIntensity.has_value();
		n += caEnable.has_value();
		n += caIntensity.has_value();
		n += lensFlareEnable.has_value();
		n += lensFlareIntensity.has_value();
		n += lensFlareGhosts.has_value();
		n += dirtEnable.has_value();
		n += dirtIntensity.has_value();
		return n;
	}

	SkySample SampleSky() noexcept
	{
		SkySample s;
		auto* sky = RE::Sky::GetSingleton();
		if (!sky) return s;
		s.current       = sky->currentWeather;
		s.previous      = sky->lastWeather;
		// Clamp torn reads to [0,1].
		s.transitionPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		s.modeIsFull    = sky->mode.any(RE::Sky::Mode::kFull);
		return s;
	}

	WeatherCategory Classify(const RE::TESWeather* a_weather,
		const std::unordered_map<std::uint32_t, WeatherCategory>& a_userOverrides) noexcept
	{
		if (!a_weather) return WeatherCategory::kUnknown;

		// User overrides win.
		const std::uint32_t formID = a_weather->GetFormID();
		if (auto it = a_userOverrides.find(formID); it != a_userOverrides.end()) {
			return it->second;
		}

		const auto& sorted = GetSortedTable();
		auto it = std::lower_bound(sorted.begin(), sorted.end(), formID,
			[](const VanillaEntry& e, std::uint32_t key) { return e.formID < key; });
		if (it != sorted.end() && it->formID == formID) {
			return it->category;
		}

		// CommonLib exposes signed flags; promote before testing.
		const auto flags = static_cast<std::uint8_t>(
			a_weather->weatherData[static_cast<std::size_t>(RE::TESWeather::WeatherData::kFlags)]);
		using F = RE::TESWeather::WeatherDataFlags;
		if (flags & static_cast<std::uint8_t>(F::kSnow))   return WeatherCategory::kSnow;
		if (flags & static_cast<std::uint8_t>(F::kRainy))  return WeatherCategory::kRain;
		if (flags & static_cast<std::uint8_t>(F::kCloudy)) return WeatherCategory::kOvercast;
		// Rain occlusion alone does not imply fog.
		return WeatherCategory::kClear;
	}

	std::vector<std::string> CollectReferencedLUTs(const std::string& a_baseLutPath,
		const WeatherProfiles& a_profiles)
	{
		std::vector<std::string> out;
		auto pushUnique = [&out](const std::string& p) {
			if (p.empty()) return;
			if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
		};
		pushUnique(a_baseLutPath);
		for (const auto& overlay : a_profiles.overlays) {
			if (overlay.lutPath.has_value()) pushUnique(*overlay.lutPath);
		}
		return out;
	}
}

#include "LUTCache.h"

namespace cs::features::imagespace
{
	namespace
	{
		struct Endpoint
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

		Endpoint BuildEndpoint(const ResolveBase& a_base, const WeatherOverlay& a_overlay)
		{
			Endpoint ep{
				a_base.exposure,
				a_base.lutEnable,
				a_base.lutPath,
				a_base.lutStrength,
				a_base.bloomEnable,
				a_base.bloomThreshold,
				a_base.bloomIntensity,
				a_base.bloomMipWeights,
				a_base.vignetteEnable,
				a_base.vignetteIntensity,
				a_base.caEnable,
				a_base.caIntensity,
				a_base.lensFlareEnable,
				a_base.lensFlareIntensity,
				a_base.lensFlareGhosts,
				a_base.dirtEnable,
				a_base.dirtIntensity,
			};
			if (a_overlay.exposure)           ep.exposure           = *a_overlay.exposure;
			if (a_overlay.lutEnable)          ep.lutEnable          = *a_overlay.lutEnable;
			if (a_overlay.lutPath)            ep.lutPath            = *a_overlay.lutPath;
			if (a_overlay.lutStrength)        ep.lutStrength        = *a_overlay.lutStrength;
			if (a_overlay.bloomEnable)        ep.bloomEnable        = *a_overlay.bloomEnable;
			if (a_overlay.bloomThreshold)     ep.bloomThreshold     = *a_overlay.bloomThreshold;
			if (a_overlay.bloomIntensity)     ep.bloomIntensity     = *a_overlay.bloomIntensity;
			if (a_overlay.bloomMipWeights)    ep.bloomMipWeights    = *a_overlay.bloomMipWeights;
			if (a_overlay.vignetteEnable)     ep.vignetteEnable     = *a_overlay.vignetteEnable;
			if (a_overlay.vignetteIntensity)  ep.vignetteIntensity  = *a_overlay.vignetteIntensity;
			if (a_overlay.caEnable)           ep.caEnable           = *a_overlay.caEnable;
			if (a_overlay.caIntensity)        ep.caIntensity        = *a_overlay.caIntensity;
			if (a_overlay.lensFlareEnable)    ep.lensFlareEnable    = *a_overlay.lensFlareEnable;
			if (a_overlay.lensFlareIntensity) ep.lensFlareIntensity = *a_overlay.lensFlareIntensity;
			if (a_overlay.lensFlareGhosts)    ep.lensFlareGhosts    = *a_overlay.lensFlareGhosts;
			if (a_overlay.dirtEnable)         ep.dirtEnable         = *a_overlay.dirtEnable;
			if (a_overlay.dirtIntensity)      ep.dirtIntensity      = *a_overlay.dirtIntensity;
			return ep;
		}

		inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
		template <typename T>
		inline T Snap(T a, T b, float t) noexcept { return (t >= 0.5f) ? b : a; }

		ResolvedRuntime BlendEndpoints(const Endpoint& a, const Endpoint& b, float t,
			ID3D11ShaderResourceView* a_baseLutSRV, const ResolveBase& a_base, const LUTCache& a_cache,
			bool& outCacheMiss) noexcept
		{
			ResolvedRuntime r{};
			r.exposure           = Lerp(a.exposure, b.exposure, t);
			r.lutEnable          = Snap(a.lutEnable, b.lutEnable, t);
			r.lutStrength        = Lerp(a.lutStrength, b.lutStrength, t);
			r.bloomEnable        = Snap(a.bloomEnable, b.bloomEnable, t);
			r.bloomThreshold     = Lerp(a.bloomThreshold, b.bloomThreshold, t);
			r.bloomIntensity     = Lerp(a.bloomIntensity, b.bloomIntensity, t);
			for (std::size_t i = 0; i < r.bloomMipWeights.size(); ++i) {
				r.bloomMipWeights[i] = Lerp(a.bloomMipWeights[i], b.bloomMipWeights[i], t);
			}
			r.vignetteEnable     = Snap(a.vignetteEnable, b.vignetteEnable, t);
			r.vignetteIntensity  = Lerp(a.vignetteIntensity, b.vignetteIntensity, t);
			r.caEnable           = Snap(a.caEnable, b.caEnable, t);
			r.caIntensity        = Lerp(a.caIntensity, b.caIntensity, t);
			r.lensFlareEnable    = Snap(a.lensFlareEnable, b.lensFlareEnable, t);
			r.lensFlareIntensity = Lerp(a.lensFlareIntensity, b.lensFlareIntensity, t);
			r.lensFlareGhosts    = Snap(a.lensFlareGhosts, b.lensFlareGhosts, t);
			r.dirtEnable         = Snap(a.dirtEnable, b.dirtEnable, t);
			r.dirtIntensity      = Lerp(a.dirtIntensity, b.dirtIntensity, t);

			// LUTs switch at halfway; cache misses use base.
			const std::string& chosen = (t >= 0.5f) ? b.lutPath : a.lutPath;
			if (chosen == a_base.lutPath) {
				r.lutSRV = a_baseLutSRV;
			} else {
				ID3D11ShaderResourceView* hit = a_cache.TryGet(chosen);
				if (hit) {
					r.lutSRV = hit;
				} else {
					r.lutSRV = a_baseLutSRV;
					outCacheMiss = true;
				}
			}
			return r;
		}

		ResolvedRuntime BaseRuntime(const ResolveBase& a_base, ID3D11ShaderResourceView* a_baseLutSRV) noexcept
		{
			ResolvedRuntime r{};
			r.exposure           = a_base.exposure;
			r.lutEnable          = a_base.lutEnable;
			r.lutSRV             = a_baseLutSRV;
			r.lutStrength        = a_base.lutStrength;
			r.bloomEnable        = a_base.bloomEnable;
			r.bloomThreshold     = a_base.bloomThreshold;
			r.bloomIntensity     = a_base.bloomIntensity;
			r.bloomMipWeights    = a_base.bloomMipWeights;
			r.vignetteEnable     = a_base.vignetteEnable;
			r.vignetteIntensity  = a_base.vignetteIntensity;
			r.caEnable           = a_base.caEnable;
			r.caIntensity        = a_base.caIntensity;
			r.lensFlareEnable    = a_base.lensFlareEnable;
			r.lensFlareIntensity = a_base.lensFlareIntensity;
			r.lensFlareGhosts    = a_base.lensFlareGhosts;
			r.dirtEnable         = a_base.dirtEnable;
			r.dirtIntensity      = a_base.dirtIntensity;
			r.weatherProfilesActive = false;
			return r;
		}
	}

	ResolvedRuntime Resolve(const ResolveBase& a_base,
		ID3D11ShaderResourceView*  a_baseLutSRV,
		const WeatherProfiles&     a_profiles,
		const SkySample&           a_sample,
		const LUTCache&            a_lutCache) noexcept
	{
		if (!a_profiles.enablePerWeatherProfiles || !a_sample.modeIsFull || !a_sample.current) {
			auto r = BaseRuntime(a_base, a_baseLutSRV);
			r.currentCategory  = a_sample.current  ? Classify(a_sample.current,  a_profiles.userOverrides) : WeatherCategory::kUnknown;
			r.previousCategory = a_sample.previous ? Classify(a_sample.previous, a_profiles.userOverrides) : WeatherCategory::kUnknown;
			r.transitionPct    = a_sample.transitionPct;
			return r;
		}

		const WeatherCategory curCat  = Classify(a_sample.current, a_profiles.userOverrides);
		const WeatherCategory prevCat = a_sample.previous
			? Classify(a_sample.previous, a_profiles.userOverrides)
			: curCat;
		// Missing previous weather means transition complete.
		const float t = a_sample.previous ? std::clamp(a_sample.transitionPct, 0.0f, 1.0f) : 1.0f;

		const Endpoint epPrev = BuildEndpoint(a_base, a_profiles.overlays[static_cast<std::size_t>(prevCat)]);
		const Endpoint epCur  = BuildEndpoint(a_base, a_profiles.overlays[static_cast<std::size_t>(curCat)]);

		bool cacheMiss = false;
		auto r = BlendEndpoints(epPrev, epCur, t, a_baseLutSRV, a_base, a_lutCache, cacheMiss);
		r.currentCategory       = curCat;
		r.previousCategory      = prevCat;
		r.transitionPct         = t;
		r.weatherProfilesActive = true;
		r.lutCacheMiss          = cacheMiss;
		return r;
	}

	ResolvedRuntime ResolveForced(const ResolveBase& a_base,
		ID3D11ShaderResourceView*  a_baseLutSRV,
		const WeatherProfiles&     a_profiles,
		WeatherCategory            a_category,
		const LUTCache&            a_lutCache) noexcept
	{
		if (static_cast<std::size_t>(a_category) >= static_cast<std::size_t>(WeatherCategory::kCount)) {
			a_category = WeatherCategory::kUnknown;
		}
		const Endpoint ep = BuildEndpoint(a_base, a_profiles.overlays[static_cast<std::size_t>(a_category)]);

		bool cacheMiss = false;
		auto r = BlendEndpoints(ep, ep, 1.0f, a_baseLutSRV, a_base, a_lutCache, cacheMiss);
		r.currentCategory       = a_category;
		r.previousCategory      = a_category;
		r.transitionPct         = 1.0f;
		r.weatherProfilesActive = true;
		r.lutCacheMiss          = cacheMiss;
		return r;
	}
}
