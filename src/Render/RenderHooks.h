#pragma once

#include <functional>

namespace cs::engine
{
	using RenderHookCallback        = std::function<void()>;
	using PostDynResViewportFGCb    = std::function<void(bool a_setting)>;

	// Callbacks fire by priority then registration order; use Default, with PostDeferredLightsImpl additive lights using Late after darkening.
	enum class HookPriority : int {
		Early   = -100,
		Default =    0,
		Late    =  100,
	};

	// Single-owner broker lazy-installs each anchor on first registration and dispatches by priority; SAFETY: Register* only from startup Load/OnPostPostLoad.
	void RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority = HookPriority::Default);

	// Named post-upscale viewport-reset slots keep Imagespace before FG HUDLess without priority interleaving.
	void RegisterPostDynResViewport_Imagespace(RenderHookCallback callback);
	void RegisterPostDynResViewport_FGCapture(PostDynResViewportFGCb callback);

	// Fires before each fullscreen DrawIndexed(6) in DeferredLightsImpl, after SetDirtyStates flushes PS SRVs and before DrawIndexed; bind only engine-left-NULL resources (e.g. SSS mask) and self-filter ambient/IBL.
	void RegisterPreSunLightDraw(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
}
