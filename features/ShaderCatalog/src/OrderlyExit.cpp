#include "OrderlyExit.h"

#include <Windows.h>
#include <detours/Detours.h>

#include <atomic>
#include <mutex>

namespace cs::features::catalog::orderly_exit
{
	namespace
	{
		enum class FinalizationState : unsigned
		{
			kNotStarted,
			kRunning,
			kCompleted
		};

		struct State
		{
			std::atomic<FinalizerCallback> callback{ nullptr };
			std::atomic<unsigned> installState{ 0 };
			std::atomic<unsigned> installSummary{ 0 };
			std::atomic<FinalizationState> finalizationState{
				FinalizationState::kNotStarted
			};
			std::atomic<LONG> transactionResult{ NO_ERROR };
#ifdef FO4CS_SHADER_CATALOG_TESTING
			std::atomic<ThunkEnteredCallbackForTesting>
				thunkEnteredCallback{ nullptr };
			std::atomic<InstallFailurePoint> installFailurePoint{
				InstallFailurePoint::kNone
			};
			std::atomic<void*> rtlTargetOverride{ nullptr };
#endif
			std::mutex installMutex;
		};

		thread_local bool g_finalizerThread = false;

		State& GetState()
		{
			static auto* state = new State();
			return *state;
		}

		void CompleteFailedInstallation(
			State& a_state,
			LONG a_result) noexcept
		{
			a_state.transactionResult.store(a_result, std::memory_order_relaxed);
			a_state.installSummary.store(0, std::memory_order_relaxed);
			a_state.installState.store(3, std::memory_order_release);
			a_state.installState.notify_all();
		}

		void WaitForInstallation(State& a_state) noexcept
		{
			unsigned installation =
				a_state.installState.load(std::memory_order_acquire);
			while (installation == 1) {
				a_state.installState.wait(
					installation, std::memory_order_acquire);
				installation =
					a_state.installState.load(std::memory_order_acquire);
			}
		}

		void NotifyThunkEntered(State& a_state) noexcept
		{
#ifdef FO4CS_SHADER_CATALOG_TESTING
			if (const auto callback =
					a_state.thunkEnteredCallback.load(
						std::memory_order_acquire)) {
				callback();
			}
#else
			(void)a_state;
#endif
		}

		void FinalizeOnce(State& a_state) noexcept
		{
			if (g_finalizerThread)
				return;

			auto finalization = FinalizationState::kNotStarted;
			if (a_state.finalizationState.compare_exchange_strong(
					finalization, FinalizationState::kRunning,
					std::memory_order_acq_rel,
					std::memory_order_acquire)) {
				g_finalizerThread = true;
				if (const auto callback =
						a_state.callback.load(
							std::memory_order_acquire)) {
					callback();
				}
				g_finalizerThread = false;
				a_state.finalizationState.store(
					FinalizationState::kCompleted,
					std::memory_order_release);
				a_state.finalizationState.notify_all();
				return;
			}

			while (finalization == FinalizationState::kRunning) {
				a_state.finalizationState.wait(
					FinalizationState::kRunning,
					std::memory_order_acquire);
				finalization = a_state.finalizationState.load(
					std::memory_order_acquire);
			}
		}

		struct ExitProcessHook
		{
			[[noreturn]] static void WINAPI thunk(UINT a_exitCode) noexcept
			{
				auto& state = GetState();
				WaitForInstallation(state);
				NotifyThunkEntered(state);
				FinalizeOnce(state);
				target(a_exitCode);
				TerminateProcess(GetCurrentProcess(), a_exitCode);
				__assume(0);
			}

			static inline decltype(&::ExitProcess) target = &::ExitProcess;
		};

		using RtlExitUserProcessFunction =
			void (NTAPI*)(LONG);

		struct RtlExitUserProcessHook
		{
			[[noreturn]] static void NTAPI thunk(LONG a_exitStatus) noexcept
			{
				auto& state = GetState();
				WaitForInstallation(state);
				NotifyThunkEntered(state);
				FinalizeOnce(state);
				target(a_exitStatus);
				TerminateProcess(
					GetCurrentProcess(),
					static_cast<UINT>(a_exitStatus));
				__assume(0);
			}

			static inline RtlExitUserProcessFunction target = nullptr;
		};

		enum InstallSummary : unsigned
		{
			kExitProcessCovered = 1u << 0,
			kRtlExitUserProcessCovered = 1u << 1,
			kTargetsAliased = 1u << 2,
			kReady = 1u << 3
		};

		bool ShouldFailBeforeSecondAttach(State& a_state) noexcept
		{
#ifdef FO4CS_SHADER_CATALOG_TESTING
			return a_state.installFailurePoint.load(
				std::memory_order_acquire)
				== InstallFailurePoint::kBeforeSecondAttach;
#else
			(void)a_state;
			return false;
#endif
		}
	}

	bool Install(FinalizerCallback a_callback) noexcept
	{
		if (!a_callback)
			return false;
		auto& state = GetState();
		std::scoped_lock lock(state.installMutex);
		if (state.installState.load(std::memory_order_acquire) == 2) {
			return state.callback.load(std::memory_order_acquire)
				== a_callback;
		}

		auto* ntdll = GetModuleHandleW(L"ntdll.dll");
		auto rtlTarget = reinterpret_cast<RtlExitUserProcessFunction>(
			ntdll
				? GetProcAddress(ntdll, "RtlExitUserProcess")
				: nullptr);
#ifdef FO4CS_SHADER_CATALOG_TESTING
		if (const auto overrideTarget =
				state.rtlTargetOverride.load(
					std::memory_order_acquire)) {
			rtlTarget = reinterpret_cast<
				RtlExitUserProcessFunction>(overrideTarget);
		}
#endif
		if (!rtlTarget) {
			CompleteFailedInstallation(
				state, ERROR_PROC_NOT_FOUND);
			return false;
		}
		RtlExitUserProcessHook::target = rtlTarget;

		const auto normalizedExit = DetourCodeFromPointer(
			reinterpret_cast<PVOID>(ExitProcessHook::target),
			nullptr);
		const auto normalizedRtl = DetourCodeFromPointer(
			reinterpret_cast<PVOID>(
				RtlExitUserProcessHook::target),
			nullptr);
		if (!normalizedExit || !normalizedRtl) {
			CompleteFailedInstallation(
				state, ERROR_INVALID_ADDRESS);
			return false;
		}
		const bool targetsAliased =
			normalizedExit == normalizedRtl;

		state.installState.store(1, std::memory_order_release);
		LONG result = DetourTransactionBegin();
		if (result != NO_ERROR) {
			CompleteFailedInstallation(state, result);
			return false;
		}

		result = DetourUpdateThread(GetCurrentThread());
		if (result != NO_ERROR) {
			const LONG abortResult = DetourTransactionAbort();
			CompleteFailedInstallation(
				state, abortResult == NO_ERROR ? result : abortResult);
			return false;
		}

		result = DetourAttach(
			reinterpret_cast<PVOID*>(&ExitProcessHook::target),
			reinterpret_cast<PVOID>(&ExitProcessHook::thunk));
		if (result != NO_ERROR) {
			const LONG abortResult = DetourTransactionAbort();
			CompleteFailedInstallation(
				state, abortResult == NO_ERROR ? result : abortResult);
			return false;
		}
		if (!targetsAliased) {
			if (ShouldFailBeforeSecondAttach(state)) {
				result = ERROR_GEN_FAILURE;
			} else {
				result = DetourAttach(
					reinterpret_cast<PVOID*>(
						&RtlExitUserProcessHook::target),
					reinterpret_cast<PVOID>(
						&RtlExitUserProcessHook::thunk));
			}
			if (result != NO_ERROR) {
				const LONG abortResult = DetourTransactionAbort();
				CompleteFailedInstallation(
					state,
					abortResult == NO_ERROR
						? result
						: abortResult);
				return false;
			}
		}

		result = DetourTransactionCommit();
		if (result != NO_ERROR) {
			CompleteFailedInstallation(state, result);
			return false;
		}

		state.callback.store(a_callback, std::memory_order_release);
		state.transactionResult.store(NO_ERROR, std::memory_order_relaxed);
		state.installSummary.store(
			kExitProcessCovered
				| kRtlExitUserProcessCovered
				| (targetsAliased ? kTargetsAliased : 0u)
				| kReady,
			std::memory_order_release);
		state.installState.store(2, std::memory_order_release);
		state.installState.notify_all();
		return true;
	}

	bool IsInstalled() noexcept
	{
		return GetInstallStatus().ready;
	}

	InstallStatus GetInstallStatus() noexcept
	{
		auto& state = GetState();
		const auto installation =
			state.installState.load(std::memory_order_acquire);
		const auto summary =
			state.installSummary.load(std::memory_order_acquire);
		return {
			.ready = installation == 2
				&& (summary & kReady) != 0
				&& (summary & kExitProcessCovered) != 0
				&& (summary & kRtlExitUserProcessCovered) != 0,
			.exitProcessCovered =
				(summary & kExitProcessCovered) != 0,
			.rtlExitUserProcessCovered =
				(summary & kRtlExitUserProcessCovered) != 0,
			.targetsAliased =
				(summary & kTargetsAliased) != 0,
			.transactionResult =
				state.transactionResult.load(
					std::memory_order_relaxed)
		};
	}

#ifdef FO4CS_SHADER_CATALOG_TESTING
	void SetThunkEnteredCallbackForTesting(
		ThunkEnteredCallbackForTesting a_callback) noexcept
	{
		GetState().thunkEnteredCallback.store(
			a_callback, std::memory_order_release);
	}

	void SetInstallFailurePointForTesting(
		InstallFailurePoint a_point) noexcept
	{
		GetState().installFailurePoint.store(
			a_point, std::memory_order_release);
	}

	void SetRtlExitUserProcessTargetForTesting(void* a_target) noexcept
	{
		GetState().rtlTargetOverride.store(
			a_target, std::memory_order_release);
	}
#endif
}
