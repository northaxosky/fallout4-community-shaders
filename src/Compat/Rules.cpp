#include "Compat/Rules.h"

#include "Settings/FeatureKeys.h"

#include <array>

namespace cs::compat
{
	namespace
	{
		namespace fk = cs::feature_config;

		constexpr std::array<std::string_view, 1> kMotionVectorFixes{ fk::kMotionVectorFixes };
		// Image Based Lighting needs Dynamic Cubemaps.
		constexpr std::array<std::string_view, 2> kDynamicCubemaps{ fk::kDynamicCubemaps, fk::kImageBasedLighting };
		constexpr std::array<std::string_view, 1> kCloudShadows{ fk::kCloudShadows };
		constexpr std::array<std::string_view, 1> kHdrDisplay{ fk::kHDRDisplay };
		// These replace the game's device creation.
		constexpr std::array<std::string_view, 4> kDeviceCreation{ fk::kHDRDisplay, fk::kUpscaling, fk::kFrameGeneration, fk::kRenderDoc };

		constexpr std::array kRules{
			Rule{ "Motion Vector Fixes", { Source::kF4SEPlugin, "MotionVectorFixes" }, kMotionVectorFixes,
				"Turned off Motion Vector Fixes to avoid conflicting with the standalone Motion Vector Fixes mod. Remove that mod to use it." },
			Rule{ "Dynamic Cubemaps F4", { Source::kF4SEPlugin, "DynamicCubemapF4" }, kDynamicCubemaps,
				"Turned off Dynamic Cubemaps and Image Based Lighting to avoid conflicting with the standalone Dynamic Cubemaps mod. Remove that mod to use them." },
			// doodlum's Upscaling and jarari's Upscaling Custom share this plugin name.
			Rule{ "Upscaling", { Source::kF4SEPlugin, "Upscaling" }, kDeviceCreation,
				"Turned off HDR Display, Upscaling, Frame Generation and RenderDoc to avoid conflicting with the standalone Upscaling mod. Remove that mod to use them." },
			Rule{ "Frame Generation", { Source::kF4SEPlugin, "AAAFrameGeneration" }, kDeviceCreation,
				"Turned off HDR Display, Upscaling, Frame Generation and RenderDoc to avoid conflicting with the standalone Frame Generation mod. Remove that mod to use them." },
			Rule{ "Cloud Shadows (1001Bits)", { Source::kF4SEPlugin, "FO4CloudShadows" }, kCloudShadows,
				"Turned off Cloud Shadows to avoid conflicting with the standalone Cloud Shadows mod. Remove that mod to use it." },
			Rule{ "Isotope", { Source::kF4SEPlugin, "FO4Isotope" }, kDeviceCreation,
				"Turned off HDR Display, Upscaling, Frame Generation and RenderDoc to avoid conflicting with Isotope. Remove Isotope to use them." },
			Rule{ "RenoDX", { Source::kGameFile, "renodx-fallout4.addon64" }, kHdrDisplay,
				"Turned off HDR Display to avoid conflicting with RenoDX's HDR. Remove RenoDX to use it." },
			Rule{ "ENB", { Source::kENB }, {},
				"Turned off all features because ENB is not supported." },
		};
	}

	std::span<const Rule> Rules() noexcept
	{
		return kRules;
	}
}
