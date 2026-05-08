#include "RenderHooks.h"

#include "Log.h"

#include <vector>

namespace { auto* L = cs::log::Get("cs.hooks"); }

namespace cs::engine
{
	namespace
	{
		std::vector<RenderHookCallback> g_postDeferredPrePass;
		std::vector<RenderHookCallback> g_postDeferredLightsImpl;
		bool g_prePassInstalled = false;
		bool g_lightsImplInstalled = false;

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
				func();
				for (auto& cb : g_postDeferredLightsImpl) {
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

	void RegisterPostDeferredLightsImpl(RenderHookCallback callback)
	{
		g_postDeferredLightsImpl.push_back(std::move(callback));
		if (!g_lightsImplInstalled) {
			stl::detour_thunk<DeferredLightsImpl_Hook>(REL::ID({ 1108521, 2318312, 2318312 }));
			g_lightsImplInstalled = true;
			L->info("Hook installed on DrawWorld::DeferredLightsImpl");
		}
	}
}
