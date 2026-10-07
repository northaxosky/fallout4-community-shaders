#pragma once

#include <functional>

namespace cs::engine
{
	bool EnsureDrawProfilingInstalled();
	using RenderHookCallback = std::function<void()>;

	// Late priority keeps additive lights after darkening.
	enum class HookPriority : int
	{
		Early = -100,
		Default = 0,
		Late = 100,
	};

	// Register only during Load or OnPostPostLoad.
	bool RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	bool RegisterPreDeferredPrePass(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	bool RegisterPreDeferredComposite(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	bool RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	// Right after the main sun's cascades render, before focus shadows reuse them.
	bool RegisterPostSunShadowRender(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	// After the stock precipitation occlusion pass; it runs in clear weather too.
	bool RegisterPostPrecipitationOcclusion(RenderHookCallback callback, HookPriority priority = HookPriority::Default);

	// After all sky and cloud draws in the main color target, before water and alpha.
	bool RegisterPostForwardSky(RenderHookCallback callback, HookPriority priority = HookPriority::Default);

	// Install the post-dirty-state deferred draw anchor.
	bool EnsureDeferredDrawAnchorInstalled();
	void RegisterPreFullscreenDeferredLightDraw(
		RenderHookCallback callback,
		HookPriority priority = HookPriority::Default);
}
