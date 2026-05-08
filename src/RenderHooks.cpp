#include "RenderHooks.h"

#include "Log.h"

#include <vector>

namespace { auto* L = cs::log::Get("cs.hooks"); }

namespace cs::engine
{
	namespace
	{
		std::vector<RenderHookCallback> g_postDeferredPrePass;
		std::vector<RenderHookCallback> g_preDeferredLightsImpl;
		std::vector<RenderHookCallback> g_postDeferredLightsImpl;
		std::vector<RenderHookCallback> g_postDeferredComposite;
		bool g_prePassInstalled = false;
		bool g_lightsImplInstalled = false;
		bool g_compositeInstalled = false;

		struct DeferredPrePass_Hook
		{
			static void thunk()
			{
				func();
				for (auto& cb : g_postDeferredPrePass) {
					cb();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferredLightsImpl_Hook
		{
			static void thunk()
			{
				for (auto& cb : g_preDeferredLightsImpl) {
					cb();
				}
				func();
				for (auto& cb : g_postDeferredLightsImpl) {
					cb();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferredComposite_Hook
		{
			static void thunk()
			{
				func();
				for (auto& cb : g_postDeferredComposite) {
					cb();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void RegisterPostDeferredPrePass(RenderHookCallback callback)
	{
		g_postDeferredPrePass.push_back(std::move(callback));
		if (!g_prePassInstalled) {
			stl::detour_thunk<DeferredPrePass_Hook>(REL::ID({ 56596, 2318301, 2318301 }));
			g_prePassInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredPrePass");
		}
	}

	void RegisterPreDeferredLightsImpl(RenderHookCallback callback)
	{
		g_preDeferredLightsImpl.push_back(std::move(callback));
		if (!g_lightsImplInstalled) {
			stl::detour_thunk<DeferredLightsImpl_Hook>(REL::ID({ 1108521, 2318312, 2318312 }));
			g_lightsImplInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredLightsImpl");
		}
	}

	void RegisterPostDeferredLightsImpl(RenderHookCallback callback)
	{
		g_postDeferredLightsImpl.push_back(std::move(callback));
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
	void RegisterPostDeferredComposite(RenderHookCallback callback)
	{
		g_postDeferredComposite.push_back(std::move(callback));
		if (!g_compositeInstalled) {
			stl::detour_thunk<DeferredComposite_Hook>(REL::ID({ 728427, 2318313, 2318313 }));
			g_compositeInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredComposite");
		}
	}
}
