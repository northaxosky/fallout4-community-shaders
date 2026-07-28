#pragma once

#include <atomic>

namespace cs::features::catalog::orderly_exit
{
	using FinalizerCallback = void (*)() noexcept;

	struct InstallStatus
	{
		bool ready = false;
		bool exitProcessCovered = false;
		bool rtlExitUserProcessCovered = false;
		bool targetsAliased = false;
		long transactionResult = 0;
	};

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
	InstallStatus GetInstallStatus() noexcept;
#ifdef FO4CS_SHADER_CATALOG_TESTING
	enum class InstallFailurePoint : unsigned
	{
		kNone,
		kBeforeSecondAttach
	};

	using ThunkEnteredCallbackForTesting = void (*)() noexcept;
	void SetThunkEnteredCallbackForTesting(
		ThunkEnteredCallbackForTesting a_callback) noexcept;
	void SetInstallFailurePointForTesting(
		InstallFailurePoint a_point) noexcept;
	void SetRtlExitUserProcessTargetForTesting(void* a_target) noexcept;
#endif
}
