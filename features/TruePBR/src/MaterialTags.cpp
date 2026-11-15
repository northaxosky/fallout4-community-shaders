#include "MaterialTags.h"

#include <algorithm>
#include <map>

namespace cs::features::true_pbr
{
	RecordId MaterialTags::Find(const void* a_material) const
	{
		if (Count() == 0)
			return 0;
		std::shared_lock lock(_lock);
		const auto found = _tags.find(a_material);
		return found == _tags.end() ? RecordId{ 0 } : found->second;
	}

	void MaterialTags::Assign(const void* a_material, RecordId a_id)
	{
		std::unique_lock lock(_lock);
		const auto found = _tags.find(a_material);
		if (found != _tags.end() && found->second != a_id)
			_conflicts.fetch_add(1, std::memory_order_relaxed);
		if (a_id == 0) {
			if (found != _tags.end())
				_tags.erase(found);
		} else {
			_tags.insert_or_assign(a_material, a_id);
		}
		_count.store(_tags.size(), std::memory_order_release);
	}

	void MaterialTags::Erase(const void* a_material)
	{
		if (Count() == 0)
			return;
		std::unique_lock lock(_lock);
		if (_tags.erase(a_material) != 0)
			_count.store(_tags.size(), std::memory_order_release);
	}

	std::vector<std::pair<RecordId, std::size_t>> MaterialTags::LiveByRecord() const
	{
		std::map<RecordId, std::size_t> perRecord;
		{
			std::shared_lock lock(_lock);
			for (const auto& entry : _tags)
				++perRecord[entry.second];
		}
		return { perRecord.begin(), perRecord.end() };
	}

	MaterialTags& Tags()
	{
		static MaterialTags tags;
		return tags;
	}
}
