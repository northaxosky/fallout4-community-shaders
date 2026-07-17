#pragma once

#include <functional>

namespace cs::engine
{
	using RenderHookCallback        = std::function<void()>;
	using PostDynResViewportFGCb    = std::function<void(bool a_setting)>;

	// Earlier callbacks fire first; same priority keeps registration order. Use Default unless needed.
	// PostDeferredLightsImpl slots: Default = darken passes, Late = additive light after darkening.
	enum class HookPriority : int {
		Early   = -100,
		Default =    0,
		Late    =  100,
	};

	// Single-owner detour broker; first registration lazy-installs each anchor and dispatches by priority.
	// SAFETY: Register* is startup-only from Load/OnPostPostLoad before any render hook can fire.
	void RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
	void RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority = HookPriority::Default);

	// Post-upscale viewport-reset anchor; Imagespace must run before FG HUDLess captures its output.
	// Explicit named slots prevent third-party callbacks from landing between them by priority guess.
	void RegisterPostDynResViewport_Imagespace(RenderHookCallback callback);
	void RegisterPostDynResViewport_FGCapture(PostDynResViewportFGCb callback);

	// Fires right before each fullscreen DrawIndexed(6) draw inside DeferredLightsImpl, AFTER the
	// engine's SetDirtyStates flushes the pending PS SRVs and before the DrawIndexed. Lets a feature
	// bind a resource the engine leaves NULL (e.g. an SSS mask) for the directional sun light draw.
	// The anchor cannot tell the sun draw from other fullscreen deferred draws (ambient/IBL), so
	// callbacks MUST self-filter to their target (e.g. bind only when the target slot is still NULL).
	void RegisterPreSunLightDraw(RenderHookCallback callback, HookPriority priority = HookPriority::Default);
}
