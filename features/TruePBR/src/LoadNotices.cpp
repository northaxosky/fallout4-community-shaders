#include "LoadNotices.h"

#include <format>

namespace cs::features::true_pbr
{
	bool LoadNotices::Reject(std::string_view a_name, std::string a_reason)
	{
		std::scoped_lock lock(_lock);
		_last = std::format("{}: {}", a_name, a_reason);
		const bool first = _rejectedNames.emplace(a_name).second;
		if (first)
			_rejected.store(_rejectedNames.size(), std::memory_order_relaxed);
		return first;
	}

	bool LoadNotices::Once(std::string_view a_key)
	{
		std::scoped_lock lock(_lock);
		return _seen.emplace(a_key).second;
	}

	std::string LoadNotices::LastRejection() const
	{
		std::scoped_lock lock(_lock);
		return _last;
	}

	LoadNotices& Notices()
	{
		static LoadNotices notices;
		return notices;
	}
}
