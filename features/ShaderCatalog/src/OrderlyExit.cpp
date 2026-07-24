#include "OrderlyExit.h"

#include <Windows.h>
#include <detours/Detours.h>

#include <atomic>
#include <mutex>

namespace cs::features::catalog::orderly_exit
{
	namespace
	{
		struct State
		{
			std::atomic<FinalizerCallback> callback{ nullptr };
			std::atomic<bool> installed{ false };
			std::atomic<unsigned> installState{ 0 };
			std::atomic<LONG> transactionResult{ NO_ERROR };
			std::mutex installMutex;
		};

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
			a_state.installState.store(3, std::memory_order_release);
			a_state.installState.notify_all();
		}

		struct ExitProcessHook
		{
			[[noreturn]] static void WINAPI thunk(UINT a_exitCode) noexcept
			{
				auto& state = GetState();
				unsigned installation =
					state.installState.load(std::memory_order_acquire);
				while (installation == 1) {
					state.installState.wait(
						installation, std::memory_order_acquire);
					installation =
						state.installState.load(std::memory_order_acquire);
				}
				if (const auto callback =
						state.callback.load(std::memory_order_acquire)) {
					callback();
				}
				target(a_exitCode);
				TerminateProcess(GetCurrentProcess(), a_exitCode);
				__assume(0);
			}

			static inline decltype(&::ExitProcess) target = &::ExitProcess;
		};
	}

	bool Install(FinalizerCallback a_callback) noexcept
	{
		if (!a_callback)
			return false;
		auto& state = GetState();
		std::scoped_lock lock(state.installMutex);
		if (state.installed.load(std::memory_order_acquire)) {
			return state.callback.load(std::memory_order_acquire)
				== a_callback;
		}

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

		result = DetourTransactionCommit();
		if (result != NO_ERROR) {
			CompleteFailedInstallation(state, result);
			return false;
		}

		state.callback.store(a_callback, std::memory_order_release);
		state.installed.store(true, std::memory_order_release);
		state.transactionResult.store(NO_ERROR, std::memory_order_relaxed);
		state.installState.store(2, std::memory_order_release);
		state.installState.notify_all();
		return true;
	}

	bool IsInstalled() noexcept
	{
		return GetState().installed.load(std::memory_order_acquire)
			&& ExitProcessHook::target != nullptr;
	}
}
