#pragma once

namespace cs::env
{
	// One-shot detection. Call from F4SEPlugin_Load after the logger is attached.
	void DetectENB();

	// Cached result of DetectENB. False before DetectENB has run.
	bool IsENBLoaded() noexcept;

	// True when renderdoc.dll is loaded (feature runtime or RenderDoc launch); cheap live per-frame check, not cached.
	bool IsRenderDocActive() noexcept;
}
