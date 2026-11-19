#pragma once

#include <atomic>
#include <spdlog/spdlog.h>

namespace cs::render
{
	// Set by the frame-boundary hook on the first frame.
	inline std::atomic_bool frameStarted{ false };

	// Set by the dispatcher while it drains posted tasks.
	inline thread_local bool inFrameDrain = false;

	// Inside a drain, or before frame one when nothing can drain.
	[[nodiscard]] inline bool RunsInline() noexcept
	{
		return inFrameDrain || !frameStarted.load(std::memory_order_acquire);
	}

	// Structural mutators call this; startup runs before the first frame.
	inline void CheckFrameBoundary(const char* a_what) noexcept
	{
		if (RunsInline())
			return;
		spdlog::error("{} ran mid-frame; post it with PostToFrame", a_what);
	}
}
