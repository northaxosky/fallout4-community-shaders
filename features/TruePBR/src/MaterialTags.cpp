#include "MaterialTags.h"

#include <algorithm>
#include <map>

namespace cs::features::true_pbr
{
	engine::PrepassRecord MaterialTags::Lookup(const void* a_material) const
	{
		if (Count() == 0)
			return {};
		std::shared_lock lock(_lock);
		const auto found = _tags.find(a_material);
		if (found == _tags.end())
			return {};
		engine::PrepassRecord record{ .id = found->second.id };
		std::ranges::transform(found->second.textures, record.textures.begin(), [](const auto& a_texture) { return a_texture.get(); });
		return record;
	}

	MaterialTags::Entry MaterialTags::Replace(const void* a_material, RecordId a_id, RecordTextureSet&& a_textures, bool a_countConflict)
	{
		Entry displaced;
		const auto found = _tags.find(a_material);
		if (found != _tags.end()) {
			if (a_countConflict && found->second.id != a_id)
				_conflicts.fetch_add(1, std::memory_order_relaxed);
			displaced = std::move(found->second);
			if (a_id == 0)
				_tags.erase(found);
			else
				found->second = Entry{ a_id, std::move(a_textures) };
		} else if (a_id != 0) {
			_tags.emplace(a_material, Entry{ a_id, std::move(a_textures) });
		}
		_count.store(_tags.size(), std::memory_order_release);
		return displaced;
	}

	void MaterialTags::Assign(const void* a_material, RecordId a_id, RecordTextureSet a_textures)
	{
		Entry displaced;
		std::unique_lock lock(_lock);
		displaced = Replace(a_material, a_id, std::move(a_textures), true);
	}

	void MaterialTags::Inherit(const void* a_dest, const void* a_source)
	{
		Entry displaced;
		std::unique_lock lock(_lock);
		const auto source = _tags.find(a_source);
		if (source == _tags.end()) {
			displaced = Replace(a_dest, 0, {}, false);
			return;
		}
		const auto id = source->second.id;
		auto textures = source->second.textures;
		displaced = Replace(a_dest, id, std::move(textures), true);
	}

	void MaterialTags::Erase(const void* a_material)
	{
		if (Count() == 0)
			return;
		Entry displaced;
		std::unique_lock lock(_lock);
		displaced = Replace(a_material, 0, {}, false);
	}

	std::vector<std::pair<RecordId, std::size_t>> MaterialTags::LiveByRecord() const
	{
		std::map<RecordId, std::size_t> perRecord;
		{
			std::shared_lock lock(_lock);
			for (const auto& entry : _tags)
				++perRecord[entry.second.id];
		}
		return { perRecord.begin(), perRecord.end() };
	}

	MaterialTags& Tags()
	{
		static MaterialTags tags;
		return tags;
	}
}
