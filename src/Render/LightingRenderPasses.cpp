#include "Render/LightingRenderPasses.h"

#include "Log.h"
#include "Render/HookRegistry.h"
#include "Render/RuntimeCallback.h"

#include "RE/B/BSShaderProperty.h"

#include <algorithm>
#include <vector>

namespace
{
	auto* L = cs::log::Get("cs.hooks");
}

namespace cs::engine
{
	namespace
	{
		template <class Callback>
		struct Entry
		{
			HookPriority priority;
			Callback callback;
			Feature* owner;
		};

		std::vector<Entry<PreLightingRenderPassesCallback>> g_pre;
		bool g_installed = false;

		struct BSLightingShaderProperty_GetRenderPasses
		{
			static RE::BSShaderProperty::RenderPassArray* thunk(
				RE::BSLightingShaderProperty* a_property,
				RE::BSGeometry* a_geometry,
				std::uint32_t a_renderMode,
				RE::BSShaderAccumulator* a_accumulator)
			{
				const LightingRenderPassesCall call{ a_property, a_geometry, a_renderMode, a_accumulator };
				for (const auto& entry : g_pre)
					render::RunRenderCallback(entry.owner, "PreLightingRenderPasses", [&] { entry.callback(call); });
				return func(a_property, a_geometry, a_renderMode, a_accumulator);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		bool EnsureInstalled()
		{
			if (g_installed)
				return true;
			constexpr std::size_t kGetRenderPassesVtableSlot = 43;
			stl::write_vfunc<kGetRenderPassesVtableSlot, BSLightingShaderProperty_GetRenderPasses>(
				RE::VTABLE::BSLightingShaderProperty[0]);
			if (BSLightingShaderProperty_GetRenderPasses::func.address() == 0) {
				L->error("Hook failed on BSLightingShaderProperty::GetRenderPasses");
				return false;
			}
			g_installed = true;
			L->info("Hook installed on BSLightingShaderProperty::GetRenderPasses (vtable slot {})", kGetRenderPassesVtableSlot);
			return true;
		}
	}

	static bool RegisterLightingRenderPasses(PreLightingRenderPassesCallback a_callback, HookPriority a_priority, Feature* a_owner)
	{
		if (!RenderHookRegistrationAllowed("PreLightingRenderPasses") || !EnsureInstalled())
			return false;
		InsertByPriority(g_pre, Entry<PreLightingRenderPassesCallback>{ a_priority, a_callback, a_owner });
		return true;
	}

	bool RegisterPreLightingRenderPasses(PreLightingRenderPassesCallback a_callback, HookPriority a_priority)
	{
		return RegisterLightingRenderPasses(a_callback, a_priority, nullptr);
	}

	bool RegisterPreLightingRenderPasses(Feature& a_owner, PreLightingRenderPassesCallback a_callback, HookPriority a_priority)
	{
		return RegisterLightingRenderPasses(a_callback, a_priority, &a_owner);
	}
}
