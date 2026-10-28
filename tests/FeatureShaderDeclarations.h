#pragma once

#include "../features/DynamicCubemaps/src/ShaderDefines.h"
#include "../features/ExtendedMaterials/src/ShaderDefines.h"
#include "../features/IBL/src/ShaderDefines.h"
#include "../features/InverseSquareLighting/src/ShaderDefines.h"
#include "../features/LODBlending/src/ShaderDefines.h"
#include "../features/ScreenSpaceGI/src/ShaderDefines.h"
#include "../features/ScreenSpaceShadows/src/ShaderDefines.h"
#include "../features/Skylighting/src/ShaderDefines.h"
#include "../features/SubsurfaceScattering/src/ShaderDefines.h"
#include "../features/TerrainBlending/src/ShaderDefines.h"
#include "../features/TerrainShadows/src/ShaderDefines.h"
#include "../features/TerrainVariation/src/ShaderDefines.h"
#include "../features/UnifiedWater/src/ShaderDefines.h"
#include "../features/Upscaling/src/ShaderDefines.h"
#include "../features/VolumetricShadows/src/ShaderDefines.h"
#include "../features/WaterEffects/src/ShaderDefines.h"
#include "Render/FeatureShaderBindings.h"

namespace cs::engine
{
	inline const std::vector<ShaderReplacementRegistration>& GetFeatureShaderContributions()
	{
		static const auto contributions = [] {
			using namespace cs::features;
			std::vector<ShaderReplacementRegistration> result;
			const std::pair<std::string_view, const ShaderDefineProvider*> features[]{
				{ "ScreenSpaceShadows", &sss::kShaderDefines },
				{ "ScreenSpaceGI", &ssgi::kShaderDefines },
				{ "SubsurfaceScattering", &subsurface_scattering_shader::kShaderDefines },
				{ "Skylighting", &skylighting_shader::kShaderDefines },
				{ "InverseSquareLighting", &isl::kShaderDefines },
				{ "DynamicCubemaps", &dc::kShaderDefines },
				{ "ImageBasedLighting", &ibl_shader::kShaderDefines },
				{ "TerrainShadows", &terrain_shader::kShaderDefines },
				{ "VolumetricShadows", &volumetric_shadows_shader::kShaderDefines },
				{ "LODBlending", &lod_blending_shader::kShaderDefines },
				{ "TerrainVariation", &terrain_variation_shader::kShaderDefines },
				{ "TerrainBlending", &terrain_blending_shader::kShaderDefines },
				{ "ExtendedMaterials", &extended_materials_shader::kShaderDefines },
				{ "WaterEffects", &water_effects::kShaderDefines },
				{ "UnifiedWater", &unified_water::kShaderDefines },
				{ "UpscalingSamplerBias", &upscaling::kSamplerBiasShaderDefines },
				{ "UpscalingImageSpaceUv", &upscaling::kImageSpaceUvShaderDefines }
			};
			for (const auto& [name, feature] : features) {
				result.append_range(DescribeFeatureShaderBindings(name, *feature));
			}
			return result;
		}();
		return contributions;
	}
}
