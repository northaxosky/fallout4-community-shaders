#include "Compat/Rules.h"

#include "DynamicCubemaps.h"
#include "Feature.h"
#include "FrameGeneration.h"
#include "HDRDisplay.h"
#include "ImageBasedLighting.h"
#include "MotionVectorFixes.h"
#include "RenderDoc.h"
#include "Upscaling.h"

#include <array>

namespace cs::compat
{
	namespace
	{
		using namespace cs::features;

		template <class T>
		Feature* Ref() noexcept
		{
			return T::GetSingleton();
		}

		constexpr std::array<FeatureRef, 1> kMotionVectorFixes{ Ref<MotionVectorFixes> };
		// Image Based Lighting needs Dynamic Cubemaps.
		constexpr std::array<FeatureRef, 2> kDynamicCubemaps{ Ref<DynamicCubemaps>, Ref<ImageBasedLighting> };
		constexpr std::array<FeatureRef, 1> kHdrDisplay{ Ref<HDRDisplay> };
		// These replace the game's device creation.
		constexpr std::array<FeatureRef, 4> kDeviceCreation{ Ref<HDRDisplay>, Ref<Upscaling>, Ref<FrameGeneration>, Ref<RenderDoc> };

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
			// It patches stock shader bytecode, so no CS shader replacement can coexist.
			Rule{ "Cloud Shadows (Asciim0v)", { Source::kF4SEPlugin, "FO4CloudShadows" }, {},
				"Turned off all features because the standalone Cloud Shadows mod is not supported. Remove it to use them." },
			Rule{ "Isotope", { Source::kF4SEPlugin, "FO4Isotope" }, kDeviceCreation,
				"Turned off HDR Display, Upscaling, Frame Generation and RenderDoc to avoid conflicting with Isotope. Remove Isotope to use them." },
			Rule{ "RenoDX", { Source::kGameFile, "renodx-fallout4.addon64" }, kHdrDisplay,
				"Turned off HDR Display to avoid conflicting with RenoDX's HDR. Remove RenoDX to use it." },
			Rule{ "ENB", { Source::kENB }, {},
				"ENB detected. All features are off and your execution has been scheduled. Remove ENB to use them." },
		};
	}

	std::span<const Rule> Rules() noexcept
	{
		return kRules;
	}
}
