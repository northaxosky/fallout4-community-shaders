#pragma once

#include <cstdint>
#include <initializer_list>

namespace cs::render
{
	// Why feature history must restart.
	enum class ResetReason : std::uint8_t
	{
		kLoad = 1 << 0,        // loading screen closed
		kTeleport = 1 << 1,    // camera origin jumped within one frame
		kCameraState = 1 << 2  // discontinuous camera state change
	};

	using ResetMask = std::uint8_t;

	[[nodiscard]] constexpr ResetMask operator|(ResetReason a_lhs, ResetReason a_rhs) noexcept
	{
		return static_cast<ResetMask>(static_cast<ResetMask>(a_lhs) | static_cast<ResetMask>(a_rhs));
	}

	// Resets that break history anchored to world positions.
	inline constexpr ResetMask kWorldAnchoredResets = ResetReason::kLoad | ResetReason::kTeleport;

	// Dominant reason in a mask, for logs.
	[[nodiscard]] constexpr const char* ResetLabel(ResetMask a_resets) noexcept
	{
		if (a_resets & static_cast<ResetMask>(ResetReason::kLoad))
			return "loading_screen_closed";
		if (a_resets & static_cast<ResetMask>(ResetReason::kTeleport))
			return "camera_teleport";
		return "camera_state_change";
	}

	// One coherent view of the engine frame; read it, never re-query UI state.
	struct FrameState
	{
		std::uint32_t engineFrame = 0;  // BSGraphics frameCount: counts presents
		std::uint64_t worldFrame = 0;   // world prepasses begun so far
		bool shellOpen = false;
		bool paused = false;  // game paused, or no UI exists
		bool mainOrLoading = false;
		bool worldFrozen = false;  // active game in a menu or frozen time
		ResetMask resets = 0;      // ResetReason bits applied to this world frame

		[[nodiscard]] bool HasReset() const noexcept { return resets != 0; }
		[[nodiscard]] bool HasReset(ResetMask a_mask) const noexcept { return (resets & a_mask) != 0; }
	};

	// Menus whose open state the engine event stream tracks for us.
	enum class TrackedMenu : std::uint8_t
	{
		kMain = 1 << 0,
		kLoading = 1 << 1,
		kSleepWait = 1 << 2,
		kPipboy = 1 << 3
	};

	// Runs on the UI thread after the tracked state updated; ignores other menus.
	using MenuListener = void (*)(TrackedMenu a_menu, bool a_opening);

	// Must precede RegisterMenuEvents to see the first events. Idempotent.
	void AddMenuListener(MenuListener a_listener) noexcept;

	// Main thread, once the UI singleton exists. False when it is unavailable.
	bool RegisterMenuEvents();

	// Any thread. Everything reads open until events are registered.
	[[nodiscard]] bool IsAnyMenuOpen(std::initializer_list<TrackedMenu> a_menus) noexcept;

	// Any thread, lock-free. Menu flags latch on the first call each frame.
	[[nodiscard]] FrameState GetFrameState() noexcept;

	// Any thread. Applies to the next world frame outside main and loading menus.
	void RequestReset(ResetReason a_reason) noexcept;

	// Once per engine frame, after the capture queued that frame's resets.
	void BeginWorldFrame() noexcept;
}
