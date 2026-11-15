#include "LoadNotices.h"

#include <format>

namespace cs::features::true_pbr
{
	void LoadNotices::Reject(std::string_view a_name, std::string a_reason)
	{
		std::scoped_lock lock(_lock);
		_last = std::format("{}: {}", a_name, a_reason);
		_rejectedNames.emplace(a_name);
		_rejected.store(_rejectedNames.size(), std::memory_order_relaxed);
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
