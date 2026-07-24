#pragma once

#include <atomic>

namespace cs::features::catalog::orderly_exit
{
	using FinalizerCallback = void (*)() noexcept;

	class FinalizerGate
	{
	public:
		bool TryBegin() noexcept
		{
			bool expected = false;
			return _started.compare_exchange_strong(
				expected, true, std::memory_order_acq_rel);
		}

		bool Started() const noexcept
		{
			return _started.load(std::memory_order_acquire);
		}

	private:
		std::atomic<bool> _started{ false };
	};

	bool Install(FinalizerCallback a_callback) noexcept;
	bool IsInstalled() noexcept;
}
