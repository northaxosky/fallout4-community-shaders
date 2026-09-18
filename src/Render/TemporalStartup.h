#pragma once

#include "Render/TemporalPipelineState.h"
#include "Render/TemporalProvider.h"

#include <format>

namespace cs::render::temporal
{
	struct SuperResolutionAdmission
	{
		std::array<bool, static_cast<std::size_t>(SuperResolutionMethod::kCount)> methods{};
		std::string detail;
	};

	template <class Initialize>
	[[nodiscard]] SuperResolutionAdmission InitializeSelectedSuperResolution(
		const RequestedTopology& a_request,
		Initialize&& a_initialize)
	{
		SuperResolutionAdmission admission;
		if (!a_request.upscalingEligible)
			return admission;

		admission.methods[static_cast<std::size_t>(SuperResolutionMethod::kNone)] = true;
		admission.methods[static_cast<std::size_t>(SuperResolutionMethod::kTAA)] = true;
		const auto initialize = [&](SuperResolutionMethod a_method) -> ProviderResult {
			const auto index = static_cast<std::size_t>(a_method);
			if (index >= admission.methods.size()) {
				return {
					.code = ProviderResultCode::kUnavailable,
					.message = "Invalid startup super-resolution method."
				};
			}
			if (a_method == SuperResolutionMethod::kNone || a_method == SuperResolutionMethod::kTAA)
				return { .code = ProviderResultCode::kSuccess };
			auto result = a_initialize(a_method);
			admission.methods[index] = result.Succeeded();
			return result;
		};

		const auto requested = initialize(a_request.superResolution);
		if (requested.Succeeded())
			return admission;

		admission.detail = std::format(
			"Requested super resolution is unavailable: {} (SDK {}).",
			requested.message, requested.sdkResult);
		const bool externalRequested =
			a_request.superResolution == SuperResolutionMethod::kFSR3 ||
			a_request.superResolution == SuperResolutionMethod::kDLSS ||
			a_request.superResolution == SuperResolutionMethod::kFSR4;
		if (externalRequested) {
			admission.detail +=
				" Native TAA remains active for this startup.";
		}
		return admission;
	}
}
