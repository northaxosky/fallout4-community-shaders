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
			settings::Field{ "VertexAOStrength", "Vertex AO Strength", &Settings::VertexAOStrength, settings::Range{ 0.0f, 1.0f } }.Help("How much vertex color darkening occludes the material. Zero removes it."),
			settings::Field{ "EnableMicroShadows", "Micro Shadows", &Settings::EnableMicroShadows }.Help("Occludes direct light with the material's ambient occlusion map, adding contact shadowing to surface detail such as seams, rivets and fabric weave."),
			settings::Field{ "MicroShadowStrength", "Micro Shadow Strength", &Settings::MicroShadowStrength, settings::Range{ 0.0f, 1.0f } }.Help("How strongly ambient occlusion darkens direct light.") }
	};
}
