#pragma once

#include <atomic>
#include <thread>

namespace cs::render
{
	// The main thread, recorded by the frame-boundary hook; unset until the first frame.
	inline std::atomic<std::thread::id> frameThread{};

	inline void RecordFrameThread() noexcept
	{
		frameThread.store(std::this_thread::get_id(), std::memory_order_release);
	}

	// True on the main thread, or before the first frame (startup callers).
	[[nodiscard]] inline bool OnFrameThread() noexcept
	{
		const auto thread = frameThread.load(std::memory_order_acquire);
		return thread == std::thread::id{} || thread == std::this_thread::get_id();
	}
}
