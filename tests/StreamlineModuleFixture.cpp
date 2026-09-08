#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

extern "C" __declspec(dllexport) int StreamlineModuleFixtureValue() noexcept
{
	return 0x534C;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD a_reason, LPVOID)
{
	if (a_reason == DLL_PROCESS_ATTACH)
		static_cast<void>(SetEnvironmentVariableW(
			L"FO4CS_STREAMLINE_MODULE_FIXTURE_ATTACHED", L"1"));
	return TRUE;
}
