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
		std::vector<PrioritizedCallback>    g_preSunLightDraw;
		bool g_prePassInstalled            = false;
		bool g_lightsImplInstalled         = false;
		bool g_compositeInstalled          = false;
		bool g_postDynResViewportInstalled = false;
		bool g_preSunLightDrawInstalled    = false;
		// Set by DeferredLightsImpl_Hook around the engine's deferred-lighting call; read by the
		// PreSunLightDraw thunk to restrict binding to that phase (DrawTriShape is the generic
		// geometry draw and would otherwise fire thousands of times per frame).
		bool g_insideDeferredLightsImpl    = false;

		// Registration must run on the startup thread before any render hook fires. Enforced in
		// release too: MarkRegistrationClosed runs on the first thunk, and a late or off-thread
		// Register* is rejected (logged) instead of mutating the vectors while Dispatch may iterate.
		const DWORD      g_registrationThreadId = ::GetCurrentThreadId();
		std::atomic_bool g_registrationClosed{ false };

		void MarkRegistrationClosed() noexcept
		{
			g_registrationClosed.store(true, std::memory_order_relaxed);
		}

		bool RegistrationAllowed(const char* a_where)
		{
			const bool onStartupThread = (::GetCurrentThreadId() == g_registrationThreadId);
			const bool stillOpen       = !g_registrationClosed.load(std::memory_order_relaxed);
			assert(onStartupThread && "RenderHooks registration must run on the startup thread");
			assert(stillOpen && "RenderHooks registration must finish before render hooks fire");
			if (!onStartupThread || !stillOpen) {
				L->error("Rejected RenderHooks registration for {} (late or off-thread)", a_where);
				return false;
			}
			return true;
		}

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
				g_insideDeferredLightsImpl = true;
				func();
				g_insideDeferredLightsImpl = false;
				Dispatch(g_postDeferredLightsImpl);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// write_thunk_call on the `call BSGraphics::SetDirtyStates(bool,bool)` inside the generic
		// DrawTriShape (REL::ID 763320/2276846/2276846 + {0x9C,0x9A,0x9A}, AE-verified). SetDirtyStates
		// flushes the pending PS SRV binds (it leaves t6 NULL for the sun draw), so dispatching AFTER
		// func() lands in the window between the SRV flush and the DrawIndexed. R8D/R9D still hold
		// DrawTriShape's startIndex/primitiveCount at the call (verified: the prologue does mov r15d,r9d
		// without clobbering r9, and nothing writes r8/r9 before the call). primitiveCount==2 selects
		// the fullscreen DrawIndexed(6) light passes; the phase flag restricts to DeferredLightsImpl.
		struct PreSunLightDraw_Hook
		{
			static void thunk(bool a_force, bool a_clear, std::uint32_t /*a_startIndex*/, std::uint32_t a_primitiveCount)
			{
				func(a_force, a_clear);
				if (g_insideDeferredLightsImpl && a_primitiveCount == 2) {
					Dispatch(g_preSunLightDraw);
				}
			}
			static inline REL::Relocation<void(bool, bool)> func;
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

		void EnsureDeferredLightsImplInstalled()
		{
			if (g_lightsImplInstalled) {
				return;
			}
			stl::detour_thunk<DeferredLightsImpl_Hook>(REL::ID({ 1108521, 2318312, 2318312 }));
			g_lightsImplInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredLightsImpl");
		}

		// REL::ID 763320/2276846/2276846 + {0x9C,0x9A,0x9A}: the SetDirtyStates call inside DrawTriShape.
		// Source: Fallout4RE AE (1.11.221) disasm + version.bin, cross-checked against NG.
		void EnsurePreSunLightDrawInstalled()
		{
			if (g_preSunLightDrawInstalled) {
				return;
			}
			// The thunk's phase gate needs DeferredLightsImpl's enter/exit flag.
			EnsureDeferredLightsImplInstalled();
			const auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
			constexpr std::ptrdiff_t offsets[] = { 0x9C, 0x9A, 0x9A };
			stl::write_thunk_call<PreSunLightDraw_Hook>(
				REL::ID({ 763320, 2276846, 2276846 }).address() + offsets[runtimeIdx]);
			g_preSunLightDrawInstalled = true;
			L->info("Hook installed on DrawTriShape SetDirtyStates call (sun-light draw anchor)");
		}
	}

	void RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostDeferredPrePass")) return;
		InsertPrioritized(g_postDeferredPrePass, std::move(callback), priority);
		if (!g_prePassInstalled) {
			stl::detour_thunk<DeferredPrePass_Hook>(REL::ID({ 56596, 2318301, 2318301 }));
			g_prePassInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredPrePass");
		}
	}

	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PreDeferredLightsImpl")) return;
		InsertPrioritized(g_preDeferredLightsImpl, std::move(callback), priority);
		EnsureDeferredLightsImplInstalled();
	}

	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostDeferredLightsImpl")) return;
		InsertPrioritized(g_postDeferredLightsImpl, std::move(callback), priority);
		EnsureDeferredLightsImplInstalled();
	}

	// REL::ID 728427/2318313/2318313 from DrawWorld::Render_PreUI anchor walk; NG/AE bodies match.
	// Source: Fallout4RE exports/cs-render-subsystem-ids.json @ 20e5fa7.
	void RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostDeferredComposite")) return;
		InsertPrioritized(g_postDeferredComposite, std::move(callback), priority);
		if (!g_compositeInstalled) {
			stl::detour_thunk<DeferredComposite_Hook>(REL::ID({ 728427, 2318313, 2318313 }));
			g_compositeInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredComposite");
		}
	}

	void RegisterPostDynResViewport_Imagespace(RenderHookCallback callback)
	{
		if (!RegistrationAllowed("PostDynResViewport_Imagespace")) return;
		g_postDynResViewport_Imagespace.push_back(std::move(callback));
		EnsurePostDynResViewportInstalled();
	}

	void RegisterPostDynResViewport_FGCapture(PostDynResViewportFGCb callback)
	{
		if (!RegistrationAllowed("PostDynResViewport_FGCapture")) return;
		g_postDynResViewport_FGCapture.push_back(std::move(callback));
		EnsurePostDynResViewportInstalled();
	}

	void RegisterPreSunLightDraw(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PreSunLightDraw")) return;
		InsertPrioritized(g_preSunLightDraw, std::move(callback), priority);
		EnsurePreSunLightDrawInstalled();
	}
}
