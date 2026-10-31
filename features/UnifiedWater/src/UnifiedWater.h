#pragma once

#include "Feature.h"
#include "RE/B/BSTriShape.h"
#include "Render/TerrainWater.h"
#include "ShaderDefines.h"
#include "WaterCache.h"

#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace cs::features
{
	class UnifiedWater : public ShaderFeature<unified_water::kShaderDefines>
	{
	public:
		static UnifiedWater* GetSingleton();
		std::string_view GetName() const override { return "UnifiedWater"; }
		std::string_view GetDisplayName() const override { return "Unified Water"; }
		std::string GetCategory() const override { return FeatureCategories::kWater; }
		std::string GetFeatureSummary() const override
		{
			return "Replaces distant terrain water tiles with close water and unifies their optical distance response. Placed water retains its vanilla distance rendering.";
		}
		void Load() override;
		void WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4) override;
		void OnPostPostLoad() override;
		void OnDataLoaded() override;
		void DrawSettings() override;
		void DrawOverlay() override;
		bool IsOverlayActive() const override;
		bool ValidateShaderInjections(std::string& a_error) override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(telemetry::Sink& a_sink) const override;

	private:
		using Chunk = engine::water::TerrainChunk;
		using Node = engine::water::TerrainNode;
		struct ManagedWater
		{
			std::vector<RE::NiPointer<RE::NiAVObject>> original;
			std::vector<engine::water::TileCell> tiles;
			std::vector<engine::water::TileWater> water;
			std::uint32_t level{};
		};
		struct BuiltTile
		{
			RE::NiPointer<RE::BSTriShape> shape;
			const unified_water::Instruction* instruction;
			bool merged;
		};
		struct Attach
		{
			static void thunk(Chunk* a_chunk);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct Detach
		{
			static void thunk(Chunk* a_chunk);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct AttachWater
		{
			static void thunk(bool a_attach);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct Visibility
		{
			static void thunk(Node* a_node, RE::NiNode* a_parent);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct SetWorldSpace
		{
			static void thunk(RE::TES* a_tes, RE::TESWorldSpace* a_world, bool a_exterior);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct ReleaseWorld
		{
			static void thunk(RE::TESWorldSpace* a_world);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct UpdateWaterSystem
		{
			static void thunk(engine::water::WaterSystem* a_system, RE::BSCullingProcess* a_process);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		void ApplyEnabledState();
		void RegisterChunkWater(Chunk* a_chunk);
		bool TilesHidden() const;
		void RefreshTileVisibility();
		void RestoreOriginal(RE::BSMultiBoundNode* a_parent, ManagedWater& a_water);
		RE::NiPointer<RE::BSTriShape> CreateTile(const Node* a_node, RE::NiProperty* a_prepared, const unified_water::Instruction& a_instruction) const;
		RE::NiPointer<RE::BSTriShape> CreateMergedTile(const Node* a_node, RE::NiProperty* a_prepared, const unified_water::TileGroup& a_group) const;
		void CountShapes(const ManagedWater& a_water, std::int64_t a_sign);
		bool CreateTiles(const Node* a_node, RE::NiProperty* a_prepared, const std::vector<unified_water::Instruction>& a_instructions, std::vector<BuiltTile>& a_built);
		void AttachVanilla(Chunk* a_chunk, bool a_alreadyAttached = false);

		RE::NiPointer<RE::BSTriShape> _mesh;
		unified_water::WaterCache _cache;
		std::recursive_mutex _mutex;
		std::unordered_map<RE::NiNode*, ManagedWater> _managed;
		std::unordered_set<Chunk*> _attached;
		std::atomic_bool _appliedEnabled{ true };
		bool _applying = false;
		std::atomic_bool _ready{ false };
		std::atomic_bool _registrations{ false };
		std::atomic_bool _hooks{ false };
		std::atomic_bool _injections{ false };
		std::atomic_bool _exterior{ false };
		std::atomic_bool _lodWaterAttached{ true };
		std::atomic_bool _refreshTiles{ false };
		std::atomic_uint64_t _tilesBuilt{ 0 };
		std::atomic_uint64_t _chunksAttached{ 0 };
		std::atomic_uint64_t _chunksDetached{ 0 };
		std::atomic_uint64_t _vanillaFallbacks{ 0 };
		std::atomic_uint64_t _visibilityUpdates{ 0 };
		std::atomic_uint64_t _squaresMerged{ 0 };
		std::array<std::atomic_int64_t, 4> _shapesLive{};
	};
}
