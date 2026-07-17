#include "MotionVectorFixes.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include <imgui.h>

#include "Log.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.motionvectorfixes"); }

	// Local mirror until Dear-Modding-FO4/commonlibf4 exposes BSShaderProperty::flags.
	struct __declspec(novtable) BSShaderProperty : public RE::NiShadeProperty
	{
	private:
		static constexpr auto BIT64 = static_cast<std::uint64_t>(1);

	public:
		static constexpr auto RTTI{ RE::RTTI::BSShaderProperty };
		static constexpr auto VTABLE{ RE::VTABLE::BSShaderProperty };
		static constexpr auto Ni_RTTI{ RE::Ni_RTTI::BSShaderProperty };

		enum class EShaderPropertyFlag : std::uint64_t
		{
			kLODObjects = BIT64 << 34,
			kLODLandscape = BIT64 << 33,
			kLODLandBlend = BIT64 << 46,
			kMultiTextureLandscape = BIT64 << 14,
		};

		class RenderPassArray
		{
		public:
			constexpr RenderPassArray() noexcept {}
			RE::BSRenderPass* passList{ nullptr };
		};
		static_assert(sizeof(RenderPassArray) == 0x8);

		float                          alpha;                  // 28
		std::int32_t                   lastRenderPassState;    // 2C
		std::uint64_t                  flags;                  // 30
		RenderPassArray                renderPassList;         // 38
		RenderPassArray                debugRenderPassList;    // 40
		RE::BSFadeNode*                fadeNode;               // 48
		RE::BSEffectShaderData*        effectData;             // 50
		RE::BSShaderMaterial*          material;               // 58
		std::uint32_t                  lastAccumTime;          // 60
		float                          lodFade;                // 64
		RE::BSNonReentrantSpinLock     clearRenderPassesLock;  // 68
	};
	static_assert(sizeof(BSShaderProperty) == 0x70);

	static constexpr std::uint64_t kLODMask =
		static_cast<std::uint64_t>(BSShaderProperty::EShaderPropertyFlag::kLODObjects) |
		static_cast<std::uint64_t>(BSShaderProperty::EShaderPropertyFlag::kLODLandscape) |
		static_cast<std::uint64_t>(BSShaderProperty::EShaderPropertyFlag::kLODLandBlend) |
		static_cast<std::uint64_t>(BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape);

	// UI event thread updates this flag; render hooks only need a best-effort loading-menu gate.
	static std::atomic_bool g_isLoadingMenuOpen{ false };

	class MenuOpenCloseHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static MenuOpenCloseHandler* GetSingleton()
		{
			static MenuOpenCloseHandler singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent& a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.menuName == "LoadingMenu")
				g_isLoadingMenuOpen.store(a_event.opening, std::memory_order_relaxed);
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	static void ResetPreviousWorldDownwards(RE::NiAVObject* a_node)
	{
		if (!a_node)
			return;
		if (auto* node = a_node->IsNode())
			for (auto& child : node->children)
				ResetPreviousWorldDownwards(child.get());
		a_node->previousWorld = a_node->world;
	}

	static void CacheWorldTransforms(
		RE::NiAVObject* a_node,
		std::unordered_map<RE::NiAVObject*, RE::NiTransform>& a_cache)
	{
		if (!a_node)
			return;
		if (auto* node = a_node->IsNode())
			for (auto& child : node->children)
				CacheWorldTransforms(child.get(), a_cache);
		a_cache.try_emplace(a_node, a_node->world);
	}

	static void RestorePreviousWorld(
		RE::NiAVObject* a_node,
		std::unordered_map<RE::NiAVObject*, RE::NiTransform>& a_cache)
	{
		if (!a_node)
			return;
		if (auto* node = a_node->IsNode())
			for (auto& child : node->children)
				RestorePreviousWorld(child.get(), a_cache);
		if (auto it = a_cache.find(a_node); it != a_cache.end())
			a_node->previousWorld = it->second;
	}

	struct TESObjectREFR_SetSequencePosition
	{
		static void thunk(RE::NiAVObject* a_this, RE::NiUpdateData* a_updateData)
		{
			func(a_this, a_updateData);
			ResetPreviousWorldDownwards(a_this);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct OnIdle_UpdatePlayer
	{
		static void thunk(RE::Main* a_this)
		{
			std::unordered_map<RE::NiAVObject*, RE::NiTransform> playerWorldCache;

			if (auto* player = RE::PlayerCharacter::GetSingleton())
				CacheWorldTransforms(player->Get3D(0), playerWorldCache);

			func(a_this);

			if (auto* player = RE::PlayerCharacter::GetSingleton())
				RestorePreviousWorld(player->Get3D(0), playerWorldCache);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShaderProperty_GetRenderPasses
	{
		static BSShaderProperty::RenderPassArray* thunk(
			BSShaderProperty* a_this,
			RE::NiAVObject* a_geometry,
			int a3,
			RE::ShadowSceneNode** a4)
		{
			thread_local static auto main = RE::Main::GetSingleton();

			bool frozenTime = main->gameActive && (main->inMenuMode || main->freezeTime);
			bool lodObject = (a_this->flags & kLODMask) != 0;

			if (!g_isLoadingMenuOpen.load(std::memory_order_relaxed) && (frozenTime || lodObject))
				a_geometry->previousWorld = a_geometry->world;

			return func(a_this, a_geometry, a3, a4);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	MotionVectorFixes* MotionVectorFixes::GetSingleton()
	{
		static MotionVectorFixes instance;
		return &instance;
	}

	void MotionVectorFixes::Load()
	{
		// Preserve weapon-model previousWorld across player idle updates.
		stl::detour_thunk<OnIdle_UpdatePlayer>(REL::ID({ 1318162, 2228929, 2228929 }));

		// Fallout4RE cs-mvf-setsequenceposition-call.json, 2026-05-23: OG/NG/AE offset 0x1D7 is the direct E8 NiAVObject::Update call in SetSequencePosition.
		{
			constexpr std::ptrdiff_t offsets[] = { 0x1D7, 0x1D7, 0x1D7 };
			const auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
			assert(runtimeIdx < 3);
			const auto setSeqAddr = REL::ID({ 854236, 2200766, 2200766 }).address() + offsets[runtimeIdx];
			if (*reinterpret_cast<const std::uint8_t*>(setSeqAddr) != 0xE8) {
				L->warn("SetSequencePosition call-site opcode mismatch at {:#x}; skipping hook", setSeqAddr);
			} else {
				stl::write_thunk_call<TESObjectREFR_SetSequencePosition>(setSeqAddr);
			}
		}

		// Keep previousWorld current while menus or frozen time stop vanilla motion-vector updates.
		stl::write_vfunc<43, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE::BSLightingShaderProperty[0]);

		L->info("Installed hooks");
	}

	void MotionVectorFixes::OnDataLoaded()
	{
		RE::UI::GetSingleton()->RegisterSink<RE::MenuOpenCloseEvent>(MenuOpenCloseHandler::GetSingleton());
		L->info("Registered menu event listener");
	}

	void MotionVectorFixes::DrawSettings()
	{
		ImGui::TextUnformatted("Active. No user-tunable options.");
		ImGui::TextDisabled("Fixes weapon-idle motion vectors, animated-object previousWorld,");
		ImGui::TextDisabled("and motion vectors during menus / time freeze.");
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
		static AutoRegister _autoRegister;
	}
}
