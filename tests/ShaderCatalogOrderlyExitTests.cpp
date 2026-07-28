#include "OrderlyExit.h"
#include "detours/NukemDetours.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace
{
	constexpr std::uint32_t kMarkerMagic = 0x58453446;
	constexpr UINT kReentrantExitCode = 23;

	enum class ChildMode
	{
		kConcurrent,
		kReentrant,
		kDirectRtl,
		kAlias,
		kPartial,
		kHandledFailure
	};

	struct Marker
	{
		std::uint32_t magic = kMarkerMagic;
		std::uint32_t callers = 0;
		std::uint32_t thunkCallers = 0;
		std::uint32_t finalizers = 0;
		std::uint32_t completed = 0;
		std::uint32_t installReady = 0;
		std::uint32_t exitProcessCovered = 0;
		std::uint32_t rtlExitUserProcessCovered = 0;
		std::uint32_t targetsAliased = 0;
		std::int32_t transactionResult = 0;
	};

	ChildMode g_mode = ChildMode::kConcurrent;
	std::wstring g_completionPath;
	std::wstring g_enteredPath;
	HANDLE g_callersReady = nullptr;
	HANDLE g_thunkCallersReady = nullptr;
	std::atomic<std::uint32_t> g_callers{ 0 };
	std::atomic<std::uint32_t> g_thunkCallers{ 0 };
	std::atomic<std::uint32_t> g_finalizers{ 0 };

	using RtlExitUserProcessFunction = void (NTAPI*)(LONG);

	RtlExitUserProcessFunction ResolveRtlExitUserProcess() noexcept
	{
		const auto ntdll = GetModuleHandleW(L"ntdll.dll");
		return reinterpret_cast<RtlExitUserProcessFunction>(
			ntdll
				? GetProcAddress(ntdll, "RtlExitUserProcess")
				: nullptr);
	}

	void AddInstallStatus(Marker& a_marker) noexcept
	{
		const auto status =
			cs::features::catalog::orderly_exit::GetInstallStatus();
		a_marker.installReady = status.ready;
		a_marker.exitProcessCovered = status.exitProcessCovered;
		a_marker.rtlExitUserProcessCovered =
			status.rtlExitUserProcessCovered;
		a_marker.targetsAliased = status.targetsAliased;
		a_marker.transactionResult = status.transactionResult;
	}

	bool WriteMarker(
		const std::wstring& a_path,
		const Marker& a_marker) noexcept
	{
		const HANDLE file = CreateFileW(
			a_path.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;
		DWORD written = 0;
		const bool success =
			WriteFile(
				file, &a_marker, sizeof(a_marker),
				&written, nullptr) != FALSE
			&& written == sizeof(a_marker)
			&& FlushFileBuffers(file) != FALSE;
		CloseHandle(file);
		return success;
	}

	bool ReadMarker(
		const std::wstring& a_path,
		Marker& a_marker) noexcept
	{
		const HANDLE file = CreateFileW(
			a_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;
		DWORD read = 0;
		const bool success =
			ReadFile(
				file, &a_marker, sizeof(a_marker),
				&read, nullptr) != FALSE
			&& read == sizeof(a_marker);
		CloseHandle(file);
		return success;
	}

	void ThunkEntered() noexcept
	{
		const auto callers =
			g_thunkCallers.fetch_add(1, std::memory_order_acq_rel) + 1;
		if (g_mode == ChildMode::kPartial) {
			Marker entered;
			entered.thunkCallers = callers;
			(void)WriteMarker(g_completionPath, entered);
			return;
		}
		if (callers == 2)
			SetEvent(g_thunkCallersReady);
	}

	void Finalize() noexcept
	{
		const auto finalizers =
			g_finalizers.fetch_add(1, std::memory_order_acq_rel) + 1;
		if (g_mode == ChildMode::kReentrant) {
			Marker entered;
			entered.finalizers = finalizers;
			AddInstallStatus(entered);
			(void)WriteMarker(g_enteredPath, entered);
			ExitProcess(kReentrantExitCode);
		}

		if (g_mode == ChildMode::kConcurrent)
			(void)WaitForSingleObject(g_thunkCallersReady, INFINITE);
		Marker completed;
		completed.callers =
			g_callers.load(std::memory_order_acquire);
		completed.thunkCallers =
			g_thunkCallers.load(std::memory_order_acquire);
		completed.finalizers =
			g_finalizers.load(std::memory_order_acquire);
		completed.completed =
			g_mode == ChildMode::kHandledFailure ? 0 : 1;
		AddInstallStatus(completed);
		(void)WriteMarker(g_completionPath, completed);
	}

	DWORD WINAPI ExitCaller(void* a_native)
	{
		const auto callers =
			g_callers.fetch_add(1, std::memory_order_acq_rel) + 1;
		if (callers == 2)
			SetEvent(g_callersReady);
		(void)WaitForSingleObject(g_callersReady, INFINITE);
		if (a_native) {
			if (const auto rtl = ResolveRtlExitUserProcess())
				rtl(0);
			TerminateProcess(GetCurrentProcess(), 0xf1);
		}
		ExitProcess(0);
	}

	int InstallFinalizer(
		cs::features::catalog::orderly_exit::FinalizerCallback
			a_callback = &Finalize)
	{
		using namespace cs::features::catalog::orderly_exit;
		(void)Detours::GetGlobalOptions();
		return Install(a_callback) && Install(a_callback) && IsInstalled()
			? 0
			: 1;
	}

	int RunConcurrentChild(const wchar_t* a_basePath)
	{
		using namespace cs::features::catalog::orderly_exit;
		g_mode = ChildMode::kConcurrent;
		g_completionPath = std::wstring(a_basePath) + L".completed";
		g_callersReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		g_thunkCallersReady =
			CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!g_callersReady || !g_thunkCallersReady)
			return 2;

		HANDLE first = CreateThread(
			nullptr, 0, &ExitCaller, nullptr, CREATE_SUSPENDED, nullptr);
		HANDLE second = CreateThread(
			nullptr, 0, &ExitCaller,
			reinterpret_cast<void*>(1), CREATE_SUSPENDED, nullptr);
		if (!first || !second) {
			if (first)
				TerminateThread(first, 2);
			if (second)
				TerminateThread(second, 2);
			if (first)
				CloseHandle(first);
			if (second)
				CloseHandle(second);
			return 3;
		}

		SetThunkEnteredCallbackForTesting(&ThunkEntered);
		if (InstallFinalizer() != 0)
			return 4;
		if (ResumeThread(first) == static_cast<DWORD>(-1)
			|| ResumeThread(second) == static_cast<DWORD>(-1)) {
			TerminateProcess(GetCurrentProcess(), 5);
		}
		CloseHandle(first);
		CloseHandle(second);

		const HANDLE hold = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!hold)
			TerminateProcess(GetCurrentProcess(), 6);
		(void)WaitForSingleObject(hold, INFINITE);
		return 7;
	}

	int RunReentrantChild(const wchar_t* a_basePath)
	{
		g_mode = ChildMode::kReentrant;
		g_enteredPath = std::wstring(a_basePath) + L".entered";
		g_completionPath = std::wstring(a_basePath) + L".completed";
		if (InstallFinalizer() != 0)
			return 8;
		ExitProcess(11);
	}

	int RunDirectRtlChild(const wchar_t* a_basePath)
	{
		g_mode = ChildMode::kDirectRtl;
		g_completionPath = std::wstring(a_basePath) + L".completed";
		if (InstallFinalizer() != 0)
			return 14;
		const auto rtl = ResolveRtlExitUserProcess();
		if (!rtl)
			return 15;
		rtl(17);
		return 15;
	}

	int RunAliasChild(const wchar_t* a_basePath)
	{
		using namespace cs::features::catalog::orderly_exit;
		g_mode = ChildMode::kAlias;
		g_completionPath = std::wstring(a_basePath) + L".completed";
		SetRtlExitUserProcessTargetForTesting(
			reinterpret_cast<void*>(&ExitProcess));
		if (InstallFinalizer() != 0)
			return 16;
		const auto status = GetInstallStatus();
		if (!status.targetsAliased)
			return 17;
		ExitProcess(19);
	}

	int RunPartialFailureChild(const wchar_t* a_basePath)
	{
		using namespace cs::features::catalog::orderly_exit;
		g_mode = ChildMode::kPartial;
		g_enteredPath = std::wstring(a_basePath) + L".entered";
		g_completionPath = std::wstring(a_basePath) + L".completed";
		SetThunkEnteredCallbackForTesting(&ThunkEntered);
		SetInstallFailurePointForTesting(
			InstallFailurePoint::kBeforeSecondAttach);
		const bool installed = Install(&Finalize);
		Marker status;
		AddInstallStatus(status);
		if (installed || status.installReady
			|| status.transactionResult == NO_ERROR)
			return 18;
		if (!WriteMarker(g_enteredPath, status))
			return 19;
		ExitProcess(29);
	}

	int RunHandledFailureChild(const wchar_t* a_basePath)
	{
		g_mode = ChildMode::kHandledFailure;
		g_completionPath = std::wstring(a_basePath) + L".completed";
		if (InstallFinalizer() != 0)
			return 20;
		const auto rtl = ResolveRtlExitUserProcess();
		if (!rtl)
			return 21;
		rtl(31);
		return 21;
	}

	std::wstring TemporaryBase()
	{
		std::array<wchar_t, MAX_PATH> directory{};
		if (GetTempPathW(
				static_cast<DWORD>(directory.size()),
				directory.data()) == 0)
			return {};
		std::array<wchar_t, MAX_PATH> path{};
		if (GetTempFileNameW(
				directory.data(), L"fcs", 0, path.data()) == 0)
			return {};
		return path.data();
	}

	void CleanupTemporaryBase(const std::wstring& a_basePath)
	{
		if (a_basePath.empty())
			return;
		DeleteFileW((a_basePath + L".completed").c_str());
		DeleteFileW((a_basePath + L".entered").c_str());
		DeleteFileW(a_basePath.c_str());
	}

	bool RunChildProcess(
		std::wstring_view a_mode,
		const std::wstring& a_basePath,
		DWORD a_expectedExitCode)
	{
		std::array<wchar_t, MAX_PATH> executable{};
		const DWORD length = GetModuleFileNameW(
			nullptr, executable.data(),
			static_cast<DWORD>(executable.size()));
		if (length == 0 || length == executable.size())
			return false;

		std::wstring command = L"\"";
		command += executable.data();
		command += L"\" ";
		command += a_mode;
		command += L" \"";
		command += a_basePath;
		command += L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(
				nullptr, command.data(), nullptr, nullptr, FALSE,
				0, nullptr, nullptr, &startup, &process))
			return false;

		const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
		DWORD exitCode = 0;
		if (wait != WAIT_OBJECT_0) {
			TerminateProcess(process.hProcess, 0xffff);
			(void)WaitForSingleObject(process.hProcess, 1000);
		} else {
			(void)GetExitCodeProcess(process.hProcess, &exitCode);
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return wait == WAIT_OBJECT_0 && exitCode == a_expectedExitCode;
	}

	int RunParent()
	{
		const auto concurrentBase = TemporaryBase();
		if (concurrentBase.empty()
			|| !RunChildProcess(
				L"--concurrent", concurrentBase, 0)) {
			CleanupTemporaryBase(concurrentBase);
			return 10;
		}
		Marker completed;
		const auto concurrentCompletion =
			concurrentBase + L".completed";
		const bool concurrentValid =
			ReadMarker(concurrentCompletion, completed)
			&& completed.magic == kMarkerMagic
			&& completed.callers == 2
			&& completed.thunkCallers == 2
			&& completed.finalizers == 1
			&& completed.completed == 1;
		CleanupTemporaryBase(concurrentBase);
		if (!concurrentValid)
			return 11;

		const auto reentrantBase = TemporaryBase();
		if (reentrantBase.empty()
			|| !RunChildProcess(
				L"--reentrant", reentrantBase,
				kReentrantExitCode)) {
			CleanupTemporaryBase(reentrantBase);
			return 12;
		}
		Marker entered;
		const auto enteredPath = reentrantBase + L".entered";
		const auto completionPath =
			reentrantBase + L".completed";
		const bool reentrantValid =
			ReadMarker(enteredPath, entered)
			&& entered.magic == kMarkerMagic
			&& entered.finalizers == 1
			&& entered.completed == 0
			&& GetFileAttributesW(completionPath.c_str())
				== INVALID_FILE_ATTRIBUTES;
		CleanupTemporaryBase(reentrantBase);
		if (!reentrantValid)
			return 13;

		const auto directBase = TemporaryBase();
		const bool directValid =
			!directBase.empty()
			&& RunChildProcess(L"--direct-rtl", directBase, 17)
			&& ReadMarker(
				directBase + L".completed", completed)
			&& completed.finalizers == 1
			&& completed.completed == 1
			&& completed.installReady == 1
			&& completed.exitProcessCovered == 1
			&& completed.rtlExitUserProcessCovered == 1;
		CleanupTemporaryBase(directBase);
		if (!directValid)
			return 22;

		const auto aliasBase = TemporaryBase();
		const bool aliasValid =
			!aliasBase.empty()
			&& RunChildProcess(L"--alias", aliasBase, 19)
			&& ReadMarker(aliasBase + L".completed", completed)
			&& completed.finalizers == 1
			&& completed.targetsAliased == 1
			&& completed.installReady == 1;
		CleanupTemporaryBase(aliasBase);
		if (!aliasValid)
			return 23;

		const auto partialBase = TemporaryBase();
		Marker partial;
		const bool partialValid =
			!partialBase.empty()
			&& RunChildProcess(L"--partial", partialBase, 29)
			&& ReadMarker(partialBase + L".entered", partial)
			&& partial.installReady == 0
			&& partial.exitProcessCovered == 0
			&& partial.rtlExitUserProcessCovered == 0
			&& partial.transactionResult != NO_ERROR
			&& GetFileAttributesW(
				(partialBase + L".completed").c_str())
				== INVALID_FILE_ATTRIBUTES;
		CleanupTemporaryBase(partialBase);
		if (!partialValid)
			return 24;

		const auto handledBase = TemporaryBase();
		const bool handledValid =
			!handledBase.empty()
			&& RunChildProcess(
				L"--handled-failure", handledBase, 31)
			&& ReadMarker(
				handledBase + L".completed", completed)
			&& completed.finalizers == 1
			&& completed.completed == 0
			&& completed.installReady == 1;
		CleanupTemporaryBase(handledBase);
		return handledValid ? 0 : 25;
	}
}

int wmain(int a_argc, wchar_t** a_argv)
{
	if (a_argc == 3) {
		const std::wstring_view mode = a_argv[1];
		if (mode == L"--concurrent")
			return RunConcurrentChild(a_argv[2]);
		if (mode == L"--reentrant")
			return RunReentrantChild(a_argv[2]);
		if (mode == L"--direct-rtl")
			return RunDirectRtlChild(a_argv[2]);
		if (mode == L"--alias")
			return RunAliasChild(a_argv[2]);
		if (mode == L"--partial")
			return RunPartialFailureChild(a_argv[2]);
		if (mode == L"--handled-failure")
			return RunHandledFailureChild(a_argv[2]);
	}
	return RunParent();
}
