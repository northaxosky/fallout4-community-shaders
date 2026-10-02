#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::lod_blending
{
	struct Settings
	{
		float LODTerrainBrightness = 1.0f;
		float LODObjectBrightness = 1.0f;
		float LODTerrainGamma = 1.0f;
		float LODObjectGamma = 1.0f;
		bool DisableTerrainVertexColors = false;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "LODTerrainBrightness", "LOD Terrain Brightness", &Settings::LODTerrainBrightness, settings::Range{ 0.01f, 5.0f } },
			settings::Field{ "LODObjectBrightness", "LOD Object Brightness", &Settings::LODObjectBrightness, settings::Range{ 0.01f, 5.0f } },
			settings::Field{ "LODTerrainGamma", "LOD Terrain Gamma", &Settings::LODTerrainGamma, settings::Range{ 0.1f, 3.0f } },
			settings::Field{ "LODObjectGamma", "LOD Object Gamma", &Settings::LODObjectGamma, settings::Range{ 0.1f, 3.0f } },
			settings::Field{ "DisableTerrainVertexColors", "Disable Terrain Vertex Colors", &Settings::DisableTerrainVertexColors } }
	};
}
