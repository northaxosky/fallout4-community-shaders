#include "WaterCache.h"

#include "Log.h"
#include "Plugin.h"
#include "RE/T/TESDataHandler.h"
#include "Render/TerrainWater.h"
#include "Utils/WinApi.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <format>
#include <fstream>
#include <winrt/base.h>

namespace cs::features::unified_water
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.unifiedwater.cache");
		// Far beyond normal terrain heights, but below sentinel/garbage values
		constexpr float kMaxValidCellHeight = 50000.0f;
		bool IsValidCellHeight(float height)
		{
			// Cell records may use sentinel or garbage heights for absent water data
			return std::isfinite(height) && height != FLT_MIN && height != FLT_MAX && std::fabs(height) < kMaxValidCellHeight;
		}
		RE::TESWaterForm* LookupWaterForm(std::uint32_t a_id)
		{
			return a_id ? RE::TESForm::GetFormByID<RE::TESWaterForm>(a_id) : nullptr;
		}

		// Permissive sharing and retries tolerate transient hash-file locks.
		constexpr int kShareRetries = 3;
		constexpr DWORD kShareRetryDelayMs = 50;

		bool AccessHashFile(const std::filesystem::path& a_path, std::uint64_t& a_hash, bool a_write)
		{
			for (int attempt = 0; attempt < kShareRetries; ++attempt) {
				winrt::file_handle handle{ CreateFileW(a_path.c_str(), a_write ? GENERIC_WRITE : GENERIC_READ,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
					a_write ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
				if (handle) {
					DWORD bytes = 0;
					return (a_write ? WriteFile(handle.get(), &a_hash, sizeof(a_hash), &bytes, nullptr) :
									  ReadFile(handle.get(), &a_hash, sizeof(a_hash), &bytes, nullptr)) &&
					       bytes == sizeof(a_hash);
				}
				const auto error = GetLastError();
				if (!a_write && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
					return false;
				if ((error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) && attempt + 1 < kShareRetries) {
					Sleep(kShareRetryDelayMs);
					continue;
				}
				L->warn("Failed to open '{}' for {} (error {})", a_path.string(), a_write ? "writing" : "reading", error);
				return false;
			}
			return false;
		}
	}

	std::filesystem::path WaterCache::Path()
	{
		// FO4: keep generated files in the existing plugin-owned data directory.
		return std::filesystem::path("Data/F4SE/Plugins/FO4CommunityShaders/UnifiedWater");
	}

	const RE::TESWorldSpace* WaterCache::Root(const RE::TESWorldSpace* a_world)
	{
		while (a_world && a_world->parentWorld && a_world->parentUseFlags.all(RE::TESWorldSpace::PARENT_USE_FLAG::kWater))
			a_world = a_world->parentWorld;
		return a_world;
	}

	std::vector<RE::TESWorldSpace*> WaterCache::GetValidWorldSpaces()
	{
		std::vector<RE::TESWorldSpace*> worlds;
		for (const auto world : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESWorldSpace>()) {
			auto root = const_cast<RE::TESWorldSpace*>(Root(world));
			if (root && root->worldWater && std::ranges::find(worlds, root) == worlds.end())
				worlds.push_back(root);
		}
		return worlds;
	}

	bool WaterCache::LoadOrderChanged()
	{
		std::uint64_t hash = 14695981039346656037ull;
		const auto add = [&](std::string_view text) {
			for (const auto c : text) {
				hash ^= static_cast<unsigned char>(c);
				hash *= 1099511628211ull;
			}
		};
		const auto handler = RE::TESDataHandler::GetSingleton();
		// FO4: compiled files include normal and light plugins in load order.
		for (const auto file : handler->compiledFileCollection.files)
			if (file)
				add(file->GetFilename());
		for (const auto file : handler->compiledFileCollection.smallFiles)
			if (file)
				add(file->GetFilename());
		add(Plugin::VERSION.string());
		std::error_code ec;
		std::filesystem::create_directories(Path(), ec);
		const auto path = Path() / "UWLoadOrder.hash";
		std::uint64_t previous = 0;
		AccessHashFile(path, previous, false);
		if (previous == hash)
			return false;
		if (!AccessHashFile(path, hash, true))
			L->warn("Could not persist load-order hash; caches will regenerate next launch");
		return true;
	}

	bool WaterCache::ReadCache(const std::string& a_name, Header& a_header, std::vector<Instruction>& a_instructions)
	{
		const auto path = Path() / (a_name + "_cache.wc");
		std::ifstream input(path, std::ios::binary);
		if (!input.read(reinterpret_cast<char*>(&a_header), sizeof(a_header)) || a_header.label != Header{}.label)
			return false;
		std::error_code ec;
		const auto length = std::filesystem::file_size(path, ec);
		if (ec || !IsValidCacheHeader(a_header, length))
			return false;
		a_instructions.resize(a_header.dataCount);
		return a_instructions.empty() || static_cast<bool>(input.read(reinterpret_cast<char*>(a_instructions.data()), a_instructions.size() * sizeof(Instruction)));
	}

	bool WaterCache::WriteCache(const std::string& a_name, const Header& a_header, const std::vector<Instruction>& a_instructions)
	{
		std::ofstream output(Path() / (a_name + "_cache.wc"), std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(&a_header), sizeof(a_header));
		output.write(reinterpret_cast<const char*>(a_instructions.data()), a_instructions.size() * sizeof(Instruction));
		return output.good();
	}

	std::shared_ptr<WaterCache::RuntimeCache> WaterCache::BuildRuntimeCache(const Header& a_header, std::vector<Instruction> a_instructions, const FormMap* a_forms)
	{
		auto cache = std::make_shared<RuntimeCache>();
		cache->bounds = a_header.bounds;
		cache->instructions.resize(4);
		for (std::uint32_t lodIndex = 0; lodIndex < 4; ++lodIndex) {
			const auto level = 1 << (lodIndex + 2);
			const auto width = GetLODCoord(level, a_header.bounds.maxX) - GetLODCoord(level, a_header.bounds.minX) + 1;
			const auto height = GetLODCoord(level, a_header.bounds.maxY) - GetLODCoord(level, a_header.bounds.minY) + 1;
			if (width <= 0 || height <= 0)
				continue;
			cache->instructions[lodIndex].resize(static_cast<std::size_t>(width) * height);
		}
		// Keep old or malformed disk caches from restoring bad water tiles
		for (auto instruction : a_instructions) {
			if (!IsValidCellHeight(instruction.waterHeight) || !std::has_single_bit(instruction.lodLevel) || instruction.lodLevel < 4 || instruction.lodLevel > 32)
				continue;
			const auto& bounds = a_header.bounds;
			if (!instruction.size || instruction.size > instruction.lodLevel ||
				(instruction.lodLevel == 4 && instruction.size != 1) ||
				instruction.x < bounds.minX || instruction.y < bounds.minY ||
				static_cast<std::int64_t>(instruction.x) + instruction.size - 1 > bounds.maxX ||
				static_cast<std::int64_t>(instruction.y) + instruction.size - 1 > bounds.maxY)
				continue;
			const auto found = a_forms ? a_forms->find(instruction.form.id) : FormMap::const_iterator{};
			const auto form = a_forms ? (found != a_forms->end() ? found->second : nullptr) : LookupWaterForm(instruction.form.id);
			if (!form)
				continue;
			instruction.form.ptr = form;
			const auto level = static_cast<std::int32_t>(instruction.lodLevel);
			const auto minX = GetLODCoord(level, a_header.bounds.minX);
			const auto minY = GetLODCoord(level, a_header.bounds.minY);
			const auto width = GetLODCoord(level, a_header.bounds.maxX) - minX + 1;
			const auto x = GetLODCoord(level, instruction.x) - minX;
			const auto y = GetLODCoord(level, instruction.y) - minY;
			auto& chunks = cache->instructions[std::countr_zero(instruction.lodLevel) - 2];
			const auto index = y * width + x;
			if (x >= 0 && x < width && y >= 0 && index >= 0 && static_cast<std::size_t>(index) < chunks.size())
				chunks[index].push_back(instruction);
		}
		return cache;
	}

	const std::vector<Instruction>* WaterCache::RuntimeCache::GetInstructions(std::uint32_t a_level, std::int32_t a_x, std::int32_t a_y) const
	{
		if (!std::has_single_bit(a_level) || a_level < 4 || a_level > 32)
			return nullptr;
		const auto level = static_cast<std::int32_t>(a_level);
		const auto minX = GetLODCoord(level, bounds.minX);
		const auto minY = GetLODCoord(level, bounds.minY);
		const auto width = GetLODCoord(level, bounds.maxX) - minX + 1;
		const auto x = GetLODCoord(level, a_x) - minX;
		const auto y = GetLODCoord(level, a_y) - minY;
		const auto index = y * width + x;
		const auto& chunks = instructions[std::countr_zero(a_level) - 2];
		if (x < 0 || x >= width || y < 0 || index < 0 || static_cast<std::size_t>(index) >= chunks.size())
			return nullptr;
		return &chunks[index];
	}

	WaterCache::InstructionResult WaterCache::GetInstructions(const RE::TESWorldSpace* a_world, std::uint32_t a_level, std::int32_t a_x, std::int32_t a_y) const
	{
		const auto root = Root(a_world);
		if (!root)
			return {};
		const auto caches = _caches.load(std::memory_order_acquire);
		const auto it = caches->find(root->editorID.c_str());
		if (it == caches->end())
			return {};
		return { it->second, it->second->GetInstructions(a_level, a_x, a_y) };
	}

	bool WaterCache::LoadCaches()
	{
		auto caches = std::make_shared<CacheMap>();
		for (const auto world : GetValidWorldSpaces()) {
			if (!world->editorID.c_str() || !*world->editorID.c_str())
				continue;
			Header header;
			std::vector<Instruction> instructions;
			if (ReadCache(world->editorID.c_str(), header, instructions))
				caches->emplace(world->editorID.c_str(), BuildRuntimeCache(header, std::move(instructions)));
		}
		if (caches->empty())
			return false;
		_caches.store(caches, std::memory_order_release);
		_ready.store(true, std::memory_order_release);
		return true;
	}

	void WaterCache::ReadSnapshots()
	{
		// FO4: snapshot unsynchronized native cursors before terrain streaming.
		engine::water::CellReader reader;
		for (const auto world : GetValidWorldSpaces()) {
			if (!world->editorID.c_str() || !*world->editorID.c_str())
				continue;
			Snapshot snapshot;
			snapshot.name = world->editorID.c_str();
			auto& header = snapshot.header;
			const auto min = world->minimumCoords;
			const auto max = world->maximumCoords;
			if (!std::isfinite(min.x) || !std::isfinite(min.y) || !std::isfinite(max.x) || !std::isfinite(max.y) ||
				std::fabs(min.x) > 2097152 || std::fabs(min.y) > 2097152 || std::fabs(max.x) > 2097152 || std::fabs(max.y) > 2097152)
				continue;
			header.bounds = { static_cast<std::int32_t>(std::floor(min.x / 4096)), static_cast<std::int32_t>(std::floor(min.y / 4096)),
				static_cast<std::int32_t>(std::floor(max.x / 4096)) - 1, static_cast<std::int32_t>(std::floor(max.y / 4096)) - 1 };
			header.width = header.bounds.maxX - header.bounds.minX + 1;
			header.height = header.bounds.maxY - header.bounds.minY + 1;
			if (header.width <= 0 || header.height <= 0 || header.width > 512 || header.height > 512)
				continue;
			snapshot.cells.resize(static_cast<std::size_t>(header.width) * header.height);
			for (auto y = header.bounds.minY; y <= header.bounds.maxY; ++y) {
				for (auto x = header.bounds.minX; x <= header.bounds.maxX; ++x) {
					std::uint32_t formID = 0;
					float water, land;
					if (!reader.Read(world, x, y, formID, water, land))
						continue;
					if (IsValidCellHeight(water) && IsValidCellHeight(land) && water > land) {
						// Use default world water if no water form set
						if (!formID)
							formID = world->worldWater->GetFormID();
					} else {
						// Discard invalid tiles before they can generate floating water planes
						formID = 0;
					}
					auto form = LookupWaterForm(formID);
					if (!form && formID && formID != world->worldWater->GetFormID()) {
						formID = world->worldWater->GetFormID();
						form = LookupWaterForm(formID);
					}
					if (form) {
						_forms.try_emplace(formID, form);
						snapshot.cells[(y - header.bounds.minY) * header.width + x - header.bounds.minX] = { water, formID, static_cast<std::uint8_t>(form->flags) };
					}
				}
			}
			_snapshots.push_back(std::move(snapshot));
		}
	}

	void WaterCache::LoadOrGenerateCaches(bool a_changed)
	{
		if (!a_changed && LoadCaches())
			return;
		ReadSnapshots();
		RegenerateCaches();
		while (_running.load(std::memory_order_acquire))
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	void WaterCache::ScheduleRegeneration()
	{
		// FO4: cell reads are startup-only, so a rebuild waits for the next launch.
		std::error_code ec;
		std::filesystem::remove(Path() / "UWLoadOrder.hash", ec);
		if (ec)
			L->warn("Could not schedule cache regeneration: {}", ec.message());
	}

	void WaterCache::RegenerateCaches()
	{
		if (_running.exchange(true))
			return;
		if (_snapshots.empty()) {
			_running.store(false);
			L->error("No water worldspaces to cache");
			return;
		}
		_total.store(static_cast<std::uint32_t>(_snapshots.size()));
		_completed.store(0);
		_failed.store(0);
		std::error_code ec;
		std::filesystem::create_directories(Path(), ec);
		for (const auto& entry : std::filesystem::directory_iterator(Path(), ec))
			if (entry.path().extension() == ".wc")
				std::filesystem::remove(entry.path(), ec);
		GenerateCaches();
	}

	void WaterCache::GenerateCaches()
	{
		_monitor = std::jthread([this] {
			// Use P-core logical count on Intel hybrid CPUs
			const auto hw = Util::GetPerformanceCoreCount();
			const auto count = std::max(1u, hw > 4 ? hw - 4 : (hw * 3) / 4);
			std::atomic_size_t next{ 0 };
			auto caches = std::make_shared<CacheMap>();
			std::mutex publication;
			std::vector<std::jthread> workers;
			for (std::uint32_t i = 0; i < std::min(count, _total.load()); ++i)
				workers.emplace_back([&] {
					for (auto index = next.fetch_add(1); index < _snapshots.size(); index = next.fetch_add(1)) {
						const auto& snapshot = _snapshots[index];
						std::vector<Instruction> instructions;
						for (const auto level : { 4, 8, 16, 32 })
							GenerateInstructions(level, snapshot.header.bounds, snapshot.cells, instructions);
						auto header = snapshot.header;
						header.dataCount = static_cast<std::int32_t>(instructions.size());
						if (WriteCache(snapshot.name, header, instructions)) {
							// FO4: workers use immutable WATR snapshots, not engine lookup.
							auto cache = BuildRuntimeCache(header, std::move(instructions), &_forms);
							const std::scoped_lock lock(publication);
							caches->emplace(snapshot.name, std::move(cache));
							_completed.fetch_add(1);
						} else
							_failed.fetch_add(1);
					}
				});
			workers.clear();
			_snapshots = {};
			_forms = {};
			if (!caches->empty()) {
				_caches.store(caches, std::memory_order_release);
				_ready.store(true, std::memory_order_release);
			}
			L->info("Water caches generated: {} complete, {} failed", _completed.load(), _failed.load());
			_running.store(false, std::memory_order_release);
		});
	}

	WaterCache::Progress WaterCache::GetProgress() const noexcept
	{
		return { _total.load(), _completed.load(), _failed.load(), _running.load() };
	}
}
