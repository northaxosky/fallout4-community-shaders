#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cs::features::unified_water
{
	struct Instruction
	{
		std::uint32_t lodLevel{};
		union Form
		{
			std::uint32_t id;
			void* ptr = nullptr;
		} form{};
		std::int32_t x{};
		std::int32_t y{};
		std::uint32_t size{};
		float waterHeight{};
	};
	static_assert(sizeof(Instruction) == 32);

	struct Bounds
	{
		std::int32_t minX{};
		std::int32_t minY{};
		std::int32_t maxX{};
		std::int32_t maxY{};
	};

	struct CellData
	{
		float water{};
		std::uint32_t formID{};
		std::uint8_t flags{};
	};

	struct CacheHeader
	{
		std::uint32_t label = 0x48435457;
		std::int32_t width{};
		std::int32_t height{};
		Bounds bounds{};
		std::int32_t dataCount{};
	};

	inline bool IsValidCacheHeader(const CacheHeader& a_header, std::uintmax_t a_length)
	{
		const auto& b = a_header.bounds;
		return a_header.label == CacheHeader{}.label && a_header.width > 0 && a_header.height > 0 &&
		       a_header.width <= 512 && a_header.height <= 512 && b.minX >= -512 && b.minY >= -512 &&
		       b.maxX <= 511 && b.maxY <= 511 &&
		       static_cast<std::int64_t>(b.maxX) - b.minX + 1 == a_header.width &&
		       static_cast<std::int64_t>(b.maxY) - b.minY + 1 == a_header.height &&
		       a_header.dataCount >= 0 && a_length >= sizeof(CacheHeader) &&
		       static_cast<std::uintmax_t>(a_header.dataCount) <= (a_length - sizeof(CacheHeader)) / sizeof(Instruction);
	}

	inline std::int32_t GetLODCoord(std::int32_t a_level, std::int32_t a_cell)
	{
		return a_cell >= 0 ? a_cell / a_level : (a_cell - (a_level - 1)) / a_level;
	}

	inline void GenerateInstructions(std::int32_t lodLevel, Bounds bounds,
		const std::vector<CellData>& cellData, std::vector<Instruction>& instructions)
	{
		const auto [minX, minY, maxX, maxY] = bounds;
		const auto width = maxX - minX + 1;
		const auto height = maxY - minY + 1;
		const auto lodMinX = GetLODCoord(lodLevel, minX);
		const auto lodMinY = GetLODCoord(lodLevel, minY);
		const auto lodMaxX = GetLODCoord(lodLevel, maxX);
		const auto lodMaxY = GetLODCoord(lodLevel, maxY);
		std::vector<bool> processed(lodLevel * lodLevel);
		for (auto lodY = lodMinY; lodY <= lodMaxY; ++lodY) {
			for (auto lodX = lodMinX; lodX <= lodMaxX; ++lodX) {
				const auto baseX = lodX * lodLevel - minX;
				const auto baseY = lodY * lodLevel - minY;
				std::ranges::fill(processed, false);
				for (std::int32_t localY = 0; localY < lodLevel; ++localY) {
					for (std::int32_t localX = 0; localX < lodLevel; ++localX) {
						const auto localIndex = static_cast<std::size_t>(localY * lodLevel + localX);
						if (processed[localIndex])
							continue;
						const auto cellX = baseX + localX;
						const auto cellY = baseY + localY;
						if (cellX < 0 || cellX >= width || cellY < 0 || cellY >= height) {
							processed[localIndex] = true;
							continue;
						}
						const auto& target = cellData[cellY * width + cellX];
						if (!target.formID) {
							processed[localIndex] = true;
							continue;
						}
						std::int32_t size = 1;
						std::int32_t maxSize = 1;
						if (lodLevel > 4) {
							// LOD4 stays per-cell so loaded cells can hide it; others fit the chunk.
							maxSize = std::min({ lodLevel - localX, lodLevel - localY, width - cellX, height - cellY });
						}
						const auto matches = [&](std::int32_t x, std::int32_t y) {
							const auto& candidate = cellData[(baseY + y) * width + baseX + x];
							return !processed[y * lodLevel + x] && candidate.formID &&
							       candidate.water == target.water && candidate.formID == target.formID && candidate.flags == target.flags;
						};
						// Try to grow to the largest square possible starting at localX/localY
						for (std::int32_t candidateSize = 2; candidateSize <= maxSize; ++candidateSize) {
							bool failed = false;
							const auto row = localY + candidateSize - 1;
							for (std::int32_t dx = 0; dx < candidateSize; ++dx)
								if (!matches(localX + dx, row)) {
									failed = true;
									break;
								}
							if (failed)
								break;
							// Exclude the corner already checked in the top row.
							const auto column = localX + candidateSize - 1;
							for (std::int32_t dy = 0; dy < candidateSize - 1; ++dy)
								if (!matches(column, localY + dy)) {
									failed = true;
									break;
								}
							if (failed)
								break;
							size = candidateSize;
						}
						for (auto yy = localY; yy < localY + size; ++yy)
							for (auto xx = localX; xx < localX + size; ++xx)
								processed[yy * lodLevel + xx] = true;
						Instruction instruction;
						instruction.lodLevel = lodLevel;
						instruction.form.id = target.formID;
						instruction.x = cellX + minX;
						instruction.y = cellY + minY;
						instruction.size = size;
						instruction.waterHeight = target.water;
						instructions.push_back(instruction);
					}
				}
			}
		}
	}
	// Squares of one chunk that share a water form and height.
	struct TileGroup
	{
		void* form{};
		float height{};
		std::vector<const Instruction*> rects;
	};

	inline std::vector<TileGroup> GroupInstructions(const std::vector<Instruction>& a_instructions)
	{
		std::vector<TileGroup> groups;
		for (const auto& instruction : a_instructions) {
			if (!instruction.form.ptr)
				continue;
			auto found = std::ranges::find_if(groups, [&](const TileGroup& group) {
				return group.form == instruction.form.ptr && group.height == instruction.waterHeight;
			});
			if (found == groups.end())
				found = groups.insert(groups.end(), TileGroup{ instruction.form.ptr, instruction.waterHeight, {} });
			found->rects.push_back(&instruction);
		}
		return groups;
	}

	// One flat quad per square, positions relative to origin and divided by scale.
	struct MergedMesh
	{
		struct Vertex
		{
			float x, y, u, v;
		};
		std::vector<Vertex> vertices;
		std::vector<std::uint16_t> indices;
		float originX{};
		float originY{};
		float scale{ 1.0f };
		float radius{};
	};

	// Half-float vertices: origin and scale keep every coordinate exact.
	inline bool BuildMergedMesh(const TileGroup& a_group, std::int32_t a_baseX, std::int32_t a_baseY, MergedMesh& a_mesh)
	{
		constexpr float kCell = 4096.0f;
		constexpr std::size_t kMaxRects = 0xFFFF / 4;
		if (a_group.rects.empty() || a_group.rects.size() > kMaxRects)
			return false;
		float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
		for (const auto* rect : a_group.rects) {
			minX = std::min(minX, (rect->x - a_baseX) * kCell);
			minY = std::min(minY, (rect->y - a_baseY) * kCell);
			maxX = std::max(maxX, (rect->x - a_baseX + static_cast<std::int32_t>(rect->size)) * kCell);
			maxY = std::max(maxY, (rect->y - a_baseY + static_cast<std::int32_t>(rect->size)) * kCell);
		}
		a_mesh.originX = (minX + maxX) * 0.5f;
		a_mesh.originY = (minY + maxY) * 0.5f;
		const float halfX = (maxX - minX) * 0.5f;
		const float halfY = (maxY - minY) * 0.5f;
		// Half floats top out at 65504; a power-of-two scale stays exact.
		a_mesh.scale = std::max(halfX, halfY) > 32768.0f ? 2.0f : 1.0f;
		a_mesh.radius = std::sqrt(halfX * halfX + halfY * halfY) / a_mesh.scale;
		for (const auto* rect : a_group.rects) {
			const float left = ((rect->x - a_baseX) * kCell - a_mesh.originX) / a_mesh.scale;
			const float bottom = ((rect->y - a_baseY) * kCell - a_mesh.originY) / a_mesh.scale;
			const float span = rect->size * kCell / a_mesh.scale;
			const auto base = static_cast<std::uint16_t>(a_mesh.vertices.size());
			// Corner order and UVs match the tile mesh quad.
			a_mesh.vertices.push_back({ left, bottom, 0.0f, 1.0f });
			a_mesh.vertices.push_back({ left + span, bottom, 1.0f, 1.0f });
			a_mesh.vertices.push_back({ left + span, bottom + span, 1.0f, 0.0f });
			a_mesh.vertices.push_back({ left, bottom + span, 0.0f, 0.0f });
			for (const std::uint16_t index : std::array<std::uint16_t, 6>{ 0, 1, 2, 2, 3, 0 })
				a_mesh.indices.push_back(static_cast<std::uint16_t>(base + index));
		}
		return true;
	}
}
