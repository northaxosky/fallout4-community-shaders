#pragma once

#include <functional>

namespace cs::engine
{
	using RenderHookCallback        = std::function<void()>;
	using PostDynResViewportFGCb    = std::function<void(bool a_setting)>;

	// Cross-feature ordering for callbacks that share a deferred-renderer anchor. Earlier values
	// fire first; same-priority callbacks fire in registration order (stable sort). Use Default
	// unless an explicit slot below applies to your callback's semantics.
	//
	// Slot conventions (PostDeferredLightsImpl has the only multi-feature ordering constraint today):
	//   Default - multiplicative darken (e.g. vanilla AO darken, occlusion/shadow apply passes)
	//   Late    - additive contributions that must not be modulated by their own darken pass
	//             (e.g. indirect-bounce apply on top of the shaded base)
	enum class HookPriority : int {
		Early   = -100,
		Default =    0,
		Late    =  100,
	};

	// Single-owner detour broker for FO4 deferred-renderer anchors. Multiple features can register
	// post-callbacks at the same anchor; the broker installs one detour per anchor and dispatches the
	// list. First registration at an anchor lazy-installs the detour. Callbacks are ordered by
	// priority then by registration order.
	// SAFETY: Register* is startup-only and single-threaded from Feature::Load or OnPostPostLoad.
	// The vectors and install flags are deliberately unsynchronized; callbacks must register before
	// any render hook can fire.
	void RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority = HookPriority::Default);

	// Post-upscale call site (BSGraphics::RenderTargetManager::SetUseDynamicResolutionViewportAsDefaultViewport
	// re-enters with a_setting=false to restore the engine viewport after Upscaling writes the post-
	// upscale buffer). Imagespace (tonemap/LUT/bloom) and FrameGeneration HUDLess capture both need to
	// fire here. The broker enforces order: Imagespace first (so its post-FX lands in the buffer FG
	// captures), FG HUDLess second. The two-callbacks-by-explicit-name design avoids a third-party
	// feature accidentally landing between them by guessing the wrong priority.
	void RegisterPostDynResViewport_Imagespace(RenderHookCallback callback);
	void RegisterPostDynResViewport_FGCapture(PostDynResViewportFGCb callback);
}
