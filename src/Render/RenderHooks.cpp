#include "Render/RenderHooks.h"

#include "Log.h"
#include "Render/DeferredDrawAnchor.h"
#include "Render/ShaderInjection.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
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
		std::vector<PrioritizedCallback>    g_preDeferredComposite;
		std::vector<PrioritizedCallback>    g_postDeferredComposite;
		std::vector<PostDynResViewportFGCb> g_postDynResViewport_FGCapture;
		std::vector<PrioritizedCallback>    g_preSunLightDraw;
		bool g_prePassInstalled            = false;
		bool g_lightsImplInstalled         = false;
		bool g_compositeInstalled          = false;
		bool g_postDynResViewportInstalled = false;
		bool g_deferredDrawAnchorInstalled = false;
		// Empty means no earlier viewport thunk owner.
		std::string g_postDynResViewportPreThunkOwner;
		bool        g_postDynResViewportPreThunkClaimed = false;
		bool g_insideDeferredLightsImpl    = false;
		bool g_insideDeferredComposite     = false;

		// Registration closes when the first render hook runs.
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

		class ScopedRenderPhase
		{
		public:
			explicit ScopedRenderPhase(bool& a_phase) noexcept :
				_phase(a_phase),
				_previous(std::exchange(a_phase, true))
			{}

			~ScopedRenderPhase()
			{
				_phase = _previous;
			}

			ScopedRenderPhase(const ScopedRenderPhase&) = delete;
			ScopedRenderPhase& operator=(const ScopedRenderPhase&) = delete;

		private:
			bool& _phase;
			bool _previous;
		};

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
				{
					ScopedRenderPhase phase(g_insideDeferredLightsImpl);
					func();
				}
				Dispatch(g_postDeferredLightsImpl);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The fourth argument is residual r9d state, not a call parameter.
		struct DeferredDrawAnchor_Hook
		{
			static void thunk(
				bool a_force,
				bool a_clear,
				std::uint32_t,
				std::uint32_t a_residualR9d)
			{
				func(a_force, a_clear);
				const auto decision = SelectDeferredDrawAnchorDecision(
					g_insideDeferredLightsImpl,
					g_insideDeferredComposite,
					a_residualR9d);
				if (decision.dispatchInjections) {
					auto* rendererData = RE::BSGraphics::GetRendererData();
					DispatchInjectionsForBoundPixelShader(rendererData ?
						reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
						nullptr);
				}
				if (decision.dispatchLegacySunCallbacks) {
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
				Dispatch(g_preDeferredComposite);
				{
					ScopedRenderPhase phase(g_insideDeferredComposite);
					func();
				}
				Dispatch(g_postDeferredComposite);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Capture must observe the post-upscale viewport.
		struct PostDynResViewport_Hook
		{
			static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_setting)
			{
				MarkRegistrationClosed();
				func(This, a_setting);
				for (auto& cb : g_postDynResViewport_FGCapture) {
					cb(a_setting);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void EnsurePostDynResViewportInstalled()
		{
			if (g_postDynResViewportInstalled) {
				return;
			}
			// Earlier viewport thunks must install before the broker.
			if (g_postDynResViewportPreThunkClaimed) {
				L->info("Broker chaining PostDynResViewport after pre-thunk '{}'.", g_postDynResViewportPreThunkOwner);
			} else {
				L->info("Broker installing PostDynResViewport without a pre-thunk (e.g. Upscaling disabled).");
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

		void EnsureDeferredCompositeInstalled()
		{
			if (g_compositeInstalled) {
				return;
			}
			stl::detour_thunk<DeferredComposite_Hook>(REL::ID({ 728427, 2318313, 2318313 }));
			g_compositeInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredComposite");
		}

		void InstallDeferredDrawAnchor()
		{
			EnsureDeferredLightsImplInstalled();
			EnsureDeferredCompositeInstalled();
			if (g_deferredDrawAnchorInstalled) {
				return;
			}
			const auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
			constexpr std::ptrdiff_t offsets[] = { 0x9C, 0x9A, 0x9A };
			stl::write_thunk_call<DeferredDrawAnchor_Hook>(
				REL::ID({ 763320, 2276846, 2276846 }).address() + offsets[runtimeIdx]);
			g_deferredDrawAnchorInstalled = true;
			L->info("Hook installed on DrawTriShape SetDirtyStates call (deferred draw anchor)");
		}
	}

	bool EnsurePreSunLightDrawInstalled()
	{
		if (!RegistrationAllowed("DeferredDrawAnchor")) return false;
		InstallDeferredDrawAnchor();
		return g_deferredDrawAnchorInstalled;
	}

	bool RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostDeferredPrePass")) return false;
		InsertPrioritized(g_postDeferredPrePass, std::move(callback), priority);
		if (!g_prePassInstalled) {
			stl::detour_thunk<DeferredPrePass_Hook>(REL::ID({ 56596, 2318301, 2318301 }));
			g_prePassInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredPrePass");
		}
		return true;
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

	bool RegisterPreDeferredComposite(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PreDeferredComposite")) return false;
		InsertPrioritized(g_preDeferredComposite, std::move(callback), priority);
		EnsureDeferredCompositeInstalled();
		return true;
	}

	bool RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostDeferredComposite")) return false;
		InsertPrioritized(g_postDeferredComposite, std::move(callback), priority);
		EnsureDeferredCompositeInstalled();
		return true;
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
		InstallDeferredDrawAnchor();
	}

	void MarkPostDynResViewportPreThunkInstalled(std::string_view a_ownerLabel)
	{
		if (!RegistrationAllowed("PostDynResViewport_PreThunk")) return;
		if (g_postDynResViewportPreThunkClaimed) {
			L->error("Duplicate PostDynResViewport pre-thunk claim: existing '{}', new '{}'. Two owners fighting for REL::ID(587723)+0xE1 will silently chain in load order.",
				g_postDynResViewportPreThunkOwner, a_ownerLabel);
			return;
		}
		g_postDynResViewportPreThunkOwner = std::string(a_ownerLabel);
		g_postDynResViewportPreThunkClaimed = true;
		if (g_postDynResViewportInstalled) {
			L->error("PostDynResViewport pre-thunk '{}' installed AFTER the broker; wrap order is inverted. FGCapture will now run before the pre-thunk body (pre-upscale pixels).",
				a_ownerLabel);
		} else {
			L->info("PostDynResViewport pre-thunk '{}' claimed; broker will chain after it.", a_ownerLabel);
		}
	}
}
