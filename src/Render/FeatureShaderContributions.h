#pragma once

#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"

#include <initializer_list>

namespace cs::engine
{
	inline const std::vector<ShaderReplacementRegistration>& GetFeatureShaderContributions()
	{
		static const auto contributions = [] {
			using enum ShaderInjectionTarget;
			using namespace shader_injection_defines;
			std::vector<ShaderReplacementRegistration> result;
			const auto add = [&](const char* a_contributor, const char* a_define,
								 std::initializer_list<ShaderInjectionTarget> a_targets,
								 const char* a_debugDefine = nullptr,
								 bool a_graphicsPair = false) {
				for (const auto target : a_targets) {
					ShaderReplacementRegistration registration{
						.targetId = target,
						.stages = ShaderStageBit(target == kDfTiledLighting ? ShaderStage::kCompute : ShaderStage::kPixel),
						.contributor = a_contributor,
						.defines = { { a_define, "1" } }
					};
					if (target == kBsdfComposite && a_debugDefine)
						registration.defines.emplace(a_debugDefine, "1");
					if (a_graphicsPair) {
						registration.stages |= ShaderStageBit(ShaderStage::kVertex);
						registration.requiresGraphicsPair = true;
					}
					result.push_back(std::move(registration));
				}
			};
			add("ScreenSpaceShadows", kScreenSpaceShadows, { kBsdfLight });
			add("ScreenSpaceGI", kScreenSpaceGi, { kBsdfComposite, kDeferredPrepass });
			add("InverseSquareLighting", kInverseSquareLighting, { kBsdfLight, kDfTiledLighting });
			add("ExponentialHeightFog", kExponentialHeightFog,
				{ kBsdfComposite, kBsWater, kEffect, kDistantTree, kBsdfLight },
				"EXPONENTIAL_HEIGHT_FOG_FULLSCREEN_DEBUG");
			add("DynamicCubemaps", kDynamicCubemaps, { kBsWater, kBsdfComposite, kBsdfLight, kDfTiledLighting, kImageSpace });
			add("WetnessEffects", kWetnessEffects, { kDeferredPrepass }, nullptr, true);
			add("WetnessEffects", kWetnessEffects, { kBsdfLight, kDfTiledLighting, kBsdfComposite },
				kWetnessEffectsFullscreenDebug);
			add("TerrainShadows", kTerrainShadows, { kBsdfLight, kDistantTree, kBsWater, kEffect, kBsdfComposite },
				kTerrainShadowsFullscreenDebug);
			add("WaterEffects", kWaterEffects, { kBsdfLight, kBsdfComposite }, kWaterEffectsFullscreenDebug);
			add("Upscaling", "UPSCALING", { kImageSpace });
			return result;
		}();
		return contributions;
	}

	template <class Configure>
	bool RegisterFeatureShaderContributions(std::string_view a_contributor, Configure&& a_configure)
	{
		bool found = false;
		for (const auto& contribution : GetFeatureShaderContributions()) {
			if (contribution.contributor != a_contributor)
				continue;
			found = true;
			auto registration = contribution;
			a_configure(registration);
			if (!RegisterReplacement(std::move(registration)))
				return false;
		}
		return found;
	}
}
