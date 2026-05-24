#include "Env.h"

#include <atomic>
#include <cstdint>

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
		std::atomic<int> g_displayedFrameMultiplier{ 1 };
		std::atomic<uint64_t> g_displayedFrameTotal{ 0 };
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

	void SetDisplayedFrameMultiplier(int a_multiplier) noexcept
	{
		if (a_multiplier < 1) a_multiplier = 1;
		if (a_multiplier > 8) a_multiplier = 8;
		g_displayedFrameMultiplier.store(a_multiplier, std::memory_order_relaxed);
	}

	int GetDisplayedFrameMultiplier() noexcept
	{
		return g_displayedFrameMultiplier.load(std::memory_order_relaxed);
	}

	void AddDisplayedFrames(uint32_t a_count) noexcept
	{
		if (a_count == 0) return;
		g_displayedFrameTotal.fetch_add(a_count, std::memory_order_relaxed);
	}

	uint64_t GetDisplayedFrameTotal() noexcept
	{
		return g_displayedFrameTotal.load(std::memory_order_relaxed);
	}
}
