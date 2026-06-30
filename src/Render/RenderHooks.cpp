#include "Render/RenderHooks.h"

#include "Log.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace { auto* L = cs::log::Get("cs.hooks"); }

namespace cs::engine
{
	namespace
	{
		struct PrioritizedCallback
		{
			HookPriority        priority;
			RenderHookCallback  cb;
		};

		std::vector<PrioritizedCallback>    g_postDeferredPrePass;
		std::vector<PrioritizedCallback>    g_preDeferredLightsImpl;
		std::vector<PrioritizedCallback>    g_postDeferredLightsImpl;
		std::vector<PrioritizedCallback>    g_postDeferredComposite;
		std::vector<RenderHookCallback>     g_postDynResViewport_Imagespace;
		std::vector<PostDynResViewportFGCb> g_postDynResViewport_FGCapture;
		bool g_prePassInstalled            = false;
		bool g_lightsImplInstalled         = false;
		bool g_compositeInstalled          = false;
		bool g_postDynResViewportInstalled = false;

#if !defined(NDEBUG)
		const DWORD      g_registrationThreadId = ::GetCurrentThreadId();
		std::atomic_bool g_registrationClosed{ false };

		void MarkRegistrationClosed() noexcept
		{
			g_registrationClosed.store(true, std::memory_order_relaxed);
		}

		void AssertRegistrationAllowed()
		{
			assert(::GetCurrentThreadId() == g_registrationThreadId &&
				"RenderHooks registration must run on the startup thread");
			assert(!g_registrationClosed.load(std::memory_order_relaxed) &&
				"RenderHooks registration must finish before render hooks fire");
		}
#else
		void MarkRegistrationClosed() noexcept {}
		void AssertRegistrationAllowed() noexcept {}
#endif

		void InsertPrioritized(std::vector<PrioritizedCallback>& v, RenderHookCallback&& cb, HookPriority p)
		{
			v.push_back({ p, std::move(cb) });
			std::stable_sort(v.begin(), v.end(),
				[](const PrioritizedCallback& a, const PrioritizedCallback& b) {
					return static_cast<int>(a.priority) < static_cast<int>(b.priority);
				});
		}

		void Dispatch(const std::vector<PrioritizedCallback>& v)
		{
			for (auto& entry : v) {
				entry.cb();
			}
		}

		struct DeferredPrePass_Hook
		{
			static void thunk()
			{
				MarkRegistrationClosed();
				func();
				Dispatch(g_postDeferredPrePass);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferredLightsImpl_Hook
		{
			static void thunk()
			{
				MarkRegistrationClosed();
				Dispatch(g_preDeferredLightsImpl);
				func();
				Dispatch(g_postDeferredLightsImpl);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferredComposite_Hook
		{
			static void thunk()
			{
				MarkRegistrationClosed();
				func();
				Dispatch(g_postDeferredComposite);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Post-upscale viewport reset: engine first, then Imagespace post-FX, then FG captures post-FX pixels.
		struct PostDynResViewport_Hook
		{
			static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_setting)
			{
				MarkRegistrationClosed();
				func(This, a_setting);
				for (auto& cb : g_postDynResViewport_Imagespace) {
					cb();
				}
				for (auto& cb : g_postDynResViewport_FGCapture) {
					cb(a_setting);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// REL::ID 587723/2318322/2318322 + {0xE1,0xC5,0xC5}: shared viewport re-arm E8 after upscaling.
		// Source: Fallout4RE exports/cs-render-subsystem-ids.json @ 20e5fa7.
		void EnsurePostDynResViewportInstalled()
		{
			if (g_postDynResViewportInstalled) {
				return;
			}
			const auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
			constexpr std::ptrdiff_t offsets[] = { 0xE1, 0xC5, 0xC5 };
			stl::write_thunk_call<PostDynResViewport_Hook>(
				REL::ID({ 587723, 2318322, 2318322 }).address() + offsets[runtimeIdx]);
			g_postDynResViewportInstalled = true;
			L->info("Hook installed on SetUseDynamicResolutionViewportAsDefaultViewport (broker)");
		}
	}

	void RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority)
	{
		AssertRegistrationAllowed();
		InsertPrioritized(g_postDeferredPrePass, std::move(callback), priority);
		if (!g_prePassInstalled) {
			stl::detour_thunk<DeferredPrePass_Hook>(REL::ID({ 56596, 2318301, 2318301 }));
			g_prePassInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredPrePass");
		}
	}

	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority)
	{
		AssertRegistrationAllowed();
		InsertPrioritized(g_preDeferredLightsImpl, std::move(callback), priority);
		if (!g_lightsImplInstalled) {
			stl::detour_thunk<DeferredLightsImpl_Hook>(REL::ID({ 1108521, 2318312, 2318312 }));
			g_lightsImplInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredLightsImpl");
		}
	}

	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority)
	{
		AssertRegistrationAllowed();
		InsertPrioritized(g_postDeferredLightsImpl, std::move(callback), priority);
		if (!g_lightsImplInstalled) {
			stl::detour_thunk<DeferredLightsImpl_Hook>(REL::ID({ 1108521, 2318312, 2318312 }));
			g_lightsImplInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredLightsImpl");
		}
	}

	// REL::ID 728427/2318313/2318313 from DrawWorld::Render_PreUI anchor walk; NG/AE bodies match.
	// Source: Fallout4RE exports/cs-render-subsystem-ids.json @ 20e5fa7.
	void RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority)
	{
		AssertRegistrationAllowed();
		InsertPrioritized(g_postDeferredComposite, std::move(callback), priority);
		if (!g_compositeInstalled) {
			stl::detour_thunk<DeferredComposite_Hook>(REL::ID({ 728427, 2318313, 2318313 }));
			g_compositeInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredComposite");
		}
	}

	void RegisterPostDynResViewport_Imagespace(RenderHookCallback callback)
	{
		AssertRegistrationAllowed();
		g_postDynResViewport_Imagespace.push_back(std::move(callback));
		EnsurePostDynResViewportInstalled();
	}

	void RegisterPostDynResViewport_FGCapture(PostDynResViewportFGCb callback)
	{
		AssertRegistrationAllowed();
		g_postDynResViewport_FGCapture.push_back(std::move(callback));
		EnsurePostDynResViewportInstalled();
	}
}
