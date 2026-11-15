#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::true_pbr
{
	struct Settings
	{
		float VertexAOStrength = 1.0f;
		bool EnableMicroShadows = true;
		float MicroShadowStrength = 1.0f;
	};
	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "VertexAOStrength", "Vertex AO Strength", &Settings::VertexAOStrength, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "EnableMicroShadows", "Micro Shadows", &Settings::EnableMicroShadows },
			settings::Field{ "MicroShadowStrength", "Micro Shadow Strength", &Settings::MicroShadowStrength, settings::Range{ 0.0f, 1.0f } } }
	};
}
