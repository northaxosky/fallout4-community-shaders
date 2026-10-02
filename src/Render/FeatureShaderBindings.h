#pragma once

#include "Render/ShaderDefineProvider.h"
#include "Render/ShaderInjection.h"

namespace cs::engine
{
	inline std::vector<ShaderReplacementRegistration> DescribeFeatureShaderBindings(std::string_view a_name, const ShaderDefineProvider& a_feature)
	{
		std::vector<ShaderReplacementRegistration> result;
		for (const auto& target : kShaderInjectionTargets) {
			if (!a_feature.HasShaderDefine(target.id))
				continue;
			ShaderReplacementRegistration registration{
				.targetId = target.id,
				.stages = ShaderStageBit(target.id == ShaderInjectionTarget::kDfTiledLighting ? ShaderStage::kCompute : ShaderStage::kPixel),
				.contributor = std::string(a_name),
				.feature = &a_feature
			};
			if (a_feature.RequiresShaderGraphicsPair(target.id)) {
				registration.stages |= ShaderStageBit(ShaderStage::kVertex);
				registration.requiresGraphicsPair = true;
			}
			result.push_back(std::move(registration));
		}
		return result;
	}

	template <class Configure>
	bool RegisterFeatureShaderBindings(std::string_view a_name, const ShaderDefineProvider& a_feature, Configure&& a_configure)
	{
		auto registrations = DescribeFeatureShaderBindings(a_name, a_feature);
		for (auto& registration : registrations) {
			a_configure(registration);
			if (!RegisterReplacement(std::move(registration)))
				return false;
		}
		return !registrations.empty();
	}

	inline bool RegisterFeatureShaderBindings(std::string_view a_name, const ShaderDefineProvider& a_feature)
	{
		return RegisterFeatureShaderBindings(a_name, a_feature, [](ShaderReplacementRegistration&) {});
	}
}
