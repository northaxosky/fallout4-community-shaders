#pragma once

#include <cstdint>

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

	// FrameGeneration writes the number of frames presented per engine tick (1 = FG off /
	// gated, 2+ = active multiplier). PerformanceOverlay reads to multiply engine FPS.
	// Atomic so the writer (DX12 present thread) and reader (DX11 render thread) don't race.
	void SetDisplayedFrameMultiplier(int a_multiplier) noexcept;
	int  GetDisplayedFrameMultiplier() noexcept;

	// Approach B post-FG FPS source: backends accumulate actual frames-presented into a
	// monotonic counter (DLSS-G slDLSSGGetState::numFramesActuallyPresented, XeSS-FG
	// xefgSwapChainGetLastPresentStatus::framesPresented, FSR3 presentCallback hits, or +1
	// per engine tick when FG is off). PerformanceOverlay deltas this against wall time
	// for the displayed FPS readout instead of multiplying engine FPS by the multiplier.
	void     AddDisplayedFrames(uint32_t a_count) noexcept;
	uint64_t GetDisplayedFrameTotal() noexcept;
}
