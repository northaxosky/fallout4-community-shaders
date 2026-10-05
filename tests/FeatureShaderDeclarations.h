#pragma once

#include "../features/DynamicCubemaps/src/ShaderDefines.h"
#include "../features/ExponentialHeightFog/src/ShaderDefines.h"
#include "../features/InverseSquareLighting/src/ShaderDefines.h"
#include "../features/LODBlending/src/ShaderDefines.h"
#include "../features/ScreenSpaceGI/src/ShaderDefines.h"
#include "../features/ScreenSpaceShadows/src/ShaderDefines.h"
#include "../features/TerrainShadows/src/ShaderDefines.h"
#include "../features/Upscaling/src/ShaderDefines.h"
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
				{ "InverseSquareLighting", &isl::kShaderDefines },
				{ "ExponentialHeightFog", &fog_shader::kShaderDefines },
				{ "DynamicCubemaps", &dc::kShaderDefines },
				{ "TerrainShadows", &terrain_shader::kShaderDefines },
				{ "LODBlending", &lod_blending_shader::kShaderDefines },
				{ "WaterEffects", &water_effects::kShaderDefines },
				{ "Upscaling", &upscaling::kShaderDefines }
			};
			for (const auto& [name, feature] : features) {
				result.append_range(DescribeFeatureShaderBindings(name, *feature));
			}
			return result;
		}();
		return contributions;
	}
}
