#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <expected>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cs::render
{
	class FrameDispatcher
	{
	public:
		static constexpr std::chrono::milliseconds kDefaultTimeout{ 5000 };
		static constexpr std::size_t kMaxQueued = 256;

		// Frame thread only. Returns one message per posted task that threw.
		[[nodiscard]] std::vector<std::string> Drain() noexcept;

		// A timeout cancels the task so it never runs late.
		template <class T>
		[[nodiscard]] std::expected<T, std::string> RunOnFrame(
			std::function<T()> a_task,
			std::chrono::milliseconds a_timeout = kDefaultTimeout)
		{
			struct Shared
			{
				enum Phase : int
				{
					kQueued,
					kRunning,
					kCancelled
				};
				std::atomic<int> phase{ kQueued };
				std::promise<std::expected<T, std::string>> result;
			};
			auto shared = std::make_shared<Shared>();
			auto future = shared->result.get_future();
			const bool posted = Post([shared, task = std::move(a_task)] {
				int expected = Shared::kQueued;
				if (!shared->phase.compare_exchange_strong(expected, Shared::kRunning))
					return;
				try {
					if constexpr (std::is_void_v<T>) {
						task();
						shared->result.set_value({});
					} else {
						shared->result.set_value(task());
					}
				} catch (const std::exception& error) {
					shared->result.set_value(std::unexpected(std::string(error.what())));
				} catch (...) {
					shared->result.set_value(std::unexpected(std::string("non-standard exception")));
				}
			});
			if (!posted)
				return std::unexpected("frame queue is full; frames are not being presented");
			if (future.wait_for(a_timeout) != std::future_status::ready) {
				int expected = Shared::kQueued;
				if (shared->phase.compare_exchange_strong(expected, Shared::kCancelled)) {
					return std::unexpected(std::format(
						"frame thread did not run within {}ms", a_timeout.count()));
				}
				// Already running: its side effects are real, so report its outcome.
			}
			return future.get();
		}

	private:
		[[nodiscard]] bool Post(std::function<void()> a_task);

		std::mutex _mutex;
		std::deque<std::function<void()>> _queue;
	};

	FrameDispatcher& GetFrameDispatcher() noexcept;
}
