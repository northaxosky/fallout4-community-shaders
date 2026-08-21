#pragma once

#include <cstdint>

namespace cs::engine
{
	struct DeferredDrawAnchorDecision
	{
		bool dispatchInjections = false;
		bool dispatchLegacySunCallbacks = false;

		bool operator==(const DeferredDrawAnchorDecision&) const = default;
	};

	constexpr DeferredDrawAnchorDecision SelectDeferredDrawAnchorDecision(
		bool a_insideDeferredLights,
		bool a_insideDeferredComposite,
		std::uint32_t a_residualR9d) noexcept
	{
		if (a_insideDeferredComposite)
			return { true, false };

		const bool dispatchLights =
			a_insideDeferredLights && a_residualR9d == 2;
		return { dispatchLights, dispatchLights };
	}
}
