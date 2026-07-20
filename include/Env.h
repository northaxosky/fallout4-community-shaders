#pragma once

#include <cstdint>

namespace cs::env
{
	// One-shot detection. Call from F4SEPlugin_Load after the logger is attached.
	void DetectENB();

	// Cached result of DetectENB. False before DetectENB has run.
	bool IsENBLoaded() noexcept;

	// True when renderdoc.dll is loaded (feature runtime or RenderDoc launch); cheap live per-frame check, not cached.
	bool IsRenderDocActive() noexcept;

	// FrameGeneration writes frames presented per engine tick (1 = off/gated, 2+ = active); PerformanceOverlay multiplies engine FPS; atomic for DX12-present/DX11-render threads.
	void SetDisplayedFrameMultiplier(int a_multiplier) noexcept;
	int  GetDisplayedFrameMultiplier() noexcept;

	// Approach B post-FG FPS: backends add actual frames to a monotonic counter (DLSS-G slDLSSGGetState::numFramesActuallyPresented, XeSS-FG xefgSwapChainGetLastPresentStatus::framesPresented, FSR3 presentCallback hits, or +1/engine tick when FG is off); PerformanceOverlay deltas against wall time instead of multiplying engine FPS.
	void     AddDisplayedFrames(uint32_t a_count) noexcept;
	uint64_t GetDisplayedFrameTotal() noexcept;
}
