#pragma once

#include "Settings/SettingsSchema.h"

#include <array>
#include <cstdint>

namespace cs::features::light_limit_fix
{
	inline constexpr std::uint32_t kMinLocalShadowSlots = 4;
	inline constexpr std::uint32_t kMaxLocalShadowSlots = 64;
	inline constexpr float kMinLocalShadowFilterScale = 0.25f;
	inline constexpr float kMaxLocalShadowFilterScale = 2.0f;

	enum class LocalShadowResolution : std::uint32_t
	{
		k512 = 512,
		k1024 = 1024,
		k2048 = 2048
	};

	enum class LocalShadowSamples : std::uint32_t
	{
		k1 = 1,
		k4 = 4,
		k8 = 8
	};

	struct Settings
	{
		bool deriveUnauthoredLights = false;
		bool enableLocalShadows = false;
		std::uint32_t localShadowSlots = 16;
		LocalShadowResolution localShadowResolution = LocalShadowResolution::k1024;
		LocalShadowSamples localShadowSamples = LocalShadowSamples::k8;
		float localShadowFilterScale = 1.0f;
	};

	// Live local shadow settings as the render thread reads them.
	struct LocalShadowConfig
	{
		bool enabled;
		std::uint32_t slots;
		std::uint32_t resolution;
		std::uint32_t samples;
		float filterScale;
	};

	inline constexpr std::array kLocalShadowResolutions{
		settings::Choice{ "512", LocalShadowResolution::k512 },
		settings::Choice{ "1024", LocalShadowResolution::k1024 },
		settings::Choice{ "2048", LocalShadowResolution::k2048 }
	};

	inline constexpr std::array kLocalShadowSampleCounts{
		settings::Choice{ "1", LocalShadowSamples::k1 },
		settings::Choice{ "4", LocalShadowSamples::k4 },
		settings::Choice{ "8", LocalShadowSamples::k8 }
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "derive_unauthored_lights", "Derive Unauthored Lights", &Settings::deriveUnauthoredLights, {}, settings::ApplyTiming::kNextLaunch },
			settings::Field{ "enable_local_shadows", "Enable local light shadows on effects and water.", &Settings::enableLocalShadows },
			settings::Field{ "local_shadow_slots", "Number of shadow-casting lights kept in the local shadow cache.", &Settings::localShadowSlots, settings::Range{ kMinLocalShadowSlots, kMaxLocalShadowSlots } },
			settings::ChoiceField{ "local_shadow_resolution", "Local shadow cache resolution in texels.", &Settings::localShadowResolution, kLocalShadowResolutions },
			settings::ChoiceField{ "local_shadow_samples", "Filter taps per local shadow lookup.", &Settings::localShadowSamples, kLocalShadowSampleCounts },
			settings::Field{ "local_shadow_filter_scale", "Scales the local shadow filter radius.", &Settings::localShadowFilterScale, settings::Range{ kMinLocalShadowFilterScale, kMaxLocalShadowFilterScale } } }
	};
}
