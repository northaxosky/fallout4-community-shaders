#pragma once

#include <functional>

namespace cs::engine
{
	using RenderHookCallback        = std::function<void()>;

	// Late priority keeps additive lights after darkening.
	enum class HookPriority : int {
		Early   = -100,
		Default =    0,
		Late    =  100,
	};

	// Register only during Load or OnPostPostLoad.
	bool RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	bool RegisterPreDeferredComposite(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	bool RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority = HookPriority::Default);

	// Install the post-dirty-state deferred draw anchor.
	bool EnsurePreSunLightDrawInstalled();
	void RegisterPreSunLightDraw(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
}
