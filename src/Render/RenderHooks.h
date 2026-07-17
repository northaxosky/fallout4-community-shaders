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

	// Fires before each fullscreen DrawIndexed(6) in DeferredLightsImpl, AFTER SetDirtyStates flushes pending PS SRVs but before DrawIndexed,
	// allowing a resource the engine leaves NULL (e.g. an SSS mask) to bind for the directional sun light draw. The anchor also sees ambient/IBL,
	// so callbacks MUST self-filter to their target, e.g. bind only when the target slot is still NULL.
	void RegisterPreSunLightDraw(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
}
