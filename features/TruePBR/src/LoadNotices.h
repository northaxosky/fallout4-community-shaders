#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cs::features::true_pbr
{
	// Deduplicates load diagnostics so a file warns once per session however often it parses.
	class LoadNotices
	{
	public:
		// True the first time a_name is rejected; later calls only refresh the last reason.
		bool Reject(std::string_view a_name, std::string a_reason);

		// True the first time a_key is seen.
		bool Once(std::string_view a_key);

		[[nodiscard]] std::size_t RejectedCount() const noexcept { return _rejected.load(std::memory_order_relaxed); }
		[[nodiscard]] std::string LastRejection() const;

	private:
		mutable std::mutex _lock;
		std::unordered_set<std::string> _rejectedNames;
		std::unordered_set<std::string> _seen;
		std::string _last;
		std::atomic<std::size_t> _rejected{ 0 };
	};

	[[nodiscard]] LoadNotices& Notices();
}
