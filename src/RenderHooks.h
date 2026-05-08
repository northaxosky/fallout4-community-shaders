#pragma once

#include <functional>

namespace cs::engine
{
	using RenderHookCallback = std::function<void()>;

	// Single-owner detour broker for FO4 deferred-renderer anchors. Multiple features can register
	// post-callbacks at the same anchor; the broker installs one detour per anchor and dispatches the list.
	// First registration at an anchor lazy-installs the detour. Callbacks fire in registration order.
	void RegisterPostDeferredPrePass(RenderHookCallback callback);
	void RegisterPostDeferredLightsImpl(RenderHookCallback callback);
	void RegisterPostDeferredComposite(RenderHookCallback callback);
}
