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
		// True the first time this name and reason are noted.
		bool Warn(std::string_view a_name, std::string_view a_reason);

		[[nodiscard]] std::size_t RejectedCount() const noexcept { return _rejected.load(std::memory_order_relaxed); }
		[[nodiscard]] std::size_t WarningCount() const noexcept { return _warned.load(std::memory_order_relaxed); }
		[[nodiscard]] std::string LastRejection() const;

	private:
		mutable std::mutex _lock;
		std::unordered_set<std::string> _rejectedNames;
		std::unordered_set<std::string> _warnings;
		std::string _last;
		std::atomic<std::size_t> _rejected{ 0 };
		std::atomic<std::size_t> _warned{ 0 };
	};

	[[nodiscard]] LoadNotices& Notices();
}
