#include "World/Water.h"

#include "RE/E/EXTERIOR_DATA.h"
#include "RE/G/GridCellArray.h"
#include "RE/N/NiAVObject.h"
#include "RE/S/Sky.h"
#include "RE/T/TES.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWaterForm.h"

#include <atomic>

namespace cs::engine
{
	namespace
	{
		constexpr std::size_t kWaterMultiplierColor = 14;
		std::atomic_uint32_t waterCells{ 0 };
		std::atomic<float> cameraCellHeight{ kNoWaterHeight };
		std::function<bool()> suppressRipples;

		struct WaterRippleObject
		{
			std::byte pad[0x28];
			RE::NiAVObject* geometry;
		};
		struct WaterRippleSystem
		{
			std::byte pad[0x18];
			WaterRippleObject** objects;
			std::byte capacity[8];
			std::uint32_t count;
		};
		static_assert(offsetof(WaterRippleSystem, count) == 0x28);
		static_assert(offsetof(WaterRippleObject, geometry) == 0x28);

		struct ToggleWaterRipples
		{
			static void thunk(WaterRippleSystem* a_system, bool a_enabled, float a_fade)
			{
				// FO4: preserve the logical active flag that also drives native material wetness.
				func(a_system, a_enabled, a_fade);
				const bool hidden = !a_enabled || (suppressRipples && suppressRipples());
				for (std::uint32_t index = 0; index < a_system->count; ++index) {
					auto* object = a_system->objects[index];
					if (object && object->geometry)
						object->geometry->SetAppCulled(hidden);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		float GetExteriorWaterHeight(const RE::TESObjectCELL& a_cell)
		{
			// FO4: the accessor resolves worldspace inheritance and rejects interior/no-water cells.
			using Func = float(const RE::TESObjectCELL*);
			static REL::Relocation<Func> func{ REL::ID({ 1457825, 2200267, 2200267 }) };
			return func(&a_cell);
		}

		DirectX::XMFLOAT4 CellWaterData(
			const RE::TESObjectCELL& a_cell, const RE::Sky* a_sky, float a_originZ)
		{
			DirectX::XMFLOAT4 data{ 1, 1, 1, kNoWaterHeight };
			if (const auto* water = a_cell.GetWaterType()) {
				const auto shallow = water->data.shallowWaterColor;
				const auto deep = water->data.deepWaterColor;
				data.x = WaterColorChannel(shallow, deep, 0);
				data.y = WaterColorChannel(shallow, deep, 8);
				data.z = WaterColorChannel(shallow, deep, 16);
			}
			if (a_sky) {
				const auto& multiplier = a_sky->skyColor[kWaterMultiplierColor];
				data.x *= multiplier.r;
				data.y *= multiplier.g;
				data.z *= multiplier.b;
			}
			data.w = RelativeWaterHeight(GetExteriorWaterHeight(a_cell), a_originZ);
			return data;
		}
	}

	WaterDataStatus GetWaterDataStatus() noexcept
	{
		return { waterCells.load(std::memory_order_relaxed), cameraCellHeight.load(std::memory_order_relaxed) };
	}

	void InstallWaterRippleVisibilityFilter(std::function<bool()> a_suppress)
	{
		suppressRipples = std::move(a_suppress);
		static bool installed = false;
		if (!installed) {
			stl::detour_thunk<ToggleWaterRipples>(REL::ID({ 1074671, 2213956, 2213956 }));
			installed = true;
		}
	}

	void FillWaterData(
		std::array<DirectX::XMFLOAT4, 25>& a_data,
		float& a_systemHeight,
		const DirectX::XMFLOAT3& a_cameraOrigin)
	{
		a_data.fill({ 1, 1, 1, kNoWaterHeight });
		a_systemHeight = kNoWaterHeight;
		waterCells.store(0, std::memory_order_relaxed);
		cameraCellHeight.store(kNoWaterHeight, std::memory_order_relaxed);
		const auto* tes = RE::TES::GetSingleton();
		if (!tes)
			return;
		if (tes->interiorCell) {
			a_data.fill(CellWaterData(*tes->interiorCell, tes->sky, a_cameraOrigin.z));
			return;
		}
		const auto* grid = tes->gridCells;
		if (!grid)
			return;

		std::uint32_t count = 0;
		// FO4: loaded-cell coordinates reproduce upstream's eye-centred 4096-unit queries.
		for (std::uint32_t x = 0; x < grid->dimension; ++x) {
			for (std::uint32_t y = 0; y < grid->dimension; ++y) {
				const auto* cell = grid->GetCell(x, y);
				const auto* coordinates = cell ? cell->GetCoordinates() : nullptr;
				if (!coordinates)
					continue;
				const auto tile = WaterTileIndex(
					coordinates->cellX, coordinates->cellY, a_cameraOrigin.x, a_cameraOrigin.y);
				if (tile < 0)
					continue;
				auto& data = a_data[static_cast<std::size_t>(tile)];
				data = CellWaterData(*cell, tes->sky, a_cameraOrigin.z);
				if (data.w != kNoWaterHeight) {
					++count;
					if (tile == 12) {
						// FO4: publish the camera cell plane without inventing mesh intersections.
						a_systemHeight = data.w;
						cameraCellHeight.store(data.w + a_cameraOrigin.z, std::memory_order_relaxed);
					}
				}
			}
		}
		waterCells.store(count, std::memory_order_relaxed);
	}
}
