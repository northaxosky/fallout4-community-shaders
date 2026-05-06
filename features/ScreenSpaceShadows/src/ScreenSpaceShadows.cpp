#include "ScreenSpaceShadows.h"

#include <imgui.h>

#include "Log.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.sss"); }

	// Phase 1 scaffold: feature loads as a no-op so we can land the registration plumbing
	// independently of REL::ID wiring and the Bend SSS dispatch (Phase 2). The DrawWorld
	// hook targets we'll need in Phase 2 are documented here for traceability.
	//
	// Mangled symbols (from Fallout4RE/Targets/NamePorts/OG_to_AE_1.11.191_IDA/fallout4_og_funcs.json):
	//   ?DeferredPrePass@DrawWorld@@YAXXZ                  OG VA 0x14266CDB0  (start: 5410983088)
	//   ?DrawPointLight@DrawWorld@@YAXPEAVBSLight@@...     OG VA TBD          (start: ~5410986000)
	//   ?DeferredLightsImpl@DrawWorld@@YAXXZ               OG VA TBD          (start: ~5410993000)
	//   ?DeferredComposite@DrawWorld@@YAXXZ                OG VA TBD          (start: ~5410993500)
	//
	// NG/AE REL::IDs are needed before Phase 2 can install hooks; see the version-aware
	// ID database under Data/F4SE/Plugins/versionlib-*.bin.

	ScreenSpaceShadows* ScreenSpaceShadows::GetSingleton()
	{
		static ScreenSpaceShadows instance;
		return &instance;
	}

	void ScreenSpaceShadows::Load()
	{
		L->info("Loaded (Phase 1 scaffold; no hooks installed yet)");
	}

	void ScreenSpaceShadows::DrawSettings()
	{
		ImGui::TextDisabled("Phase 1 scaffold. Hooks land in Phase 2 alongside the Bend SSS compute pipeline.");
		ImGui::TextDisabled("See plan: ~/.claude/plans/1-let-s-use-end-giggly-valiant.md");
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(ScreenSpaceShadows::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
