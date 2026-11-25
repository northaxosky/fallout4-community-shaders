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

		// Isotope's hosted presentation (1.4.0) leaves device creation and presentation to CS.
		bool IsotopeHostedPresentation(const REL::Version& a_version)
		{
			if (a_version < REL::Version{ 1, 4, 0 })
				return false;
			std::error_code ec;
			const auto ini = std::filesystem::absolute("Data/F4SE/Plugins/FO4Isotope.ini", ec);
			return !ec && ::GetPrivateProfileIntW(L"General", L"HostedPresentation", 0, ini.c_str()) == 1;
		}

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
				"Turned off HDR Display, Upscaling, Frame Generation and RenderDoc to avoid conflicting with Isotope. Remove Isotope, or turn on its Hosted Presentation setting, to use them.",
				&IsotopeHostedPresentation },
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
