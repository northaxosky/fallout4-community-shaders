#include "Render/FrameDispatcher.h"

#include <exception>

#include "Log.h"

namespace cs::render
{
	namespace
	{
		thread_local bool t_atBoundary = false;
	}

	bool AtFrameBoundary() noexcept
	{
		return t_atBoundary;
	}

	bool FrameDispatcher::Post(std::function<void()> a_task)
	{
		const std::scoped_lock lock{ _mutex };
		if (_queue.size() >= kMaxQueued)
			return false;
		_queue.push_back(std::move(a_task));
		return true;
	}

	std::vector<std::string> FrameDispatcher::Drain() noexcept
	{
		std::vector<std::string> failures;
		try {
			std::deque<std::function<void()>> batch;
			{
				const std::scoped_lock lock{ _mutex };
				batch.swap(_queue);
			}
			t_atBoundary = true;
			for (auto& task : batch) {
				try {
					task();
				} catch (const std::exception& error) {
					failures.emplace_back(error.what());
				} catch (...) {
					failures.emplace_back("non-standard exception");
				}
			}
			t_atBoundary = false;
		} catch (...) {
			t_atBoundary = false;
			failures.emplace_back("frame dispatcher drain failed");
		}
		return failures;
	}

	FrameDispatcher& GetFrameDispatcher() noexcept
	{
		static FrameDispatcher dispatcher;
		return dispatcher;
	}

	void PostToFrame(std::function<void()> a_task)
	{
		if (AtFrameBoundary()) {
			a_task();
			return;
		}
		if (!GetFrameDispatcher().Post(std::move(a_task)))
			cs::log::Get("cs.render.framedispatcher")->error("Frame queue is full; mutation dropped");
	}
}
