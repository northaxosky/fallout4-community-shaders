#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "Sha1.h"

struct sqlite3;
struct sqlite3_stmt;

namespace cs::features::catalog
{
	// Hot-path POD enqueued by the Create*Shader thunks. Fixed-size, no allocations.
	struct CatalogEntry
	{
		std::array<uint8_t, 20> sha1_bytes{};
		std::size_t             bytecode_size = 0;
		char                    stage         = 0;  // 'v','p','c','h','d','g'
		std::uintptr_t          source_va     = 0;
		std::uint32_t           thread_id     = 0;
		std::int64_t            timestamp_qpc = 0;
		std::array<void*, 4>    stack_frames{};

		// subclass_name is a string literal owned by SubclassContext, safe to copy as a pointer.
		const char*             subclass_name           = nullptr;
		std::uint32_t           technique_bits          = 0;
		bool                    has_subclass            = false;
		bool                    has_technique_bits      = false;
		bool                    attribution_only        = false;
	};

	struct DbConfig
	{
		std::string catalog_path;       // sqlite db path
		std::uint32_t flush_interval_ms = 5000;
	};

	class CatalogDB
	{
	public:
		static CatalogDB& Get();

		// Open DB, bootstrap schema, insert a session, and start the writer; false leaves feature inert.
		bool Start(const DbConfig& cfg, const char* engine_runtime, const char* plugin_version, const char* engine_build = nullptr);

		// Stop writer, finalize session row, and close DB; idempotent.
		void Stop();

		// Hot-path producer. Lock-free MPSC ring; drops newest on overflow with a counter.
		void EnqueueShader(const CatalogEntry& e) noexcept;

		// Lightweight ImGui stats updated by the writer.
		struct Stats
		{
			std::uint64_t enqueued = 0;
			std::uint64_t dropped  = 0;
			std::uint64_t written  = 0;
			std::uint64_t attributed_ps = 0;
			std::uint64_t total_ps      = 0;
			std::uint64_t attribution_events = 0;
		};
		Stats GetStats() const noexcept;
		void EnqueueAttribution(const Sha1Result& sha, const char* subclassName, std::uint32_t techniqueBits) noexcept;

	private:
		CatalogDB() = default;
		~CatalogDB();
		CatalogDB(const CatalogDB&) = delete;
		CatalogDB& operator=(const CatalogDB&) = delete;

		void WriterLoop();
		bool OpenAndBootstrap();
		void FinalizeSession();
		void PersistShader(const CatalogEntry& e);
		void PersistAttribution(const CatalogEntry& e);
		void RefreshCatalogCounts();

		// Module/VA resolution cache; writer-thread only.
		std::string ResolveModule(std::uintptr_t va);
		std::string FormatStack(const std::array<void*, 4>& frames);

		DbConfig _cfg{};
		std::string _session_id;
		std::string _engine_runtime;
		std::string _engine_build;
		std::string _plugin_version;

		sqlite3*       _db = nullptr;
		sqlite3_stmt*  _insertShader = nullptr;
		sqlite3_stmt*  _upsertAttribution = nullptr;
		sqlite3_stmt*  _updateSession = nullptr;

		std::thread _writer;
		std::atomic<bool> _running{ false };
		std::condition_variable _wakeWriter;
		std::mutex _wakeMutex;

		// Bounded MPSC ring; power-of-two capacity enables fast masking.
		static constexpr std::size_t kCapacity = 4096;
		struct Cell
		{
			std::atomic<std::uint64_t> sequence{ 0 };
			CatalogEntry data{};
		};
		alignas(64) std::array<Cell, kCapacity> _ring{};
		alignas(64) std::atomic<std::uint64_t> _enqPos{ 0 };  // writer-claimed; producer reserves with fetch_add
		alignas(64) std::atomic<std::uint64_t> _deqPos{ 0 };  // consumer-only

		// Stats use relaxed atomics on both producer and writer sides.
		mutable std::atomic<std::uint64_t> _statEnqueued{ 0 };
		mutable std::atomic<std::uint64_t> _statDropped{ 0 };
		mutable std::atomic<std::uint64_t> _statWritten{ 0 };
		mutable std::atomic<std::uint64_t> _statAttributedPs{ 0 };
		mutable std::atomic<std::uint64_t> _statTotalPs{ 0 };
		mutable std::atomic<std::uint64_t> _statAttributionEvents{ 0 };

		// Cached module base -> formatted Name+RVA prefix.
		std::unordered_map<std::uintptr_t, std::string> _moduleCache;
	};
}
