#include "MotionVectorFixes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include <imgui.h>

#include "Log.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.motionvectorfixes");

		using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;

		std::atomic_bool g_loadingMenuOpen{ false };

		bool IsCallSiteTargeting(std::uintptr_t a_site, std::uintptr_t a_expectedTarget)
		{
			if (!a_site || !a_expectedTarget) {
				return false;
			}
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_site);
			if (bytes[0] != 0xE8) {
				return false;
			}
			std::int32_t displacement = 0;
			std::memcpy(&displacement, bytes + 1, sizeof(displacement));
			return a_site + 5 + static_cast<std::intptr_t>(displacement) == a_expectedTarget;
		}

		void ResetPreviousWorldDownwards(RE::NiAVObject* a_object)
		{
			if (!a_object) {
				return;
			}
			if (auto* node = a_object->IsNode()) {
				for (auto& child : node->children) {
					ResetPreviousWorldDownwards(child.get());
				}
			}
			a_object->previousWorld = a_object->world;
		}

		void CacheWorldTransforms(
			RE::NiAVObject* a_object,
			std::unordered_map<RE::NiAVObject*, RE::NiTransform>& a_cache)
		{
			if (!a_object) {
				return;
			}
			if (auto* node = a_object->IsNode()) {
				for (auto& child : node->children) {
					CacheWorldTransforms(child.get(), a_cache);
				}
			}
			a_cache.try_emplace(a_object, a_object->world);
		}

		void RestorePreviousWorld(
			RE::NiAVObject* a_object,
			const std::unordered_map<RE::NiAVObject*, RE::NiTransform>& a_cache)
		{
			if (!a_object) {
				return;
			}
			if (auto* node = a_object->IsNode()) {
				for (auto& child : node->children) {
					RestorePreviousWorld(child.get(), a_cache);
				}
			}
			if (const auto it = a_cache.find(a_object); it != a_cache.end()) {
				a_object->previousWorld = it->second;
			}
		}

		class MenuOpenCloseHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuOpenCloseHandler* GetSingleton()
			{
				static MenuOpenCloseHandler instance;
				return &instance;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event.menuName == RE::LoadingMenu::MENU_NAME) {
					g_loadingMenuOpen.store(a_event.opening, std::memory_order_relaxed);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		struct TESObjectREFR_SetSequencePosition
		{
			static void thunk(RE::NiAVObject* a_object, RE::NiUpdateData* a_updateData)
			{
				func(a_object, a_updateData);
				ResetPreviousWorldDownwards(a_object);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct OnIdle_UpdatePlayer
		{
			static void thunk(RE::Main* a_main)
			{
				thread_local std::unordered_map<RE::NiAVObject*, RE::NiTransform> transforms;
				transforms.clear();

				auto* player = RE::PlayerCharacter::GetSingleton();
				CacheWorldTransforms(player ? player->Get3D(false) : nullptr, transforms);

				func(a_main);

				player = RE::PlayerCharacter::GetSingleton();
				RestorePreviousWorld(player ? player->Get3D(false) : nullptr, transforms);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSLightingShaderProperty_GetRenderPasses
		{
			static RE::BSShaderProperty::RenderPassArray* thunk(
				RE::BSLightingShaderProperty* a_property,
				RE::BSGeometry* a_geometry,
				std::uint32_t a_renderMode,
				RE::BSShaderAccumulator* a_accumulator)
			{
				auto* main = RE::Main::GetSingleton();
				const bool frozen =
					main && main->gameActive && (main->inMenuMode || main->freezeTime);
				const bool lod =
					a_property && a_property->flags.any(
						ShaderFlag::kLODObjects,
						ShaderFlag::kLODLandscape,
						ShaderFlag::kLODLandBlend,
						ShaderFlag::kMultiTextureLandscape);
				if (a_geometry && !g_loadingMenuOpen.load(std::memory_order_relaxed) &&
					(frozen || lod)) {
					a_geometry->previousWorld = a_geometry->world;
				}
				return func(a_property, a_geometry, a_renderMode, a_accumulator);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	MotionVectorFixes* MotionVectorFixes::GetSingleton()
	{
		static MotionVectorFixes instance;
		return &instance;
	}

	void MotionVectorFixes::Load()
	{
		stl::detour_thunk<OnIdle_UpdatePlayer>(
			REL::ID({ 1318162, 2228929, 2228929 }));
		_playerUpdateHooked = true;

		// OG's callsite is unproven; NG and AE target NiAVObject::Update.
		constexpr std::ptrdiff_t offsets[] = { 0x1D7, 0x1D7, 0x1D7 };
		const auto runtimeIndex = static_cast<std::size_t>(REX::FModule::GetRuntimeIndex());
		const auto site =
			REL::ID({ 854236, 2200766, 2200766 }).address() + offsets[runtimeIndex];
		const auto expectedTarget = runtimeIndex == 0
			? 0
			: REL::ID({ 0, 2270101, 2270101 }).address();

		if (!expectedTarget) {
			L->warn("SetSequencePosition target is unproven on OG; skipping its correction");
		} else if (!IsCallSiteTargeting(site, expectedTarget)) {
			L->warn(
				"SetSequencePosition call target mismatch at {:#x}; skipping its correction",
				site);
		} else {
			stl::write_thunk_call<TESObjectREFR_SetSequencePosition>(site);
			_setSequencePositionHooked = true;
		}

		stl::write_vfunc<43, BSLightingShaderProperty_GetRenderPasses>(
			RE::VTABLE::BSLightingShaderProperty[0]);
		L->info("Installed previous-transform corrections");
	}

	void MotionVectorFixes::OnDataLoaded()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			L->warn("Menu event source is unavailable");
			return;
		}
		ui->RegisterSink<RE::MenuOpenCloseEvent>(MenuOpenCloseHandler::GetSingleton());
		_menuSinkRegistered = true;
	}

	void MotionVectorFixes::DrawSettings()
	{
		ImGui::TextUnformatted("Active. No user-tunable options.");
		ImGui::TextDisabled("Corrects player, animated-object, frozen/menu, and LOD previous transforms.");
	}

	void MotionVectorFixes::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("player_update_hook", _playerUpdateHooked)
			.Field("set_sequence_position_hook", _setSequencePositionHooked)
			.Field("menu_sink_registered", _menuSinkRegistered)
			.Field("loading_menu", g_loadingMenuOpen.load(std::memory_order_relaxed));
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(MotionVectorFixes::GetSingleton());
			}
		};
		static AutoRegister autoRegister;
	}
}
