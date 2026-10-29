#include "Render/TerrainWater.h"
#include "Render/EngineCallSite.h"

#include "Log.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSGeometrySegmentData.h"
#include "RE/B/BSPrecomputedVisibility.h"
#include "RE/B/BSSpinLock.h"
#include "RE/B/BSWaterShaderMaterial.h"
#include "RE/B/BSWaterShaderProperty.h"
#include "RE/N/NiUpdateData.h"
#include "RE/T/TES.h"
#include "RE/T/TESFileArray.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWaterForm.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <limits>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace cs::engine::water
{
	namespace
	{
		bool ActiveCell(RE::TESObjectCELL* a_cell)
		{
			const auto visibility = RE::BSPrecomputedVisibility::MultiCellVisibilityData::GetSingleton();
			return a_cell && RE::TES::GetSingleton()->IsCellLoaded(a_cell, true) &&
			       visibility && visibility->IsCellVisibilityActive(a_cell->GetFormID());
		}

		void UpdateSegments(RE::BSGeometrySegmentData* a_data, const RE::BSScrapArray<RE::TESObjectCELL*>& a_cells)
		{
			const auto count = a_data->GetSegmentCount();
			for (std::uint32_t i = 0; i < count; ++i) {
				constexpr auto allSubsegments = std::numeric_limits<std::uint8_t>::max();
				const bool hide = !a_data->IsEmpty(i, allSubsegments) && i < a_cells.size() && ActiveCell(a_cells[i]);
				if (a_data->IsEnabled(i, allSubsegments) == hide) {
					if (hide)
						a_data->DisableSegment(i, allSubsegments, false);
					else
						a_data->EnableSegment(i, allSubsegments, false);
				}
			}
			a_data->UpdateDrawData();
		}

		bool SeekChunk(RE::TESFile* a_file, std::uint32_t a_type)
		{
			for (auto type = a_file->GetTESChunk(); type; type = a_file->GetTESChunk()) {
				if (type == a_type)
					return true;
				if (!a_file->NextChunk())
					break;
			}
			return false;
		}

		bool ReadChunk(RE::TESFile* a_file, void* a_data, std::uint32_t a_size)
		{
			// FO4: undersized buffers silently return truncated records.
			return a_file->actualChunkSize == a_size && a_file->GetChunkData(a_data, a_size);
		}
	}

	namespace
	{
		thread_local std::span<const TileWater> t_tiles;
		std::atomic_uint64_t g_overrides{ 0 };

		struct LODWaterAddWater
		{
			static bool thunk(RE::TESWaterSystem* a_system, RE::NiAVObject* a_geometry, RE::TESWaterForm* a_form, float a_height,
				RE::BSTArray<RE::NiPointer<RE::BSMultiBoundAABB>>* a_bounds, bool a_forceNonHorizontal, bool a_addRipples)
			{
				// FO4: AddLODWater passes the worldspace WATR; tiles need their cell's.
				if (const auto tile = std::ranges::find(t_tiles, a_geometry, &TileWater::geometry); tile != t_tiles.end()) {
					if (tile->form != a_form)
						g_overrides.fetch_add(1, std::memory_order_relaxed);
					a_form = tile->form;
					a_height = tile->height;
				}
				return func(a_system, a_geometry, a_form, a_height, a_bounds, a_forceNonHorizontal, a_addRipples);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	namespace
	{
		// Vanilla CRC omits normal texture names, so forms could share normals.
		thread_local std::uint32_t t_textureHash = 0;
		thread_local bool t_hasTextureHash = false;

		struct AddWaterUpdateMaterial
		{
			static void thunk(RE::TESWaterForm* a_form, RE::BSWaterShaderMaterial* a_material)
			{
				t_hasTextureHash = false;
				func(a_form, a_material);
				std::uint32_t hash = 2166136261u;
				// FO4: WATR carries three noise textures, Skyrim four.
				for (const auto* texture : { &a_form->noiseTexture0, &a_form->noiseTexture1, &a_form->noiseTexture2 }) {
					for (const auto c : std::string_view(texture->textureName.c_str())) {
						hash ^= static_cast<unsigned char>(c);
						hash *= 16777619u;
					}
				}
				t_textureHash = hash;
				t_hasTextureHash = true;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct WaterMaterialCRC
		{
			static std::uint32_t thunk(RE::BSWaterShaderMaterial* a_this, std::uint32_t a_srcHash, bool a_arg)
			{
				if (t_hasTextureHash) {
					a_srcHash ^= t_textureHash + (a_srcHash << 6) + (a_srcHash >> 2);
					t_hasTextureHash = false;
				}
				return func(a_this, a_srcHash, a_arg);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	bool InstallWaterMaterialIdentity()
	{
		static REL::Relocation<std::uintptr_t> addWater{ RE::ID::TESWaterSystem::AddWater };
		const auto site = addWater.address() + (REX::FModule::IsRuntimeOG() ? 0x377 : 0x433);
		if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8)
			return false;
		stl::write_thunk_call<AddWaterUpdateMaterial>(site);
		// FO4: ComputeCRC32 is vtable slot 3, Skyrim slot 4.
		stl::write_vfunc<3, WaterMaterialCRC>(RE::VTABLE::BSWaterShaderMaterial[0]);
		return true;
	}

	bool InstallTileWaterOverride()
	{
		const std::array anchors{
			CallSiteAnchor{ "AddLODWater AddWater", RE::ID::TESWaterSystem::AddLODWater,
				{ 0xC8, 0x139, 0x139 }, RE::ID::TESWaterSystem::AddWater },
			CallSiteAnchor{ "AddLODWater alternate AddWater", RE::ID::TESWaterSystem::AddLODWater,
				{ 0x106, 0, 0 }, RE::ID::TESWaterSystem::AddWater }
		};
		std::vector<std::uintptr_t> sites;
		for (std::size_t i = 0; i < (REX::FModule::IsRuntimeOG() ? 2u : 1u); ++i) {
			const auto site = ResolveCallSite(anchors[i]);
			if (!site) {
				cs::log::Get("cs.render.terrainwater")->error("{}", site.error());
				return false;
			}
			sites.push_back(*site);
		}
		for (const auto site : sites)
			stl::write_thunk_call<LODWaterAddWater>(site);
		return true;
	}

	std::uint64_t TileWaterOverrides() noexcept
	{
		return g_overrides.load(std::memory_order_relaxed);
	}

	void RegisterTileWater(TerrainChunk* a_chunk, std::span<const TileWater> a_tiles)
	{
		const auto flags = [](const TileWater& a_tile) -> std::uint32_t* {
			const auto geometry = a_tile.geometry ? a_tile.geometry->IsGeometry() : nullptr;
			const auto property = geometry ? geometry->properties[1].get() : nullptr;
			return property ? &static_cast<RE::BSWaterShaderProperty*>(property)->waterFlags : nullptr;
		};
		// FO4: AddWater reads Prepare's LOD mask 0x2; restore it to re-register.
		for (const auto& tile : a_tiles)
			if (const auto value = flags(tile))
				*value |= 0x2u;
		const auto previous = std::exchange(t_tiles, a_tiles);
		RegisterVanillaWater(a_chunk);
		t_tiles = previous;
		// FO4: preserve native reflections; no CUBEMAP technique exists.
		for (const auto& tile : a_tiles)
			if (const auto value = flags(tile))
				*value = (*value & ~0x2u) | 0xC4u;
	}

	RE::NiNode* GetWaterLOD()
	{
		return RE::BGSTerrainManager::GetWaterNode();
	}

	bool DisableVanillaWaterLOD()
	{
		// Attach owns terrain water registration.
		static REL::Relocation<std::uintptr_t> attach{ RE::ID::BGSTerrainChunk::Attach };
		static REL::Relocation<std::uintptr_t> registration{ RE::ID::BGSBtrDB::WaterRegistration };
		const std::array addresses{ attach.address() + 0x103, registration.address() + (REX::FModule::IsRuntimeOG() ? 0x67 : 0x10B) };
		const std::array<std::array<std::uint8_t, 6>, 2> expected = REX::FModule::IsRuntimeOG() ?
		                                                                std::array{ std::array<std::uint8_t, 6>{ 0x0F, 0x83, 0xC0, 0, 0, 0 }, std::array<std::uint8_t, 6>{ 0x0F, 0x83, 0x91, 0, 0, 0 } } :
		                                                                std::array{ std::array<std::uint8_t, 6>{ 0x0F, 0x83, 0xBC, 0, 0, 0 }, std::array<std::uint8_t, 6>{ 0x0F, 0x86, 0xA6, 0, 0, 0 } };
		for (std::size_t i = 0; i < addresses.size(); ++i)
			if (std::memcmp(reinterpret_cast<const void*>(addresses[i]), expected[i].data(), 6) != 0)
				return false;
		for (std::size_t i = 0; i < addresses.size(); ++i) {
			constexpr std::uint8_t patch[]{ 0x90, 0xE9 };
			if (!WriteVerifiedPatch(addresses[i], patch)) {
				for (std::size_t j = 0; j <= i; ++j)
					(void)WriteVerifiedPatch(addresses[j], std::span(expected[j]).first(sizeof(patch)));
				return false;
			}
		}
		return true;
	}

	void Prepare(TerrainChunk* a_chunk)
	{
		a_chunk->Prepare();
	}

	void RegisterVanillaWater(TerrainChunk* a_chunk)
	{
		const auto root = GetWaterLOD();
		const auto system = RE::TESWaterSystem::GetSingleton();
		if (!a_chunk->water || !root || !system)
			return;
		for (const auto& child : a_chunk->water->children) {
			if (!child)
				continue;
			if (auto geometry = child->IsGeometry(); geometry && geometry->GetSegmentData())
				geometry->GetSegmentData()->EnableAllSegments();
			system->AddLODWater(child.get(), a_chunk->node->manager->world, root, a_chunk->water.get(), true);
		}
		RE::NiUpdateData update{};
		root->Update(update);
	}

	void UnregisterVanillaWater(TerrainChunk* a_chunk)
	{
		const auto root = GetWaterLOD();
		const auto system = RE::TESWaterSystem::GetSingleton();
		if (!a_chunk->water || !system)
			return;
		for (const auto& child : a_chunk->water->children) {
			if (!child)
				continue;
			if (root)
				system->RemoveLODWater(child.get(), root, a_chunk->water.get());
			else
				RemoveWater(child.get());
		}
		// FO4: RemoveLODWater may leave the water node under the LOD root.
		if (root)
			root->DetachChild(a_chunk->water.get());
	}

	void RemoveWater(RE::NiAVObject* a_shape)
	{
		if (const auto system = RE::TESWaterSystem::GetSingleton(); system && a_shape)
			system->RemoveWater(a_shape);
	}

	void UpdateVisibility(TerrainNode* a_node, RE::NiNode* a_parent, std::span<const TileCell> a_tiles, bool a_hideTiles)
	{
		if (!a_node || !a_parent)
			return;
		const auto tes = RE::TES::GetSingleton();
		const bool intersects = tes && !tes->interiorCell && a_node->IsInLoadedRange();
		RE::BSScrapArray<RE::TESObjectCELL*> cells;
		if (intersects) {
			a_node->BuildCellCache(cells);
		}
		for (const auto& child : a_parent->children) {
			if (!child)
				continue;
			if (auto geometry = child->IsGeometry(); geometry && geometry->GetSegmentData()) {
				if (intersects)
					UpdateSegments(geometry->GetSegmentData(), cells);
				else
					geometry->GetSegmentData()->EnableAllSegments();
				continue;
			}
			const auto tile = std::ranges::find_if(a_tiles, [&](const auto& item) { return item.geometry == child.get(); });
			if (tile == a_tiles.end())
				continue;
			const auto x = tile->x - a_node->baseCellX;
			const auto y = tile->y - a_node->baseCellY;
			const auto level = static_cast<std::int32_t>(a_node->GetLODLevel());
			// FO4: native BuildCellCache orders the borrowed cells x-major.
			const auto index = x * level + y;
			// FO4: the engine tests each geometry's own cull bit, not the root's.
			const bool hide = a_hideTiles || (intersects && x >= 0 && y >= 0 && x < level && y < level &&
												 index >= 0 && static_cast<std::size_t>(index) < cells.size() && ActiveCell(cells[static_cast<std::uint32_t>(index)]) && cells[static_cast<std::uint32_t>(index)]->HasWater());
			child->SetAppCulled(hide);
		}
	}

	bool ReadCellData(RE::TESWorldSpace* a_world, std::int32_t a_x, std::int32_t a_y,
		RE::TESFormID& a_form, float& a_water, float& a_land)
	{
		static std::mutex cursorMutex;
		const std::scoped_lock lock(cursorMutex);
		a_form = 0;
		a_water = FLT_MAX;
		a_land = FLT_MAX;
		const auto files = a_world->sourceFiles.array;
		if (!files || files->empty())
			return false;
		std::int32_t index = static_cast<std::int32_t>(files->size()) - 1;
		// Search files in reverse load order so the winning CELL record is read
		RE::TESFile* file = nullptr;
		for (; index >= 0; --index) {
			file = (*files)[index]->GetThreadSafeFile(16 * 1024);
			if (file && a_world->FindCellInFile(file, a_x, a_y))
				break;
		}
		if (index < 0)
			return false;
		constexpr auto xclw = 0x574C4358u;
		constexpr auto xcwt = 0x54574358u;
		if (SeekChunk(file, xclw) && !ReadChunk(file, &a_water, 4))
			return false;
		if (SeekChunk(file, xcwt)) {
			if (!ReadChunk(file, &a_form, 4))
				return false;
			if (a_form)
				RE::TESForm::AddCompileIndex(a_form, file);
		}
		// Continue from that file to the original record, which has the LAND
		for (; index >= 0; --index) {
			file = (*files)[index]->GetThreadSafeFile(16 * 1024);
			if (file && a_world->FindCellInFile(file, a_x, a_y) && RE::TESObjectCELL::FindLandDataInFile(file))
				break;
		}
		if (index < 0)
			return false;
		if (SeekChunk(file, 0x54474856u)) {
			struct VHGT
			{
				float offset;
				std::int8_t deltas[1089];
				std::byte padding[3];
			} data{};
			static_assert(sizeof(data) == 1096);
			if (!ReadChunk(file, &data, sizeof(data)))
				return false;
			float offset = data.offset;
			for (std::size_t y = 0; y < 33; ++y) {
				float rowOffset = 0;
				offset += static_cast<float>(data.deltas[y * 33]);
				for (std::size_t x = 0; x < 33; ++x) {
					if (x != 0)
						rowOffset += static_cast<float>(data.deltas[y * 33 + x]);
					a_land = std::min(a_land, (rowOffset + offset) * 8.0f);
				}
			}
		}
		if (a_water == FLT_MAX)
			a_water = a_world->defaultWaterHeight;
		if (a_land == FLT_MAX)
			a_land = a_world->defaultLandHeight;
		return true;
	}
}
