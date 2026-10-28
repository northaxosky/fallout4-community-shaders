#pragma once

#include "Settings/SettingsSchema.h"

#include <cstdint>

namespace cs::features::subsurface_scattering
{
	enum SSMode : int
	{
		kSeparable = 0,
		kBurley = 1,
	};

	enum ScatterMode : int
	{
		kPreScatter = 0,
		kPostScatter = 1,
		kPreAndPostScatter = 2,
	};

	// Upstream b4964855 defaults and ranges; profiles are flattened.
	struct Settings
	{
		bool enableCharacterLighting = false;
		float characterLightingStrength = 1.0f;
		int ssMode = kBurley;
		int scatterMode = kPreAndPostScatter;
		float baseProfileBlurRadius = 1.0f;
		float baseProfileThickness = 1.0f;
		settings::Float3 baseProfileStrength{ 0.48f, 0.41f, 0.28f };
		settings::Float3 baseProfileFalloff{ 0.56f, 0.56f, 0.56f };
		float humanProfileBlurRadius = 1.0f;
		float humanProfileThickness = 1.0f;
		settings::Float3 humanProfileStrength{ 0.48f, 0.41f, 0.28f };
		settings::Float3 humanProfileFalloff{ 1.0f, 0.37f, 0.3f };
		std::uint32_t burleySamples = 16;
		// Xyz is the mean free path color, w its distance.
		settings::Color4 meanFreePathBase{ 0.56f, 0.56f, 0.56f, 2.67f };
		settings::Color4 meanFreePathHuman{ 1.0f, 0.37f, 0.3f, 2.67f };
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "EnableCharacterLighting", "Enable Fallout 4's character light.", &Settings::enableCharacterLighting },
			settings::Field{ "CharacterLightingStrength", "Scale of the character light's rim and fill.", &Settings::characterLightingStrength, {}, settings::Range{ 0.0f, 5.0f } },
			settings::Field{ "SSMode", "Scattering method: 0 separable, 1 Burley.", &Settings::ssMode, settings::Range{ 0, 1 } },
			settings::Field{ "ScatterMode", "Separable albedo handling: 0 pre-scatter, 1 post-scatter, 2 pre and post.", &Settings::scatterMode, settings::Range{ 0, 2 } },
			settings::Field{ "BaseProfileBlurRadius", "Base profile blur radius.", &Settings::baseProfileBlurRadius, {}, settings::Range{ 0.0f, 3.0f } },
			settings::Field{ "BaseProfileThickness", "Base profile blur radius relative to depth.", &Settings::baseProfileThickness, {}, settings::Range{ 0.0f, 3.0f } },
			settings::Float3Field<Settings>{ "BaseProfileStrength", "Base profile scattering strength per channel.", &Settings::baseProfileStrength },
			settings::Float3Field<Settings>{ "BaseProfileFalloff", "Base profile falloff per channel.", &Settings::baseProfileFalloff },
			settings::Field{ "HumanProfileBlurRadius", "Human profile blur radius.", &Settings::humanProfileBlurRadius, {}, settings::Range{ 0.0f, 3.0f } },
			settings::Field{ "HumanProfileThickness", "Human profile blur radius relative to depth.", &Settings::humanProfileThickness, {}, settings::Range{ 0.0f, 3.0f } },
			settings::Float3Field<Settings>{ "HumanProfileStrength", "Human profile scattering strength per channel.", &Settings::humanProfileStrength },
			settings::Float3Field<Settings>{ "HumanProfileFalloff", "Human profile falloff per channel.", &Settings::humanProfileFalloff },
			settings::Field{ "BurleySamples", "Burley samples per pixel.", &Settings::burleySamples, settings::Range{ 1u, 64u } },
			settings::ColorField<Settings>{ "MeanFreePathBase", "Base profile Burley mean free path color and distance.", &Settings::meanFreePathBase },
			settings::ColorField<Settings>{ "MeanFreePathHuman", "Human profile Burley mean free path color and distance.", &Settings::meanFreePathHuman } }
	};
}
