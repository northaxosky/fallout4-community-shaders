#pragma once

namespace cs::env
{
	// One-shot detection. Call from F4SEPlugin_Load after the logger is attached.
	void DetectENB();

	// Cached result of DetectENB. False before DetectENB has run.
	bool IsENBLoaded() noexcept;

	// True when renderdoc.dll is loaded in this process (typically because the RenderDoc
	// feature loaded the runtime or the user launched the game from RenderDoc itself).
	// Cheap LIVE check: callers may invoke per-frame; result is not cached.
	bool IsRenderDocActive() noexcept;
}
