#pragma once

#include "MaterialRegistry.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cs::features::true_pbr
{
	// Live engine material (as an opaque pointer) to the record of the file it came from.
	// The prepass classifier reads it with Find; the tag hooks keep it in step with the engine.
	class MaterialTags
	{
	public:
		// 0 when untagged. Lock-free while nothing is tagged.
		[[nodiscard]] RecordId Find(const void* a_material) const;

		// Tags a_material, or untags it for id 0. A tag already there for another record means
		// two properties share one material; the new value wins and the conflict is counted.
		void Assign(const void* a_material, RecordId a_id);

		void Erase(const void* a_material);

		[[nodiscard]] std::size_t Count() const noexcept { return _count.load(std::memory_order_acquire); }
		[[nodiscard]] std::uint64_t Conflicts() const noexcept { return _conflicts.load(std::memory_order_relaxed); }

		// Setups whose materialPath named no record.
		void NoteForeignPath() noexcept { _foreign.fetch_add(1, std::memory_order_relaxed); }
		[[nodiscard]] std::uint64_t ForeignPaths() const noexcept { return _foreign.load(std::memory_order_relaxed); }

		// Live materials per record id, ascending.
		[[nodiscard]] std::vector<std::pair<RecordId, std::size_t>> LiveByRecord() const;

	private:
		mutable std::shared_mutex _lock;
		std::unordered_map<const void*, RecordId> _tags;
		std::atomic<std::size_t> _count{ 0 };
		std::atomic<std::uint64_t> _conflicts{ 0 };
		std::atomic<std::uint64_t> _foreign{ 0 };
	};

	[[nodiscard]] MaterialTags& Tags();
}
