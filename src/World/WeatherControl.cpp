#include "World/WeatherControl.h"

#include "Log.h"
#include "World/Weather.h"

#include "RE/S/Sky.h"
#include "RE/T/TESWeather.h"

#include <atomic>

namespace cs::engine
{
	namespace
	{
		auto* L = log::Get("cs.weather");

		std::atomic<RE::TESWeather*> g_lockedWeather{ nullptr };
		RE::TESRegion* g_regionBeforeLock = nullptr;
		std::atomic_bool g_hooksInstalled{ false };

		struct SetWeather_Hook
		{
			static void thunk(RE::Sky* a_sky, RE::TESWeather* a_weather, bool a_override, bool a_accelerate);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ForceWeather_Hook
		{
			static void thunk(RE::Sky* a_sky, RE::TESWeather* a_weather, bool a_override);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Our own calls bypass the lock thunks once they are installed.
		void SetWeatherOriginal(RE::Sky* a_sky, RE::TESWeather* a_weather, bool a_override, bool a_accelerate)
		{
			if (g_hooksInstalled.load(std::memory_order_acquire))
				SetWeather_Hook::func(a_sky, a_weather, a_override, a_accelerate);
			else
				a_sky->SetWeather(a_weather, a_override, a_accelerate);
		}

		void ForceWeatherOriginal(RE::Sky* a_sky, RE::TESWeather* a_weather, bool a_override)
		{
			if (g_hooksInstalled.load(std::memory_order_acquire))
				ForceWeather_Hook::func(a_sky, a_weather, a_override);
			else
				a_sky->ForceWeather(a_weather, a_override);
		}

		// Overrides expire on a stale stamp; mid-transition it drives the blend.
		void RenewOverrideExpiry(RE::Sky* a_sky)
		{
			if (!a_sky->lastWeather)
				a_sky->lastWeatherUpdate = a_sky->currentGameHour;
		}

		// Claims the override slot so nothing lerps away from the lock.
		void ReapplyLock(RE::Sky* a_sky, RE::TESWeather* a_weather)
		{
			if (!a_sky || !a_weather)
				return;
			if (a_sky->currentWeather == a_weather && !a_sky->lastWeather) {
				a_sky->overrideWeather = a_weather;
				RenewOverrideExpiry(a_sky);
			} else {
				ForceWeatherOriginal(a_sky, a_weather, true);
			}
		}

		void SetWeather_Hook::thunk(RE::Sky* a_sky, RE::TESWeather* a_weather, bool a_override, bool a_accelerate)
		{
			if (!a_sky)
				return;
			auto* locked = GetLockedWeather();
			if (!locked)
				func(a_sky, a_weather, a_override, a_accelerate);
			else if (a_weather == locked)
				func(a_sky, locked, true, a_accelerate);
			else if (a_sky->currentWeather != locked || a_sky->overrideWeather != locked)
				ReapplyLock(a_sky, locked);
		}

		void ForceWeather_Hook::thunk(RE::Sky* a_sky, RE::TESWeather* a_weather, bool a_override)
		{
			if (!a_sky)
				return;
			if (auto* locked = GetLockedWeather())
				ReapplyLock(a_sky, locked);
			else
				func(a_sky, a_weather, a_override);
		}
	}

	void InstallWeatherLockHooks()
	{
		stl::detour_thunk<SetWeather_Hook>(RE::ID::Sky::SetWeather);
		stl::detour_thunk<ForceWeather_Hook>(RE::ID::Sky::ForceWeather);
		if (SetWeather_Hook::func.address() == 0 || ForceWeather_Hook::func.address() == 0) {
			// MaintainWeatherLock still re-applies the lock without detours.
			L->warn("Weather lock detours failed; the lock may briefly flash before correcting");
			return;
		}
		g_hooksInstalled.store(true, std::memory_order_release);
	}

	bool AreWeatherLockHooksInstalled() noexcept
	{
		return g_hooksInstalled.load(std::memory_order_acquire);
	}

	void ApplyWeather(RE::TESWeather* a_weather, bool a_instant)
	{
		auto* sky = RE::Sky::GetSingleton();
		if (!sky || !a_weather)
			return;
		if (a_instant) {
			ForceWeatherOriginal(sky, a_weather, false);
		} else {
			RenewOverrideExpiry(sky);
			SetWeatherOriginal(sky, a_weather, true, false);
		}
		// Retarget the lock so upkeep enforces the new choice.
		if (GetLockedWeather())
			LockWeather(a_weather);
		L->info("Changed weather to {}", DescribeWeather(a_weather));
	}

	void ResetWeather()
	{
		if (auto* sky = RE::Sky::GetSingleton()) {
			sky->ResetWeather();
			L->info("Reset weather to default");
		}
	}

	void LockWeather(RE::TESWeather* a_weather)
	{
		if (!a_weather)
			return;
		if (auto* sky = RE::Sky::GetSingleton(); sky && !GetLockedWeather())
			g_regionBeforeLock = sky->currentRegion;
		g_lockedWeather.store(a_weather, std::memory_order_release);
		MaintainWeatherLock();
		L->info("Weather locked: {}", DescribeWeather(a_weather));
	}

	void UnlockWeather()
	{
		auto* locked = g_lockedWeather.exchange(nullptr, std::memory_order_acq_rel);
		if (!locked)
			return;
		if (auto* sky = RE::Sky::GetSingleton()) {
			// FO4's pending release flag also forces a new region weather.
			sky->flags.reset(RE::Sky::Flags::kReleaseWeatherOverride);
			sky->overrideWeather = nullptr;
			sky->defaultWeather = sky->currentWeather;
			sky->currentRegion = g_regionBeforeLock;
		}
		g_regionBeforeLock = nullptr;
		L->info("Weather unlocked: {}", DescribeWeather(locked));
	}

	RE::TESWeather* GetLockedWeather() noexcept
	{
		return g_lockedWeather.load(std::memory_order_acquire);
	}

	void MaintainWeatherLock()
	{
		auto* locked = GetLockedWeather();
		auto* sky = RE::Sky::GetSingleton();
		if (!locked || !sky)
			return;
		// The release lands on the next sky update; clear the flag first.
		const bool releasePending = sky->flags.any(RE::Sky::Flags::kReleaseWeatherOverride);
		if (!releasePending && sky->currentWeather == locked && sky->overrideWeather == locked)
			return;
		sky->flags.reset(RE::Sky::Flags::kReleaseWeatherOverride);
		ReapplyLock(sky, locked);
	}
}
