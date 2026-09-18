#pragma once

#include <sl_fsr.h>
#include <sl_fsr_g.h>

#include "Render/TemporalProvider.h"

namespace cs::features::streamline_fidelityfx
{
	enum class FSRGStateValidation
	{
		kValid,
		kCompletionFenceUnavailable,
		kAlgorithmUnavailable,
		kSubmittedDependencyMissing
	};

	[[nodiscard]] inline render::temporal::CapabilityAvailability
	ClassifyCapability(
		sl::Result a_result,
		sl::Boolean a_available) noexcept
	{
		if (a_result != sl::Result::eOk) {
			return render::temporal::CapabilityAvailability::kUnknown;
		}
		return a_available == sl::Boolean::eTrue
			? render::temporal::CapabilityAvailability::kSupported
			: render::temporal::CapabilityAvailability::kUnsupported;
	}

	inline void SelectAlgorithm(
		sl::FSROptions& a_options,
		sl::FSRAlgorithmOptions& a_algorithmOptions,
		sl::FSRAlgorithm a_algorithm) noexcept
	{
		a_options.next = nullptr;
		if (a_algorithm == sl::FSRAlgorithm::eFSR4) {
			a_algorithmOptions.algorithm = a_algorithm;
			a_options.next = &a_algorithmOptions;
		}
	}

	inline void SelectAlgorithm(
		sl::FSRGOptions& a_options,
		sl::FSRGAlgorithmOptions& a_algorithmOptions,
		sl::FSRGAlgorithm a_algorithm) noexcept
	{
		a_options.next = nullptr;
		if (a_algorithm == sl::FSRGAlgorithm::eFSR4) {
			a_algorithmOptions.algorithm = a_algorithm;
			a_options.next = &a_algorithmOptions;
		}
	}

	[[nodiscard]] inline FSRGStateValidation ValidateState(
		const sl::FSRGState& a_state,
		sl::FSRGAlgorithm a_expectedAlgorithm,
		bool a_requireSubmittedDependency) noexcept
	{
		if (a_state.completionMode != sl::FSRGCompletionMode::eFence ||
			!a_state.completionFence) {
			return FSRGStateValidation::kCompletionFenceUnavailable;
		}
		if (a_requireSubmittedDependency &&
			(a_state.algorithm != a_expectedAlgorithm ||
				a_state.available != sl::Boolean::eTrue)) {
			return FSRGStateValidation::kAlgorithmUnavailable;
		}
		if (a_requireSubmittedDependency &&
			a_state.completionFenceValue == 0) {
			return FSRGStateValidation::kSubmittedDependencyMissing;
		}
		return FSRGStateValidation::kValid;
	}
}
