#pragma once

#include "Format/PbrMaterial.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cs::features::true_pbr
{
	// 16-bit id of one material file; 0 is not PBR. Never reused in a session.
	using RecordId = std::uint16_t;

	// Lowercase, backslashed name from the first `materials\` component; nullopt without one.
	[[nodiscard]] std::optional<std::string> CanonicalName(std::string_view a_path);

	// Immutable once published and never freed: baked draw data may hold the id.
	struct MaterialRecord
	{
		RecordId id;
		std::string name;
		PbrMaterial material;
	};

	enum class IngestOutcome : std::uint8_t
	{
		kPublished,
		kReused,
		kChangedOnDisk  // decoded differently from the published record, which stays
	};

	struct Ingested
	{
		const MaterialRecord* record;
		IngestOutcome outcome;
		std::vector<std::string> warnings;
	};

	struct Rejection
	{
		std::vector<std::string> errors;
	};

	class MaterialRegistry
	{
	public:
		static constexpr std::size_t kCapacity = 65536;  // ids 1..65535

		MaterialRegistry();

		// Validates a decoded material, then publishes it under a_canonicalName or compares it
		// with the record already there. Thread-safe; validation takes no lock.
		[[nodiscard]] std::expected<Ingested, Rejection> Ingest(std::string_view a_canonicalName, PbrMaterial a_material);

		[[nodiscard]] const MaterialRecord* Find(std::string_view a_canonicalName) const;

		// Lock-free; null for 0 and unpublished ids.
		[[nodiscard]] const MaterialRecord* Get(RecordId a_id) const noexcept;

		[[nodiscard]] std::size_t Count() const noexcept { return _count.load(std::memory_order_acquire); }

	private:
		std::unique_ptr<std::atomic<const MaterialRecord*>[]> _slots;
		std::atomic<std::size_t> _count{ 0 };
		mutable std::shared_mutex _lock;
		std::unordered_map<std::string, const MaterialRecord*> _byName;
		std::vector<std::unique_ptr<const MaterialRecord>> _owned;
	};

	[[nodiscard]] MaterialRegistry& Records();
}
