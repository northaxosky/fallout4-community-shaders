#pragma once

#include "FeatureShaderDefines.h"
#include "Render/FeatureShaderBindings.h"

#include <algorithm>

namespace cs::engine
{
	inline const std::vector<ShaderReplacementRegistration>& GetFeatureShaderContributions()
	{
		static const auto contributions = [] {
			auto registry = GetShaderContributionRegistry();
			std::ranges::sort(registry, {}, &ShaderContribution::name);
			std::vector<ShaderReplacementRegistration> result;
			for (const auto& [name, provider] : registry)
				result.append_range(DescribeFeatureShaderBindings(name, *provider));
			return result;
		}();
		return contributions;
	}
}
