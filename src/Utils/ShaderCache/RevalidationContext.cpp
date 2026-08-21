#include "Utils/ShaderCache/RevalidationContext.h"

#include "Utils/ShaderCache/ShaderRecipe.h"

#include <vector>

namespace cs::shader_cache
{
	FileObservation ObserveFile(const std::string& a_locator)
	{
		FileObservation           observation;
		std::vector<std::uint8_t> bytes;
		try {
			observation.status =
				ReadFileBytes(DecodeLocator(a_locator), kMaxSourceBytes, bytes);
			if (observation.status == FileReadStatus::kOk) {
				observation.contentDigest =
					sha256::Sha256Compute(bytes.data(), bytes.size());
				observation.contentLength = bytes.size();
				if (sha256::Sha256IsZero(observation.contentDigest))
					observation.status = FileReadStatus::kReadFailed;
			}
		} catch (...) {
			observation.status = FileReadStatus::kReadFailed;
		}
		return observation;
	}

	FileObservation RevalidationContext::Observe(const std::string& a_locator)
	{
		std::shared_ptr<Entry> entry;
		{
			const std::scoped_lock lock(_lock);
			auto&                  slot = _entries[a_locator];
			if (!slot)
				slot = std::make_shared<Entry>();
			entry = slot;
		}

		// read outside the lock to avoid serializing the batch
		std::call_once(entry->once, [this, &a_locator, &entry] {
			entry->observation = ObserveFile(a_locator);
			_reads.fetch_add(1, std::memory_order_relaxed);
		});

		return entry->observation;
	}

	std::size_t RevalidationContext::ObservedPaths() const
	{
		const std::scoped_lock lock(_lock);
		return _entries.size();
	}

	std::size_t RevalidationContext::Reads() const noexcept
	{
		return _reads.load(std::memory_order_relaxed);
	}
}
