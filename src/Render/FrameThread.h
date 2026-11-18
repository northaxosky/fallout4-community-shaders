#pragma once

#include <atomic>
#include <spdlog/spdlog.h>
#include <thread>

namespace cs::render
{
	// Main thread, recorded by the frame-boundary hook; unset before frame one.
	inline std::atomic<std::thread::id> frameThread{};

	// Set by the dispatcher while it drains posted tasks.
	inline thread_local bool inFrameDrain = false;

	inline void RecordFrameThread() noexcept
	{
		frameThread.store(std::this_thread::get_id(), std::memory_order_release);
	}

	// True while the dispatcher drains on this thread: the frame boundary itself.
	[[nodiscard]] inline bool AtFrameBoundary() noexcept
	{
		return inFrameDrain;
	}

	// Structural mutators call this; startup runs before the thread is recorded.
	inline void CheckFrameBoundary(const char* a_what) noexcept
	{
		if (inFrameDrain || frameThread.load(std::memory_order_acquire) == std::thread::id{})
			return;
		spdlog::error("{} ran mid-frame; post it with PostToFrame", a_what);
	}
}
