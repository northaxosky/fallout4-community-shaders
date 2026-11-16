#pragma once

#include "MaterialRegistry.h"
#include "RecordTextures.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cs::features::true_pbr
{
	class MaterialTags
	{
	public:
		// 0 when untagged. Lock-free while nothing is tagged.
		[[nodiscard]] RecordId Find(const void* a_material) const;
		// The textures stay valid while the material lives.
		[[nodiscard]] engine::PrepassRecord Lookup(const void* a_material) const;

		// A different tag already there means a shared material; counted.
		void Assign(const void* a_material, RecordId a_id, RecordTextureSet a_textures = {});

		// Gives a_dest a_source's tag; false when a_source has none (a_dest is untagged).
		bool Inherit(const void* a_dest, const void* a_source);

		void Erase(const void* a_material);

		[[nodiscard]] std::size_t Count() const noexcept { return _count.load(std::memory_order_acquire); }
		[[nodiscard]] std::uint64_t Conflicts() const noexcept { return _conflicts.load(std::memory_order_relaxed); }

		void NoteForeignPath() noexcept { _foreign.fetch_add(1, std::memory_order_relaxed); }
		[[nodiscard]] std::uint64_t ForeignPaths() const noexcept { return _foreign.load(std::memory_order_relaxed); }

		[[nodiscard]] std::vector<std::pair<RecordId, std::size_t>> LiveByRecord() const;

	private:
		struct Entry
		{
			RecordId id = 0;
			RecordTextureSet textures;
		};

		// Under the unique lock; the displaced entry is released after unlocking.
		[[nodiscard]] Entry Replace(const void* a_material, RecordId a_id, RecordTextureSet&& a_textures, bool a_countConflict);

		mutable std::shared_mutex _lock;
		std::unordered_map<const void*, Entry> _tags;
		std::atomic<std::size_t> _count{ 0 };
		std::atomic<std::uint64_t> _conflicts{ 0 };
		std::atomic<std::uint64_t> _foreign{ 0 };
	};

	[[nodiscard]] MaterialTags& Tags();
}
