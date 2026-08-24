#pragma once

#include <functional>

namespace cs::engine
{
	using RenderHookCallback        = std::function<void()>;
	using PostDynResViewportFGCb    = std::function<void(bool a_setting)>;

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

	// Capture runs after the viewport swap, behind any pre-thunk.
	void RegisterPostDynResViewport_FGCapture(PostDynResViewportFGCb callback);

	// Claim viewport thunks before broker registration.
	void MarkPostDynResViewportPreThunkInstalled(std::string_view a_ownerLabel);

	// Install the post-dirty-state deferred draw anchor.
	bool EnsurePreSunLightDrawInstalled();
	void RegisterPreSunLightDraw(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
}
