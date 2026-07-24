#include "OrderlyExit.h"
#include "detours/NukemDetours.h"

#include <Windows.h>

#include <array>
#include <cstring>
#include <string>

namespace
{
	constexpr char kMarker[] = "finalized";
	std::wstring g_markerPath;

	void Finalize() noexcept
	{
		const HANDLE file = CreateFileW(
			g_markerPath.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return;
		DWORD written = 0;
		(void)WriteFile(
			file, kMarker, static_cast<DWORD>(sizeof(kMarker)),
			&written, nullptr);
		(void)FlushFileBuffers(file);
		CloseHandle(file);
	}

	int RunChild(const wchar_t* a_markerPath)
	{
		using namespace cs::features::catalog::orderly_exit;
		g_markerPath = a_markerPath;
		(void)Detours::GetGlobalOptions();
		if (!Install(&Finalize) || !Install(&Finalize) || !IsInstalled())
			return 1;
		ExitProcess(0);
	}

	int RunParent()
	{
		std::array<wchar_t, MAX_PATH> executable{};
		const DWORD executableLength = GetModuleFileNameW(
			nullptr, executable.data(),
			static_cast<DWORD>(executable.size()));
		if (executableLength == 0
			|| executableLength == executable.size())
			return 2;

		std::array<wchar_t, MAX_PATH> temporaryDirectory{};
		if (GetTempPathW(
				static_cast<DWORD>(temporaryDirectory.size()),
				temporaryDirectory.data()) == 0)
			return 3;

		std::array<wchar_t, MAX_PATH> markerPath{};
		if (GetTempFileNameW(
				temporaryDirectory.data(), L"fcs", 0,
				markerPath.data()) == 0)
			return 4;
		DeleteFileW(markerPath.data());

		std::wstring command = L"\"";
		command += executable.data();
		command += L"\" --child \"";
		command += markerPath.data();
		command += L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(
				nullptr, command.data(), nullptr, nullptr, FALSE,
				0, nullptr, nullptr, &startup, &process)) {
			return 5;
		}

		const DWORD waitResult = WaitForSingleObject(
			process.hProcess, 10000);
		DWORD exitCode = 1;
		if (waitResult != WAIT_OBJECT_0) {
			TerminateProcess(process.hProcess, 6);
			(void)WaitForSingleObject(process.hProcess, 1000);
		} else {
			(void)GetExitCodeProcess(process.hProcess, &exitCode);
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		if (waitResult != WAIT_OBJECT_0 || exitCode != 0)
			return 6;

		const HANDLE marker = CreateFileW(
			markerPath.data(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (marker == INVALID_HANDLE_VALUE)
			return 7;
		std::array<char, sizeof(kMarker)> contents{};
		DWORD read = 0;
		const bool valid =
			ReadFile(
				marker, contents.data(),
				static_cast<DWORD>(contents.size()),
				&read, nullptr) != FALSE
			&& read == contents.size()
			&& std::memcmp(
				contents.data(), kMarker, contents.size()) == 0;
		CloseHandle(marker);
		DeleteFileW(markerPath.data());
		return valid ? 0 : 8;
	}
}

int wmain(int a_argc, wchar_t** a_argv)
{
	if (a_argc == 3 && std::wstring_view(a_argv[1]) == L"--child")
		return RunChild(a_argv[2]);
	return RunParent();
}
