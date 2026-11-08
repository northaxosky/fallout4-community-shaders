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

	struct Settings
	{
		bool deriveUnauthoredLights = false;
		bool enableLocalShadows = false;
		std::uint32_t localShadowSlots = 16;
		// 0 follows the engine shadow map resolution.
		std::uint32_t localShadowResolution = 0;
		std::uint32_t localShadowSamples = 8;
		float localShadowFilterScale = 1.0f;
	};

	// Live local shadow settings as the render thread reads them.
	struct LocalShadowConfig
	{
		bool enabled = false;
		std::uint32_t slots = 16;
		std::uint32_t resolution = 0;
		std::uint32_t samples = 8;
		float filterScale = 1.0f;
	};

	inline constexpr std::array kLocalShadowResolutions{
		settings::Choice<std::uint32_t>{ "engine", 0 },
		settings::Choice<std::uint32_t>{ "512", 512 },
		settings::Choice<std::uint32_t>{ "1024", 1024 },
		settings::Choice<std::uint32_t>{ "2048", 2048 }
	};

	inline constexpr std::array kLocalShadowSampleCounts{
		settings::Choice<std::uint32_t>{ "1", 1 },
		settings::Choice<std::uint32_t>{ "4", 4 },
		settings::Choice<std::uint32_t>{ "8", 8 }
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "derive_unauthored_lights", "Derive Unauthored Lights", &Settings::deriveUnauthoredLights, {}, settings::ApplyTiming::kNextLaunch },
			settings::Field{ "enable_local_shadows", "Enable local light shadows on effects and water.", &Settings::enableLocalShadows },
			settings::Field{ "local_shadow_slots", "Number of shadow-casting lights kept in the local shadow cache.", &Settings::localShadowSlots, settings::Range{ kMinLocalShadowSlots, kMaxLocalShadowSlots } },
			settings::ChoiceField{ "local_shadow_resolution", "Local shadow cache resolution in texels; engine follows the shadow map resolution.", &Settings::localShadowResolution, kLocalShadowResolutions },
			settings::ChoiceField{ "local_shadow_samples", "Filter taps per local shadow lookup.", &Settings::localShadowSamples, kLocalShadowSampleCounts },
			settings::Field{ "local_shadow_filter_scale", "Scales the local shadow filter radius.", &Settings::localShadowFilterScale, settings::Range{ kMinLocalShadowFilterScale, kMaxLocalShadowFilterScale } } }
	};
}
