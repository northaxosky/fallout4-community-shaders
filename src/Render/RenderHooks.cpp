#include "Render/RenderHooks.h"

#include "Log.h"
#include "Render/DeferredDrawAnchor.h"
#include "Render/Engine.h"
#include "Render/EngineCallSite.h"
#include "Render/FrameProfiler.h"
#include "Render/ShaderInjection.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	auto* L = cs::log::Get("cs.hooks");
}

namespace cs::engine
{
	namespace
	{
		struct PrioritizedCallback
		{
			HookPriority priority;
			RenderHookCallback cb;
		};

		std::vector<PrioritizedCallback> g_postDeferredPrePass;
		std::vector<PrioritizedCallback> g_preDeferredPrePass;
		std::vector<PrioritizedCallback> g_preDeferredLightsImpl;
		std::vector<PrioritizedCallback> g_postDeferredLightsImpl;
		std::vector<PrioritizedCallback> g_preDeferredComposite;
		std::vector<PrioritizedCallback> g_postDeferredComposite;
		std::vector<PrioritizedCallback> g_preFullscreenDeferredLightDraw;
		std::vector<PrioritizedCallback> g_postForwardSky;
		std::vector<PrioritizedCallback> g_postPrecipitationOcclusion;
		std::vector<PrioritizedCallback> g_postSunShadowRender;
		std::vector<PrioritizedCallback> g_preWaterUpdate;
		bool g_prePassInstalled = false;
		bool g_lightsImplInstalled = false;
		bool g_compositeInstalled = false;
		bool g_deferredDrawAnchorInstalled = false;
		bool g_forwardSkyInstalled = false;
		bool g_precipitationOcclusionInstalled = false;
		bool g_sunShadowRenderInstalled = false;
		bool g_waterUpdateInstalled = false;
		bool g_insideDeferredLightsImpl = false;
		bool g_insideDeferredComposite = false;

		// Registration closes when the first render hook runs.
		const DWORD g_registrationThreadId = ::GetCurrentThreadId();
		std::atomic_bool g_registrationClosed{ false };

		void MarkRegistrationClosed() noexcept
		{
			g_registrationClosed.store(true, std::memory_order_relaxed);
		}

		bool RegistrationAllowed(const char* a_where)
		{
			const bool onStartupThread = (::GetCurrentThreadId() == g_registrationThreadId);
			const bool stillOpen = !g_registrationClosed.load(std::memory_order_relaxed);
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

		// Callbacks run inside engine frames, so failures are logged, not unwound.
		void DispatchGuarded(const std::vector<PrioritizedCallback>& v, const char* a_where) noexcept
		{
			for (auto& entry : v) {
				try {
					entry.cb();
				} catch (const std::exception& e) {
					L->error("{} callback failed: {}", a_where, e.what());
				} catch (...) {
					L->error("{} callback failed.", a_where);
				}
			}
		}

		class PostDispatchScope
		{
		public:
			explicit PostDispatchScope(
				const std::vector<PrioritizedCallback>& a_callbacks) noexcept :
				_callbacks(a_callbacks)
			{}
			~PostDispatchScope() noexcept
			{
				DispatchGuarded(_callbacks, "Deferred-lights post");
			}

			PostDispatchScope(const PostDispatchScope&) = delete;
			PostDispatchScope& operator=(const PostDispatchScope&) = delete;

		private:
			const std::vector<PrioritizedCallback>& _callbacks;
		};

		struct DeferredPrePass_Hook
		{
			static void thunk()
			{
				MarkRegistrationClosed();
				Dispatch(g_preDeferredPrePass);
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
				const PostDispatchScope post(g_postDeferredLightsImpl);
				Dispatch(g_preDeferredLightsImpl);
				{
					ScopedRenderPhase phase(g_insideDeferredLightsImpl);
					func();
				}
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
				if (decision.dispatchFullscreenLightCallbacks) {
					Dispatch(g_preFullscreenDeferredLightDraw);
				}
				if (decision.dispatchInjections) {
					auto* rendererData = RE::BSGraphics::GetRendererData();
					DispatchInjectionsForBoundPixelShader(rendererData ?
															  reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
															  nullptr);
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

		struct DrawProfiling_Hook
		{
			static void thunk(bool a_force, bool a_clear)
			{
				func(a_force, a_clear);
				render::profiling::RecordDraw();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TriShapeDrawScope_Hook
		{
			static void thunk(
				RE::BSGraphics::Renderer* a_this,
				RE::BSGraphics::TriShape* a_shape,
				std::uint32_t a_start,
				std::uint32_t a_triangles)
			{
				if (const auto* state = GetGraphicsState())
					BeginShaderInjectionFrame(state->frameCount);
				const ScopedPixelShaderInjectionBindings bindings;
				func(a_this, a_shape, a_start, a_triangles);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct PrecipitationOcclusion_Hook
		{
			static void thunk()
			{
				MarkRegistrationClosed();
				func();
				DispatchGuarded(g_postPrecipitationOcclusion, "Precipitation occlusion");
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		[[nodiscard]] const RE::BSShadowDirectionalLight* GetSunShadowLight() noexcept
		{
			const auto* scene = GetWorldShadowSceneNode();
			return scene ? scene->directionalShadowLight : nullptr;
		}

		// The sun's Render(7) is the only directional render in DeferredLightsImpl.
		struct SunShadowRender_Hook
		{
			static void thunk(RE::BSShadowDirectionalLight* a_this, std::uint32_t a_mask)
			{
				func(a_this, a_mask);
				constexpr std::uint32_t kMainSunMask = 7;
				if (a_mask == kMainSunMask && g_insideDeferredLightsImpl && a_this == GetSunShadowLight())
					DispatchGuarded(g_postSunShadowRender, "Post sun shadow render");
			}
			static inline REL::Relocation<void(RE::BSShadowDirectionalLight*, std::uint32_t)> func;
		};

		// DrawWorld::Forward renders sky batch 7, then cloud group 14; water and alpha follow.
		struct ForwardSkyGroup_Hook
		{
			static void thunk(
				RE::BSShaderAccumulator* a_accumulator,
				std::uint32_t a_group,
				bool a_alpha)
			{
				func(a_accumulator, a_group, a_alpha);
				Dispatch(g_postForwardSky);
			}
			static inline REL::Relocation<void(RE::BSShaderAccumulator*, std::uint32_t, bool)> func;
		};

		// Render_PreUI calls the water callback before world setup.
		struct UpdateWaterFunc_Hook
		{
			using Callback = RE::DrawWorld::UpdateWaterFunc;

			static void Invoke(RE::BSGeometryListCullingProcess* a_cullingProcess)
			{
				MarkRegistrationClosed();
				DispatchGuarded(g_preWaterUpdate, "Pre water update");
				original.load(std::memory_order_acquire)(a_cullingProcess);
			}

			static void thunk(Callback a_callback)
			{
				original.store(a_callback, std::memory_order_release);
				func(a_callback ? &Invoke : nullptr);
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static inline std::atomic<Callback> original{ nullptr };
		};

		bool EnsureWaterUpdateInstalled()
		{
			if (g_waterUpdateInstalled) {
				return true;
			}
			stl::detour_thunk<UpdateWaterFunc_Hook>(RE::ID::DrawWorld::SetUpdateWaterFunc);
			if (UpdateWaterFunc_Hook::func.address() == 0) {
				L->error("Hook failed on DrawWorld::SetUpdateWaterFunc");
				return false;
			}
			g_waterUpdateInstalled = true;
			L->info("Hook installed on DrawWorld::SetUpdateWaterFunc (pre water update)");
			return true;
		}

		void EnsureForwardSkyInstalled()
		{
			if (g_forwardSkyInstalled) {
				return;
			}
			const auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
			constexpr std::ptrdiff_t offsets[] = { 0x2C9, 0x2D5, 0x2D5 };
			stl::write_thunk_call<ForwardSkyGroup_Hook>(
				RE::ID::DrawWorld::Forward.address() + offsets[runtimeIdx]);
			g_forwardSkyInstalled = true;
			L->info("Hook installed on DrawWorld::Forward cloud group call (post-sky boundary)");
		}

		bool EnsurePrecipitationOcclusionInstalled()
		{
			if (g_precipitationOcclusionInstalled) {
				return true;
			}
			stl::detour_thunk<PrecipitationOcclusion_Hook>(RE::ID::Precipitation::RenderOcclusionMap);
			if (PrecipitationOcclusion_Hook::func.address() == 0) {
				L->error("Hook failed on Precipitation::RenderOcclusionMap");
				return false;
			}
			g_precipitationOcclusionInstalled = true;
			L->info("Hook installed on Precipitation::RenderOcclusionMap");
			return true;
		}

		void EnsureDeferredLightsImplInstalled()
		{
			if (g_lightsImplInstalled) {
				return;
			}
			stl::detour_thunk<DeferredLightsImpl_Hook>(RE::ID::DrawWorld::DeferredLightsImpl);
			g_lightsImplInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredLightsImpl");
		}

		bool EnsureSunShadowRenderInstalled()
		{
			if (g_sunShadowRenderInstalled) {
				return true;
			}
			constexpr std::size_t kRenderVtableSlot = 10;
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::BSShadowDirectionalLight[0] };
			const auto slot = vtable.address() + kRenderVtableSlot * sizeof(std::uintptr_t);
			const auto expected = RE::ID::BSShadowDirectionalLight::Render.address();
			if (*reinterpret_cast<const std::uintptr_t*>(slot) != expected) {
				L->error("BSShadowDirectionalLight vtable slot {} is not Render ({:#x}); sun shadow hook unavailable", kRenderVtableSlot, expected);
				return false;
			}
			stl::write_vfunc<kRenderVtableSlot, SunShadowRender_Hook>(RE::VTABLE::BSShadowDirectionalLight[0]);
			g_sunShadowRenderInstalled = SunShadowRender_Hook::func.address() != 0;
			if (!g_sunShadowRenderInstalled) {
				L->error("Hook failed on BSShadowDirectionalLight::Render");
				return false;
			}
			// The inside-DeferredLightsImpl flag only flips while that hook is installed.
			EnsureDeferredLightsImplInstalled();
			L->info("Hook installed on BSShadowDirectionalLight::Render (vtable slot {})", kRenderVtableSlot);
			return true;
		}

		void EnsureDeferredPrePassInstalled()
		{
			if (g_prePassInstalled) {
				return;
			}
			stl::detour_thunk<DeferredPrePass_Hook>(RE::ID::DrawWorld::DeferredPrePass);
			g_prePassInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredPrePass");
		}

		void EnsureDeferredCompositeInstalled()
		{
			if (g_compositeInstalled) {
				return;
			}
			stl::detour_thunk<DeferredComposite_Hook>(RE::ID::DrawWorld::DeferredComposite);
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
			constexpr cs::engine::CallSiteAnchor kDrawTriShapeSetDirtyStates{
				.name = "DrawTriShape -> BSGraphics::SetDirtyStates",
				.function = RE::ID::BSGraphics::Renderer::DrawTriShape,
				.offset = { 0x9C, 0x9A, 0x9A },
				.target = RE::ID::BSGraphics::SetDirtyStates
			};
			const auto site = cs::engine::ResolveCallSite(kDrawTriShapeSetDirtyStates);
			if (!site) {
				L->error("Deferred draw anchor unavailable: {}", site.error());
				return;
			}
			stl::write_thunk_call<DeferredDrawAnchor_Hook>(*site);
			// FO4: forward consumers bind after dirty state and restore at the draw boundary.
			stl::detour_thunk<TriShapeDrawScope_Hook>(kDrawTriShapeSetDirtyStates.function);
			g_deferredDrawAnchorInstalled = true;
			L->info("Hook installed on DrawTriShape SetDirtyStates call (deferred draw anchor)");
		}
	}

	bool EnsureDeferredDrawAnchorInstalled()
	{
		if (!RegistrationAllowed("DeferredDrawAnchor"))
			return false;
		InstallDeferredDrawAnchor();
		return g_deferredDrawAnchorInstalled;
	}

	bool EnsureDrawProfilingInstalled()
	{
		static bool installed{};
		if (installed)
			return true;
		if (!RegistrationAllowed("DrawProfiling"))
			return false;
		// FO4: SetDirtyStates is the shared native draw submission boundary on OG/NG/AE.
		stl::detour_thunk<DrawProfiling_Hook>(RE::ID::BSGraphics::SetDirtyStates);
		installed = true;
		return true;
	}

	bool RenderHookRegistrationAllowed(const char* a_where)
	{
		return RegistrationAllowed(a_where);
	}

	bool RegisterPostDeferredPrePass(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostDeferredPrePass"))
			return false;
		InsertPrioritized(g_postDeferredPrePass, std::move(callback), priority);
		EnsureDeferredPrePassInstalled();
		return true;
	}

	bool RegisterPreDeferredPrePass(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PreDeferredPrePass"))
			return false;
		InsertPrioritized(g_preDeferredPrePass, std::move(callback), priority);
		EnsureDeferredPrePassInstalled();
		return true;
	}

	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PreDeferredLightsImpl"))
			return;
		InsertPrioritized(g_preDeferredLightsImpl, std::move(callback), priority);
		EnsureDeferredLightsImplInstalled();
	}

	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostDeferredLightsImpl"))
			return;
		InsertPrioritized(g_postDeferredLightsImpl, std::move(callback), priority);
		EnsureDeferredLightsImplInstalled();
	}

	bool RegisterPreDeferredComposite(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PreDeferredComposite"))
			return false;
		InsertPrioritized(g_preDeferredComposite, std::move(callback), priority);
		EnsureDeferredCompositeInstalled();
		return true;
	}

	bool RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostDeferredComposite"))
			return false;
		InsertPrioritized(g_postDeferredComposite, std::move(callback), priority);
		EnsureDeferredCompositeInstalled();
		return true;
	}

	bool RegisterPreWaterUpdate(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PreWaterUpdate"))
			return false;
		InsertPrioritized(g_preWaterUpdate, std::move(callback), priority);
		return EnsureWaterUpdateInstalled();
	}

	bool RegisterPostForwardSky(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostForwardSky"))
			return false;
		InsertPrioritized(g_postForwardSky, std::move(callback), priority);
		EnsureForwardSkyInstalled();
		return true;
	}

	bool RegisterPostSunShadowRender(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostSunShadowRender"))
			return false;
		InsertPrioritized(g_postSunShadowRender, std::move(callback), priority);
		return EnsureSunShadowRenderInstalled();
	}

	bool RegisterPostPrecipitationOcclusion(RenderHookCallback callback, HookPriority priority)
	{
		if (!RegistrationAllowed("PostPrecipitationOcclusion"))
			return false;
		InsertPrioritized(g_postPrecipitationOcclusion, std::move(callback), priority);
		return EnsurePrecipitationOcclusionInstalled();
	}

	void RegisterPreFullscreenDeferredLightDraw(
		RenderHookCallback callback,
		HookPriority priority)
	{
		if (!RegistrationAllowed("PreFullscreenDeferredLightDraw"))
			return;
		InsertPrioritized(g_preFullscreenDeferredLightDraw, std::move(callback), priority);
		InstallDeferredDrawAnchor();
	}
}
