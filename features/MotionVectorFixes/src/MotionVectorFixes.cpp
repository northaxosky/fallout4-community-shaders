#include "MotionVectorFixes.h"

#include <atomic>
#include <cstdint>
#include <unordered_map>

#include <DearModdingUI/Client.h>

#include "Log.h"
#include "Render/EngineCallSite.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/FrameState.h"
#include "Render/LightingRenderPasses.h"
#include "Render/SharedDataLayout.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.motionvectorfixes");

		using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;

		std::atomic_uint64_t g_corrections{ 0 };

		bool Enabled()
		{
			return MotionVectorFixes::GetSingleton()->IsEnabled();
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

		struct TESObjectREFR_SetSequencePosition
		{
			static void thunk(RE::NiAVObject* a_object, RE::NiUpdateData* a_updateData)
			{
				func(a_object, a_updateData);
				if (Enabled()) {
					ResetPreviousWorldDownwards(a_object);
					g_corrections.fetch_add(1, std::memory_order_relaxed);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct OnIdle_UpdatePlayer
		{
			static void thunk(RE::Main* a_main)
			{
				if (!Enabled()) {
					func(a_main);
					return;
				}

				thread_local std::unordered_map<RE::NiAVObject*, RE::NiTransform> transforms;
				transforms.clear();

				auto* player = RE::PlayerCharacter::GetSingleton();
				CacheWorldTransforms(player ? player->Get3D(false) : nullptr, transforms);

				func(a_main);

				player = RE::PlayerCharacter::GetSingleton();
				RestorePreviousWorld(player ? player->Get3D(false) : nullptr, transforms);
				g_corrections.fetch_add(1, std::memory_order_relaxed);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void CorrectPreviousWorld(const cs::engine::LightingRenderPassesCall& a_call) noexcept
		{
			if (!Enabled())
				return;
			const bool frozen = cs::render::GetFrameState().worldFrozen;
			const bool loading = cs::render::IsAnyMenuOpen({ cs::render::TrackedMenu::kLoading });
			const bool lod =
				a_call.property && a_call.property->flags.any(
									   ShaderFlag::kLODObjects,
									   ShaderFlag::kLODLandscape,
									   ShaderFlag::kLODLandBlend,
									   ShaderFlag::kMultiTextureLandscape);
			if (a_call.geometry && !loading &&
				(frozen || lod)) {
				a_call.geometry->previousWorld = a_call.geometry->world;
				g_corrections.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	MotionVectorFixes* MotionVectorFixes::GetSingleton()
	{
		static MotionVectorFixes instance;
		return &instance;
	}

	void MotionVectorFixes::Load()
	{
		stl::detour_thunk<OnIdle_UpdatePlayer>(
			RE::ID::Main::OnIdle_UpdatePlayer);
		_playerUpdateHooked = true;

		constexpr cs::engine::CallSiteAnchor kSetSequencePositionUpdate{
			.name = "TESObjectREFR::SetSequencePosition -> NiAVObject::Update",
			.function = RE::ID::TESObjectREFR::SetSequencePosition,
			.offset = { 0x1D7, 0x1D7, 0x1D7 },
			.target = RE::ID::NiAVObject::Update
		};
		if (const auto site = cs::engine::ResolveCallSite(kSetSequencePositionUpdate)) {
			stl::write_thunk_call<TESObjectREFR_SetSequencePosition>(*site);
			_setSequencePositionHooked = true;
		} else {
			L->warn("Animated-object correction unavailable: {}", site.error());
		}

		if (!cs::engine::RegisterPreLightingRenderPasses(*this, &CorrectPreviousWorld)) {
			FailLoad("Motion Vector Fixes pass-list hook registration failed.");
			return;
		}
		if (!cs::engine::RegisterFeatureShaderBindings(*this)) {
			FailLoad("Motion Vector Fixes shader contribution registration failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
		L->info("Installed previous-transform corrections");
	}

	void MotionVectorFixes::WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4)
	{
		a_fo4.EnabledMotionVectorFixes = 1u;
	}

	void MotionVectorFixes::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("player_update_hook", _playerUpdateHooked)
			.Field("set_sequence_position_hook", _setSequencePositionHooked)
			.Field("loading_menu", cs::render::IsAnyMenuOpen({ cs::render::TrackedMenu::kLoading }))
			.Field("corrections", g_corrections.load(std::memory_order_relaxed));
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
