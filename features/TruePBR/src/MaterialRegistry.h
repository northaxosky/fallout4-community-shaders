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
	// 0 is not PBR; ids are never reused in a session.
	using RecordId = std::uint16_t;

	// Anchors on the first materials\ component; nullopt without one.
	[[nodiscard]] std::optional<std::string> CanonicalName(std::string_view a_path);

	// Baked draw data may hold the id, so records are never freed.
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
		kChangedOnDisk  // differs from the kept record
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

		// Validation runs outside the lock.
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
