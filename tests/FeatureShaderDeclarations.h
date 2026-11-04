#pragma once

#include "FeatureShaderDefines.h"
#include "Render/FeatureShaderBindings.h"

#include <algorithm>

namespace cs::shader_test
{
	inline const std::vector<engine::ShaderReplacementRegistration>& GetFeatureShaderContributions()
	{
		static const auto contributions = [] {
			auto registry = engine::GetShaderContributionRegistry();
			std::ranges::sort(registry, {}, &engine::ShaderContribution::name);
			std::vector<engine::ShaderReplacementRegistration> result;
			for (const auto& [name, provider] : registry)
				result.append_range(engine::DescribeFeatureShaderBindings(name, *provider));
			return result;
		}();
		return contributions;
	}
}
