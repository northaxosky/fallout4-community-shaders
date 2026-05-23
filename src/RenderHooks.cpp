#include "RenderHooks.h"

#include "Log.h"

#include <algorithm>
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
			std::uint64_t       seq;
			RenderHookCallback  cb;
		};

		std::uint64_t                       g_nextSeq = 0;
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

		void InsertPrioritized(std::vector<PrioritizedCallback>& v, RenderHookCallback&& cb, HookPriority p)
		{
			v.push_back({ p, g_nextSeq++, std::move(cb) });
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
				func();
				Dispatch(g_postDeferredPrePass);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferredLightsImpl_Hook
		{
			static void thunk()
			{
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
				func();
				Dispatch(g_postDeferredComposite);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// SetUseDynamicResolutionViewportAsDefaultViewport(This, a_setting) call site after the post-
		// upscale buffer has been written. Order: engine func first, then Imagespace post-FX, then
		// FG HUDLess capture (so FG captures post-imagespace pixels for DLSS-G judder-free output).
		struct PostDynResViewport_Hook
		{
			static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_setting)
			{
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

		// REL::ID 587723 / 2318322 / 2318322 + offsets { 0xE1, 0xC5, 0xC5 } is the single E8 call site
		// inside the deferred composite epilogue that re-arms the engine viewport after Upscaling has
		// written the post-upscale buffer. Confirmed shared by Imagespace + FrameGeneration; cited in
		// Fallout4RE/Workspace/exports/cs-render-subsystem-ids.json (commit 20e5fa7).
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
		InsertPrioritized(g_postDeferredPrePass, std::move(callback), priority);
		if (!g_prePassInstalled) {
			stl::detour_thunk<DeferredPrePass_Hook>(REL::ID({ 56596, 2318301, 2318301 }));
			g_prePassInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredPrePass");
		}
	}

	void RegisterPreDeferredLightsImpl(RenderHookCallback callback, HookPriority priority)
	{
		InsertPrioritized(g_preDeferredLightsImpl, std::move(callback), priority);
		if (!g_lightsImplInstalled) {
			stl::detour_thunk<DeferredLightsImpl_Hook>(REL::ID({ 1108521, 2318312, 2318312 }));
			g_lightsImplInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredLightsImpl");
		}
	}

	void RegisterPostDeferredLightsImpl(RenderHookCallback callback, HookPriority priority)
	{
		InsertPrioritized(g_postDeferredLightsImpl, std::move(callback), priority);
		if (!g_lightsImplInstalled) {
			stl::detour_thunk<DeferredLightsImpl_Hook>(REL::ID({ 1108521, 2318312, 2318312 }));
			g_lightsImplInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredLightsImpl");
		}
	}

	// REL::ID confirmed in Fallout4RE/exports/cs-render-subsystem-ids.json
	// (commit 20e5fa7) by anchor-walking from DrawWorld::Render_PreUI:
	//   OG  RVA 0x02855E60  AL id 728427
	//   NG  RVA 0x0209B100  AL id 2318313
	//   AE  RVA 0x021F0790  AL id 2318313
	// NG and AE compile to identical body sizes / instruction counts /
	// mnemonic hashes, and Address Library v2 issues the same id in both.
	void RegisterPostDeferredComposite(RenderHookCallback callback, HookPriority priority)
	{
		InsertPrioritized(g_postDeferredComposite, std::move(callback), priority);
		if (!g_compositeInstalled) {
			stl::detour_thunk<DeferredComposite_Hook>(REL::ID({ 728427, 2318313, 2318313 }));
			g_compositeInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredComposite");
		}
	}

	void RegisterPostDynResViewport_Imagespace(RenderHookCallback callback)
	{
		g_postDynResViewport_Imagespace.push_back(std::move(callback));
		EnsurePostDynResViewportInstalled();
	}

	void RegisterPostDynResViewport_FGCapture(PostDynResViewportFGCb callback)
	{
		g_postDynResViewport_FGCapture.push_back(std::move(callback));
		EnsurePostDynResViewportInstalled();
	}
}
