#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cs::features::true_pbr
{
	class LoadNotices
	{
	public:
		void Reject(std::string_view a_name, std::string a_reason);

		[[nodiscard]] std::size_t RejectedCount() const noexcept { return _rejected.load(std::memory_order_relaxed); }
		[[nodiscard]] std::string LastRejection() const;

	private:
		mutable std::mutex _lock;
		std::unordered_set<std::string> _rejectedNames;
		std::string _last;
		std::atomic<std::size_t> _rejected{ 0 };
	};

	[[nodiscard]] LoadNotices& Notices();
}
