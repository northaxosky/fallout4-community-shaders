#pragma once

#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cs::features::volumetric_lighting
{
	inline constexpr std::int32_t kCustomQuality = 4;

	struct Settings
	{
		bool ExteriorEnabled = true;
		std::int32_t ExteriorQuality = 2;
		std::uint32_t ExteriorCustomResolution = 2;
		std::uint32_t ExteriorCustomSamples = 4;
		std::uint32_t ExteriorCustomUpsample = 2;
		float ExteriorCustomRaySize = 8.0f;
		bool InteriorEnabled = true;
		std::int32_t InteriorQuality = 2;
		std::uint32_t InteriorCustomResolution = 2;
		std::uint32_t InteriorCustomSamples = 4;
		std::uint32_t InteriorCustomUpsample = 2;
		float InteriorCustomRaySize = 8.0f;
	};

	// Resolution is a divisor of output resolution.
	inline constexpr std::array kResolutionChoices{
		settings::Choice<std::uint32_t>{ "Full", 1 },
		settings::Choice<std::uint32_t>{ "Half", 2 },
		settings::Choice<std::uint32_t>{ "Quarter", 4 }
	};
	inline constexpr std::array kSampleChoices{
		settings::Choice<std::uint32_t>{ "1x", 1 },
		settings::Choice<std::uint32_t>{ "2x", 2 },
		settings::Choice<std::uint32_t>{ "4x", 4 }
	};
	// Engine values: 1 bilinear, 2 bilateral.
	inline constexpr std::array kUpsampleChoices{
		settings::Choice<std::uint32_t>{ "Bilinear", 1 },
		settings::Choice<std::uint32_t>{ "Bilateral", 2 }
	};

	inline constexpr settings::Range<float> kRaySizeRange{ 4.0f, 64.0f };

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "ExteriorEnabled", "Enable Volumetric Lighting in Exteriors", &Settings::ExteriorEnabled },
			settings::Field{ "ExteriorQuality", "Exterior Quality", &Settings::ExteriorQuality, settings::Range<std::int32_t>{ 0, kCustomQuality } },
			settings::ChoiceField{ "ExteriorCustomResolution", "Exterior Custom Resolution", &Settings::ExteriorCustomResolution, kResolutionChoices },
			settings::ChoiceField{ "ExteriorCustomSamples", "Exterior Custom Samples", &Settings::ExteriorCustomSamples, kSampleChoices },
			settings::ChoiceField{ "ExteriorCustomUpsample", "Exterior Custom Upsample", &Settings::ExteriorCustomUpsample, kUpsampleChoices },
			settings::Field{ "ExteriorCustomRaySize", "Exterior Custom Ray Size", &Settings::ExteriorCustomRaySize, kRaySizeRange },
			settings::Field{ "InteriorEnabled", "Enable Volumetric Lighting in Interiors", &Settings::InteriorEnabled },
			settings::Field{ "InteriorQuality", "Interior Quality", &Settings::InteriorQuality, settings::Range<std::int32_t>{ 0, kCustomQuality } },
			settings::ChoiceField{ "InteriorCustomResolution", "Interior Custom Resolution", &Settings::InteriorCustomResolution, kResolutionChoices },
			settings::ChoiceField{ "InteriorCustomSamples", "Interior Custom Samples", &Settings::InteriorCustomSamples, kSampleChoices },
			settings::ChoiceField{ "InteriorCustomUpsample", "Interior Custom Upsample", &Settings::InteriorCustomUpsample, kUpsampleChoices },
			settings::Field{ "InteriorCustomRaySize", "Interior Custom Ray Size", &Settings::InteriorCustomRaySize, kRaySizeRange } }
	};

	struct Location
	{
		bool Settings::* enabled;
		std::int32_t Settings::* quality;
		std::uint32_t Settings::* resolution;
		std::uint32_t Settings::* samples;
		std::uint32_t Settings::* upsample;
		float Settings::* raySize;
	};

	inline constexpr Location kExterior{
		&Settings::ExteriorEnabled, &Settings::ExteriorQuality, &Settings::ExteriorCustomResolution,
		&Settings::ExteriorCustomSamples, &Settings::ExteriorCustomUpsample, &Settings::ExteriorCustomRaySize
	};
	inline constexpr Location kInterior{
		&Settings::InteriorEnabled, &Settings::InteriorQuality, &Settings::InteriorCustomResolution,
		&Settings::InteriorCustomSamples, &Settings::InteriorCustomUpsample, &Settings::InteriorCustomRaySize
	};

	// Output-relative godray quality.
	struct Level
	{
		std::uint32_t divisor;
		std::uint32_t samples;
		float raySize;
		std::uint32_t upsample;
	};

	inline constexpr std::array<Level, 4> kLevels{
		Level{ 4, 1, 32.0f, 2 },
		Level{ 2, 1, 16.0f, 2 },
		Level{ 2, 4, 8.0f, 2 },
		Level{ 1, 1, 8.0f, 2 }
	};

	inline Level ResolveLevel(const Settings& a_settings, const Location& a_location)
	{
		const auto quality = a_settings.*a_location.quality;
		if (quality == kCustomQuality) {
			return { a_settings.*a_location.resolution, a_settings.*a_location.samples,
				a_settings.*a_location.raySize, a_settings.*a_location.upsample };
		}
		return kLevels[static_cast<std::size_t>(std::clamp(quality, 0, 3))];
	}

	// The engine buffer is render size >> shift; flooring keeps it at or above the target.
	inline std::uint32_t BufferShift(std::uint32_t a_divisor, float a_renderScale)
	{
		const float scaled = static_cast<float>(a_divisor) * a_renderScale;
		const float shift = std::floor(std::log2(std::max(scaled, 1.0f)) + 1e-3f);
		return static_cast<std::uint32_t>(std::clamp(shift, 0.0f, 2.0f));
	}
}
