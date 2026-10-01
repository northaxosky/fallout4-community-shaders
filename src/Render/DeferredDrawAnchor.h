#pragma once

#include <cstdint>

namespace cs::engine
{
	struct DeferredDrawAnchorDecision
	{
		bool dispatchInjections = false;
		bool dispatchFullscreenLightCallbacks = false;

		bool operator==(const DeferredDrawAnchorDecision&) const = default;
	};

	constexpr DeferredDrawAnchorDecision SelectDeferredDrawAnchorDecision(
		bool a_insideDeferredLights,
		bool a_insideDeferredComposite,
		std::uint32_t a_residualR9d) noexcept
	{
		if (a_insideDeferredComposite)
			return { true, false };
		if (!a_insideDeferredLights)
			return { true, false };

		// Focused-volume shaders need bindings too; only fullscreen callbacks depend on r9d.
		return { true, a_residualR9d == 2 };
	}
}
