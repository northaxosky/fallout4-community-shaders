#pragma once

#include <cstdint>

namespace cs::render
{
	// Why feature history must restart; the producer is the load screen.
	enum class ResetReason : std::uint8_t
	{
		kLoad = 1 << 0
	};

	// One coherent view of the engine frame; read it, never re-query UI state.
	struct FrameState
	{
		std::uint32_t engineFrame = 0;  // BSGraphics frameCount: counts presents
		std::uint64_t worldFrame = 0;   // world prepasses begun so far
		bool shellOpen = false;
		bool paused = false;  // game paused, or no UI exists
		bool mainOrLoading = false;
		bool worldFrozen = false;  // active game in a menu or frozen time
		std::uint8_t resets = 0;   // ResetReason bits applied to this world frame

		[[nodiscard]] bool HasReset() const noexcept { return resets != 0; }
	};

	// Any thread, lock-free. Menu flags latch on the first call each frame.
	[[nodiscard]] FrameState GetFrameState() noexcept;

	// Any thread. Applies to the next world frame outside main and loading menus.
	void RequestReset(ResetReason a_reason) noexcept;

	// Called once per engine frame by the world-camera capture.
	void BeginWorldFrame() noexcept;
}
