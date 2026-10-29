#pragma once

#include "RE/B/BGSTerrainChunk.h"
#include "RE/B/BGSTerrainManager.h"
#include "RE/B/BGSTerrainNode.h"
#include "RE/B/BSMultiBoundNode.h"
#include "RE/B/BSTArray.h"
#include "RE/N/NiNode.h"
#include "RE/T/TESFile.h"
#include "RE/T/TESWaterForm.h"
#include "RE/T/TESWaterSystem.h"
#include "RE/T/TESWorldSpace.h"

#include <cstdint>
#include <span>

namespace cs::engine::water
{
	using WaterSystem = RE::TESWaterSystem;
	using TerrainNode = RE::BGSTerrainNode;
	using TerrainChunk = RE::BGSTerrainChunk;

	struct TileWater
	{
		RE::NiAVObject* geometry;
		RE::TESWaterForm* form;
		float height;
	};

	struct TileCell
	{
		RE::NiAVObject* geometry;
		std::int32_t x;
		std::int32_t y;
	};

	RE::NiNode* GetWaterLOD();
	bool DisableVanillaWaterLOD();
	void Prepare(TerrainChunk* a_chunk);
	void RegisterVanillaWater(TerrainChunk* a_chunk);
	void RegisterTileWater(TerrainChunk* a_chunk, std::span<const TileWater> a_tiles);
	bool InstallTileWaterOverride();
	bool InstallWaterMaterialIdentity();
	std::uint64_t TileWaterOverrides() noexcept;
	void UnregisterVanillaWater(TerrainChunk* a_chunk);
	void RemoveWater(RE::NiAVObject* a_shape);
	void UpdateVisibility(TerrainNode* a_node, RE::NiNode* a_parent, std::span<const TileCell> a_tiles, bool a_hideTiles);

	// FO4: read engine-owned cursors at DataLoaded, before terrain streaming.
	bool ReadCellData(RE::TESWorldSpace* a_world, std::int32_t a_x, std::int32_t a_y,
		RE::TESFormID& a_form, float& a_water, float& a_land);
}
