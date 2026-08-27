#include "Env.h"

#include <Windows.h>

#include "ENB/ENBSeriesAPI.h"
#include "Log.h"

namespace cs::env
{
	namespace
	{
		auto* L = cs::log::Get("cs.env");
		bool g_detected = false;
		bool g_enbLoaded = false;
	}

	void DetectENB()
	{
		if (g_detected)
			return;
		g_detected = true;
		g_enbLoaded = ENB_API::RequestENBAPI() != nullptr;
		L->info("ENB {}", g_enbLoaded ? "detected" : "not detected");
	}

	bool IsENBLoaded() noexcept
	{
		return g_enbLoaded;
	}

	bool IsRenderDocActive() noexcept
	{
		return GetModuleHandleW(L"renderdoc.dll") != nullptr;
	}
}
