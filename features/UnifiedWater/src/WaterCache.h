#pragma once

#include "RE/T/TESWaterForm.h"
#include "RE/T/TESWorldSpace.h"
#include "WaterTiles.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cs::features::unified_water
{
	class WaterCache
	{
	public:
		struct RuntimeCache
		{
			Bounds bounds;
			std::vector<std::vector<std::vector<Instruction>>> instructions;
			const std::vector<Instruction>* GetInstructions(std::uint32_t a_level, std::int32_t a_x, std::int32_t a_y) const;
		};
		struct InstructionResult
		{
			std::shared_ptr<const RuntimeCache> cache;
			const std::vector<Instruction>* instructions = nullptr;
		};
		struct Progress
		{
			std::uint32_t total;
			std::uint32_t completed;
			std::uint32_t failed;
			bool active;
		};
		void LoadOrGenerateCaches(bool a_changed);
		InstructionResult GetInstructions(const RE::TESWorldSpace* a_world, std::uint32_t a_level, std::int32_t a_x, std::int32_t a_y) const;
		Progress GetProgress() const noexcept;
		bool IsReady() const noexcept { return _ready.load(std::memory_order_acquire); }
		static bool LoadOrderChanged();
		static void ScheduleRegeneration();

	private:
		using Header = CacheHeader;
		using FormMap = std::unordered_map<std::uint32_t, RE::TESWaterForm*>;
		struct Snapshot
		{
			std::string name;
			Header header;
			std::vector<CellData> cells;
		};
		using CacheMap = std::unordered_map<std::string, std::shared_ptr<const RuntimeCache>>;
		static std::filesystem::path Path();
		static std::vector<RE::TESWorldSpace*> GetValidWorldSpaces();
		static const RE::TESWorldSpace* Root(const RE::TESWorldSpace* a_world);
		static bool ReadCache(const std::string& a_name, Header& a_header, std::vector<Instruction>& a_instructions);
		static bool WriteCache(const std::string& a_name, const Header& a_header, const std::vector<Instruction>& a_instructions);
		static std::shared_ptr<RuntimeCache> BuildRuntimeCache(const Header& a_header, std::vector<Instruction> a_instructions, const FormMap* a_forms = nullptr);
		bool LoadCaches();
		void ReadSnapshots();
		void RegenerateCaches();
		void GenerateCaches();

		std::vector<Snapshot> _snapshots;
		FormMap _forms;
		std::atomic<std::shared_ptr<const CacheMap>> _caches{ std::make_shared<CacheMap>() };
		std::atomic_uint32_t _total{ 0 };
		std::atomic_uint32_t _completed{ 0 };
		std::atomic_uint32_t _failed{ 0 };
		std::atomic_bool _running{ false };
		std::atomic_bool _ready{ false };
		std::jthread _monitor;
	};
}
