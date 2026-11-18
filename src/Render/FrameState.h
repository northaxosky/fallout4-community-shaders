#pragma once

#include <cstdint>

namespace cs::render
{
	// Why feature history must restart; producers are load screens and window resizes.
	enum class ResetReason : std::uint8_t
	{
		kLoad = 1 << 0,
		kResize = 1 << 1
	};

	// One coherent view of the current engine frame; read it, never re-query UI state.
	struct FrameState
	{
		std::uint32_t engineFrame = 0;  // BSGraphics frameCount: counts presents
		std::uint64_t worldFrame = 0;   // last world prepass; advances in CaptureWorldCamera
		bool worldRendered = false;     // a world prepass began in this engine frame
		bool shellOpen = false;         // the DearModdingUI shell is visible
		bool paused = false;            // UI reports the game paused, or no UI exists
		bool mainOrLoading = false;     // main menu or loading screen
		bool worldFrozen = false;       // game active in a menu or frozen time; motion is stale
		std::uint8_t resets = 0;        // ResetReason bits applied to this world frame

		[[nodiscard]] bool HasReset() const noexcept { return resets != 0; }
	};

	// Any thread, lock-free. Menu flags latch on the first call after frameCount changes.
	[[nodiscard]] FrameState GetFrameState() noexcept;

	// Any thread. Applied to the next world frame that begins outside main and loading menus.
	void RequestReset(ResetReason a_reason) noexcept;

	// Called once per engine frame by the world-camera capture; returns the new world frame.
	std::uint64_t BeginWorldFrame() noexcept;
}
