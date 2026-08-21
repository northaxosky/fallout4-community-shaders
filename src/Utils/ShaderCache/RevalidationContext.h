#pragma once

#include "Utils/CSSha256.h"
#include "Utils/ShaderCache/CacheStorage.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cs::shader_cache
{
	struct FileObservation
	{
		FileReadStatus       status = FileReadStatus::kMissing;
		sha256::Sha256Result contentDigest{};
		std::uint64_t        contentLength = 0;
	};

	FileObservation ObserveFile(const std::string& a_locator);

	// memo lifetime = one freeze batch
	class RevalidationContext
	{
	public:
		RevalidationContext() = default;

		RevalidationContext(const RevalidationContext&) = delete;
		RevalidationContext& operator=(const RevalidationContext&) = delete;

		FileObservation Observe(const std::string& a_locator);

		[[nodiscard]] std::size_t ObservedPaths() const;
		[[nodiscard]] std::size_t Reads() const noexcept;

	private:
		struct Entry
		{
			std::once_flag  once;
			FileObservation observation;
		};

		mutable std::mutex                                     _lock;
		std::unordered_map<std::string, std::shared_ptr<Entry>> _entries;
		std::atomic<std::size_t>                               _reads{ 0 };
	};
}
