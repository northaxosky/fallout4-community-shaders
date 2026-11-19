#pragma once

#include <atomic>
#include <cstdint>

namespace cs::render::temporal
{
	// Consecutive failed world frames before a feature latches off.
	inline constexpr std::uint32_t kSuperResolutionFailureLimit = 30;
	inline constexpr std::uint32_t kFrameGenerationFailureLimit = 5;

	// A failed frame is skipped; a run of them latches the feature off until Retry.
	class FailurePolicy
	{
	public:
		explicit FailurePolicy(std::uint32_t a_limit) noexcept : _limit(a_limit) {}

		void Succeeded() noexcept { _consecutive.store(0, std::memory_order_relaxed); }

		// True on the failure that latches.
		[[nodiscard]] bool Failed() noexcept
		{
			if (_consecutive.fetch_add(1, std::memory_order_relaxed) + 1 < _limit) {
				return false;
			}
			return Latch();
		}

		// For a failure that no later frame can cure; true if it newly latched.
		bool Latch() noexcept { return !_latched.exchange(true, std::memory_order_acq_rel); }

		[[nodiscard]] bool Latched() const noexcept { return _latched.load(std::memory_order_acquire); }

		// A settings change or resize gives the feature another chance.
		void Retry() noexcept
		{
			_consecutive.store(0, std::memory_order_relaxed);
			_latched.store(false, std::memory_order_release);
		}

	private:
		std::uint32_t _limit;
		std::atomic_uint32_t _consecutive{ 0 };
		std::atomic_bool _latched{ false };
	};

	// Dev fault injection: an armed DevFlags flag fails the next a_count calls, then disarms.
	class InjectedFaults
	{
	public:
		void Poll(std::atomic_bool& a_flag, std::uint32_t a_count) noexcept
		{
			if (a_flag.exchange(false, std::memory_order_relaxed)) {
				_remaining.store(a_count, std::memory_order_relaxed);
			}
		}

		[[nodiscard]] bool Take() noexcept
		{
			auto left = _remaining.load(std::memory_order_relaxed);
			while (left && !_remaining.compare_exchange_weak(left, left - 1, std::memory_order_relaxed)) {
			}
			return left != 0;
		}

	private:
		std::atomic_uint32_t _remaining{ 0 };
	};
}  // namespace cs::render::temporal
