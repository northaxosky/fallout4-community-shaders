#include "Imagespace.h"
#include "ImagespaceInternal.h"

#include <DirectXTex.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <DirectXMath.h>

#include "Render/ComputeScope.h"
#include "Render/RendererContext.h"
#include "Render/Engine.h"
#include "Utils/CSUtil.h"
#include "Env.h"
#include "ImagespaceConfigIO.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Settings/PresetManager.h"
#include "Render/RenderHooks.h"
#include "Settings/SettingsOverrideManager.h"
#include "World/Sky.h"
#include "World/Weather.h"
#include "WeatherProfiles.h"


namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	namespace detail
	{
		// Imagespace shared state is single-threaded by construction: RunFrame reads it mid-frame from
		// the PostDynResViewport render hook, and DrawSettings / preset commits write it end-frame from
		// the Present hook - both on the render thread, never overlapping, so access is lock-free. This
		// tripwire catches any future change that breaks that invariant (e.g. an off-thread menu), which
		// would silently turn the lock-free access into a real data race.
		void AssertRenderThread(const char* a_where)
		{
			static std::atomic<std::thread::id> established{};
			const std::thread::id self = std::this_thread::get_id();
			std::thread::id none{};
			if (established.compare_exchange_strong(none, self, std::memory_order_relaxed))
				return;
			if (established.load(std::memory_order_relaxed) != self) {
				static std::atomic_bool logged{ false };
				if (!logged.exchange(true))
					L->error("Imagespace same-thread invariant broken at {}: shared state touched off the render thread", a_where);
				assert(false && "Imagespace shared state must be accessed only from the render thread");
			}
		}
	}

	// Engine DOF vfuncs return false while ours runs; forceWithENB lets users stack with ENB.
	struct ImageSpaceEffectDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			const auto& s = Imagespace::GetSingleton()->settings;
			const bool enbYield = cs::env::IsENBLoaded() && !s.forceWithENB;
			return (!s.dofEnable || enbYield) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ImageSpaceEffectBokehDepthOfField_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			const auto& s = Imagespace::GetSingleton()->settings;
			const bool enbYield = cs::env::IsENBLoaded() && !s.forceWithENB;
			return (!s.dofEnable || enbYield) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	struct ImageSpaceEffectFullScreenBlur_IsActive
	{
		static bool thunk(RE::ImageSpaceEffect* This)
		{
			const auto& s = Imagespace::GetSingleton()->settings;
			const bool enbYield = cs::env::IsENBLoaded() && !s.forceWithENB;
			return (!s.dofEnable || enbYield) && func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	Imagespace* Imagespace::GetSingleton()
	{
		static Imagespace instance;
		return &instance;
	}

	void Imagespace::Load()
	{
		LoadSettings();
		L->info("Loaded: enabled={} op={} exposure={:.2f} adaptive={} bloom={} vig={} ca={} sharp={} dof={}",
			settings.enabled, settings.tonemapOperator, settings.exposure,
			settings.adaptiveExposure, settings.bloomEnable,
			settings.vignetteEnable, settings.caEnable, settings.sharpenEnable,
			settings.dofEnable);
	}

	void Imagespace::OnPostPostLoad()
	{
		cs::engine::RegisterPostDynResViewport_Imagespace([] {
			Imagespace::GetSingleton()->RunFrame();
		});
		L->info("Registered Imagespace post-upscale callback on cs::engine broker");

		// IsActive (vfunc 8) replacement; engine DOF resumes when bDOFEnable is false.
		stl::write_vfunc<0x8, ImageSpaceEffectDepthOfField_IsActive>(RE::VTABLE::ImageSpaceEffectDepthOfField[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectBokehDepthOfField_IsActive>(RE::VTABLE::ImageSpaceEffectBokehDepthOfField[0]);
		stl::write_vfunc<0x8, ImageSpaceEffectFullScreenBlur_IsActive>(RE::VTABLE::ImageSpaceEffectFullScreenBlur[0]);
		L->info("Engine DOF effects vfunc-disabled (DepthOfField + BokehDepthOfField + FullScreenBlur)");
	}

	void Imagespace::OnDataLoaded()
	{
		// Smoke mode: force the requested engine weather once after data load.
		if (!forcedWeatherFormID.has_value()) return;
		auto* sky = RE::Sky::GetSingleton();
		if (!sky) return;
		auto* tw = RE::TESForm::GetFormByID<RE::TESWeather>(*forcedWeatherFormID);
		if (!tw) {
			L->warn("Forced weather formID 0x{:08X} not found or not a TESWeather at OnDataLoaded", *forcedWeatherFormID);
			return;
		}
		sky->ReleaseWeatherOverride();
		sky->ForceWeather(tw, true);
		L->info("Sky::ForceWeather invoked for 0x{:08X}", *forcedWeatherFormID);
	}

	void Imagespace::OnD3D11Ready(IDXGIAdapter* /*a_adapter*/, ID3D11Device* /*a_device*/)
	{
		ApplyLUTState();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(Imagespace::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
