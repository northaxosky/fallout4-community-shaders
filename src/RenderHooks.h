#pragma once

#include <functional>

namespace cs::engine
{
	using RenderHookCallback        = std::function<void()>;
	using PostDynResViewportFGCb    = std::function<void(bool a_setting)>;

	// Single-owner detour broker for FO4 deferred-renderer anchors. Multiple features can register
	// post-callbacks at the same anchor; the broker installs one detour per anchor and dispatches the list.
	// First registration at an anchor lazy-installs the detour. Callbacks fire in registration order.
	void RegisterPostDeferredPrePass(RenderHookCallback callback);
	void RegisterPreDeferredLightsImpl(RenderHookCallback callback);
	void RegisterPostDeferredLightsImpl(RenderHookCallback callback);
	void RegisterPostDeferredComposite(RenderHookCallback callback);

	// Post-upscale call site (BSGraphics::RenderTargetManager::SetUseDynamicResolutionViewportAsDefaultViewport
	// re-enters with a_setting=false to restore the engine viewport after Upscaling writes the post-
	// upscale buffer). Imagespace (tonemap/LUT/bloom) and FrameGeneration HUDLess capture both need to
	// fire here. The broker enforces order: Imagespace first (so its post-FX lands in the buffer FG
	// captures), FG HUDLess second. Removes the previous ad-hoc two-features-patching-the-same-site
	// race that caused 60Hz judder depending on install order.
	void RegisterPostDynResViewport_Imagespace(RenderHookCallback callback);
	void RegisterPostDynResViewport_FGCapture(PostDynResViewportFGCb callback);
}
