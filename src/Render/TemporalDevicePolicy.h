#pragma once

#include "Render/TemporalPipelineState.h"

#include <algorithm>
#include <vector>

#include <d3d11.h>

namespace cs::render::temporal
{
	inline constexpr D3D_FEATURE_LEVEL kFsrMinimumFeatureLevel =
		D3D_FEATURE_LEVEL_11_1;

	inline void ConfigureTemporalFeatureLevels(
		const RequestedTopology& a_request,
		std::vector<D3D_FEATURE_LEVEL>& a_levels)
	{
		if (!a_request.upscalingEligible &&
			!(a_request.frameGenerationEligible &&
				a_request.frameGeneration == FrameGenerationMethod::kDLSSG)) {
			return;
		}
		if (a_levels.empty()) {
			// An empty engine list means D3D11's default fallback levels.
			a_levels = {
				D3D_FEATURE_LEVEL_11_0,
				D3D_FEATURE_LEVEL_10_1,
				D3D_FEATURE_LEVEL_10_0,
				D3D_FEATURE_LEVEL_9_3,
				D3D_FEATURE_LEVEL_9_2,
				D3D_FEATURE_LEVEL_9_1
			};
		}
		std::erase(a_levels, kFsrMinimumFeatureLevel);
		const auto lower = std::ranges::find_if(a_levels, [](auto a_level) {
			return a_level < kFsrMinimumFeatureLevel;
		});
		a_levels.insert(lower, kFsrMinimumFeatureLevel);
	}
}
