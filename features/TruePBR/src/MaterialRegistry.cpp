#include "MaterialRegistry.h"

#include "Format/PbrValidation.h"

#include <algorithm>
#include <cctype>

namespace cs::features::true_pbr
{
	namespace
	{
		constexpr std::string_view kMaterialsRoot = "materials\\";

		std::string FoldCase(std::string_view a_text)
		{
			std::string folded(a_text);
			std::ranges::transform(folded, folded.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return folded;
		}
	}

	std::optional<std::string> CanonicalName(std::string_view a_path)
	{
		auto name = FoldCase(a_path);
		std::ranges::replace(name, '/', '\\');
		for (auto at = name.find(kMaterialsRoot); at != std::string::npos; at = name.find(kMaterialsRoot, at + 1)) {
			if (at == 0 || name[at - 1] == '\\')
				return name.substr(at);
		}
		return std::nullopt;
	}

	MaterialRegistry::MaterialRegistry() :
		_slots(std::make_unique<std::atomic<const MaterialRecord*>[]>(kCapacity))
	{}

	std::expected<Ingested, Rejection> MaterialRegistry::Ingest(std::string_view a_canonicalName, PbrMaterial a_material)
	{
		auto validation = Validate(a_material);
		if (!validation.errors.empty())
			return std::unexpected(Rejection{ std::move(validation.errors) });

		Ingested result{ nullptr, IngestOutcome::kPublished, std::move(validation.warnings) };
		const auto key = FoldCase(a_canonicalName);
		std::unique_lock lock(_lock);
		if (const auto found = _byName.find(key); found != _byName.end()) {
			result.record = found->second;
			result.outcome = result.record->material == a_material ? IngestOutcome::kReused : IngestOutcome::kChangedOnDisk;
			return result;
		}

		const auto next = _count.load(std::memory_order_relaxed) + 1;
		if (next >= kCapacity)
			return std::unexpected(Rejection{ { "Material record table is full." } });
		const auto& owned = _owned.emplace_back(std::make_unique<const MaterialRecord>(
			MaterialRecord{ static_cast<RecordId>(next), key, std::move(a_material) }));
		_byName.emplace(key, owned.get());
		_slots[next].store(owned.get(), std::memory_order_release);
		_count.store(next, std::memory_order_release);
		result.record = owned.get();
		return result;
	}

	const MaterialRecord* MaterialRegistry::Find(std::string_view a_canonicalName) const
	{
		std::shared_lock lock(_lock);
		const auto found = _byName.find(FoldCase(a_canonicalName));
		return found == _byName.end() ? nullptr : found->second;
	}

	const MaterialRecord* MaterialRegistry::Get(RecordId a_id) const noexcept
	{
		if (a_id == 0 || a_id > Count())
			return nullptr;
		return _slots[a_id].load(std::memory_order_acquire);
	}

	MaterialRegistry& Records()
	{
		static MaterialRegistry registry;
		return registry;
	}
}
