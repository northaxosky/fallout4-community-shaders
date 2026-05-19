#include "CatalogDB.h"

#include <Windows.h>
#include <bcrypt.h>
#include <psapi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

#include <sqlite3.h>

#include "Log.h"

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace cs::features::catalog
{
	namespace { auto* L = cs::log::Get("cs.feature.catalog"); }

	// Schema source: in-repo schema definition below. If this changes,
	// bump corpus_meta.schema_version; the importer enforces version match.
	static const char* kSchemaSql = R"sql(
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS shader_catalog (
    sha1                    TEXT PRIMARY KEY,
    size_bytes              INTEGER NOT NULL,
    stage                   TEXT NOT NULL CHECK (stage IN ('vs','ps','cs','gs','hs','ds')),
    profile                 TEXT,
    cb_count                INTEGER,
    srv_count               INTEGER,
    uav_count               INTEGER,
    sampler_count           INTEGER,
    output_count            INTEGER,
    input_count             INTEGER,
    input_has_position_only INTEGER CHECK (input_has_position_only IN (0,1)),
    instruction_count       INTEGER,
    sample_call_count       INTEGER,
    input_signature_summary TEXT,
    output_signature_summary TEXT,

    first_seen_timestamp    TEXT NOT NULL,
    first_seen_session_id   TEXT NOT NULL REFERENCES sessions(session_id),
    last_seen_timestamp     TEXT NOT NULL,
    seen_count              INTEGER NOT NULL DEFAULT 1,
    source_pointer_va       TEXT,
    source_module           TEXT,
    creation_stack_top4     TEXT,
    creation_thread_id      INTEGER,
    engine_runtime          TEXT NOT NULL CHECK (engine_runtime IN ('OG','NG','AE')),

    bsshader_subclass       TEXT,
    bsshader_technique_bits INTEGER
);

CREATE INDEX IF NOT EXISTS idx_catalog_stage    ON shader_catalog(stage);
CREATE INDEX IF NOT EXISTS idx_catalog_subclass ON shader_catalog(bsshader_subclass);
CREATE INDEX IF NOT EXISTS idx_catalog_runtime  ON shader_catalog(engine_runtime);
CREATE INDEX IF NOT EXISTS idx_catalog_shape    ON shader_catalog(stage, srv_count, output_count, cb_count);
CREATE INDEX IF NOT EXISTS idx_catalog_module   ON shader_catalog(source_module);

CREATE TABLE IF NOT EXISTS compile_events (
    rowid                   INTEGER PRIMARY KEY AUTOINCREMENT,
    result_sha1             TEXT,
    hlsl_source_path        TEXT,
    hlsl_source_sha1        TEXT,
    defines_json            TEXT,
    entry_point             TEXT,
    target                  TEXT,
    compile_flags           INTEGER,
    effect_flags            INTEGER,
    source_name             TEXT,
    timestamp               TEXT NOT NULL,
    session_id              TEXT NOT NULL REFERENCES sessions(session_id),
    source_module           TEXT,
    creation_stack_top4     TEXT,
    creation_thread_id      INTEGER
);

CREATE INDEX IF NOT EXISTS idx_compile_result   ON compile_events(result_sha1);
CREATE INDEX IF NOT EXISTS idx_compile_hlslsha  ON compile_events(hlsl_source_sha1);
CREATE INDEX IF NOT EXISTS idx_compile_session  ON compile_events(session_id);

CREATE TABLE IF NOT EXISTS sessions (
    session_id              TEXT PRIMARY KEY,
    started_at              TEXT NOT NULL,
    ended_at                TEXT,
    engine_runtime          TEXT NOT NULL CHECK (engine_runtime IN ('OG','NG','AE')),
    engine_build_hash       TEXT,
    plugin_version          TEXT NOT NULL,
    plugin_git_sha          TEXT,
    shaders_added_this_session INTEGER NOT NULL DEFAULT 0,
    compiles_observed_this_session INTEGER NOT NULL DEFAULT 0,
    config_snapshot_json    TEXT
);

CREATE INDEX IF NOT EXISTS idx_sessions_runtime ON sessions(engine_runtime);
CREATE INDEX IF NOT EXISTS idx_sessions_started ON sessions(started_at);

CREATE TABLE IF NOT EXISTS corpus_meta (
    key                     TEXT PRIMARY KEY,
    value                   TEXT NOT NULL
);

INSERT OR IGNORE INTO corpus_meta(key, value) VALUES
    ('schema_version', '1'),
    ('schema_source', 'Workspace/schemas/runtime/shader-catalog.sqlite.schema.sql'),
    ('writer_invariant', 'observe-only: hooks compute sha1, enqueue, then call original Create*Shader/D3DCompile with unmodified parameters');
)sql";

	namespace
	{
		std::string IsoNowUtc()
		{
			using namespace std::chrono;
			const auto now = system_clock::now();
			const auto secs = time_point_cast<seconds>(now);
			const auto ms = duration_cast<milliseconds>(now - secs).count();
			const auto tt = system_clock::to_time_t(secs);
			std::tm tm{};
			gmtime_s(&tm, &tt);
			char buf[40];
			std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long long>(ms));
			return std::string(buf);
		}

		std::string NewUuidV4()
		{
			unsigned char b[16] = {};
			BCryptGenRandom(nullptr, b, sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
			b[6] = (b[6] & 0x0F) | 0x40;  // version 4
			b[8] = (b[8] & 0x3F) | 0x80;  // variant 10
			char out[37];
			std::snprintf(out, sizeof(out),
				"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
				b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
				b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
			return std::string(out);
		}

		const char* StageCharToText(char c)
		{
			switch (c) {
				case 'v': return "vs";
				case 'p': return "ps";
				case 'c': return "cs";
				case 'h': return "hs";
				case 'd': return "ds";
				case 'g': return "gs";
				default:  return "ps";
			}
		}
	}

	CatalogDB& CatalogDB::Get()
	{
		static CatalogDB instance;
		return instance;
	}

	CatalogDB::~CatalogDB()
	{
		Stop();
	}

	bool CatalogDB::Start(const DbConfig& cfg, const char* engine_runtime, const char* plugin_version)
	{
		if (_running.load(std::memory_order_acquire))
			return true;

		_cfg = cfg;
		_engine_runtime = engine_runtime ? engine_runtime : "OG";
		_plugin_version = plugin_version ? plugin_version : "0.0.0";
		_session_id = NewUuidV4();

		// Initialize ring sequence numbers: cell i is empty when sequence == i.
		for (std::size_t i = 0; i < kCapacity; ++i)
			_ring[i].sequence.store(i, std::memory_order_relaxed);

		if (!OpenAndBootstrap()) {
			L->error("Failed to open or bootstrap catalog DB at '{}'", _cfg.catalog_path);
			if (_db) { sqlite3_close(_db); _db = nullptr; }
			return false;
		}

		_running.store(true, std::memory_order_release);
		_writer = std::thread(&CatalogDB::WriterLoop, this);
		L->info("Catalog started: session={} runtime={} path='{}'", _session_id, _engine_runtime, _cfg.catalog_path);
		return true;
	}

	void CatalogDB::Stop()
	{
		if (!_running.exchange(false, std::memory_order_acq_rel))
			return;

		bool cleanShutdown = false;
		if (_writer.joinable()) {
			// 5s deadline; if writer is wedged we detach with a warning rather than block shutdown.
			const HANDLE handle = _writer.native_handle();
			const DWORD waitResult = ::WaitForSingleObject(handle, 5000);
			if (waitResult == WAIT_OBJECT_0) {
				_writer.join();
				cleanShutdown = true;
			} else {
				L->warn("Writer thread did not drain within 5s; detaching (DB + statements leaked).");
				_writer.detach();
			}
		} else {
			// Writer never started or already joined; safe to finalize.
			cleanShutdown = true;
		}

		if (cleanShutdown) {
			FinalizeSession();

			if (_insertShader)  { sqlite3_finalize(_insertShader);  _insertShader = nullptr; }
			if (_updateSession) { sqlite3_finalize(_updateSession); _updateSession = nullptr; }
			if (_db)            { sqlite3_close(_db); _db = nullptr; }
		} else {
			// Detached writer still owns the DB + prepared statements; touching them races. Leak
			// is intentional - process is about to exit anyway. WAL preserves committed rows.
			L->warn("Database left open; detached writer still holds resources until process exit.");
		}
	}

	bool CatalogDB::OpenAndBootstrap()
	{
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(_cfg.catalog_path).parent_path(), ec);

		if (sqlite3_open(_cfg.catalog_path.c_str(), &_db) != SQLITE_OK)
			return false;

		char* err = nullptr;
		// Enable foreign-key enforcement; off by default in SQLite. Without this, the schema's
		// FK constraints (shader_catalog.first_seen_session_id REFERENCES sessions, etc.) are
		// silently dead-code and inconsistent rows can slip in.
		sqlite3_exec(_db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }
		// WAL for crash resilience; busy_timeout so concurrent readers (importer) don't trip us.
		sqlite3_exec(_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }
		sqlite3_exec(_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }
		sqlite3_exec(_db, "PRAGMA busy_timeout=2000;", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }

		if (sqlite3_exec(_db, kSchemaSql, nullptr, nullptr, &err) != SQLITE_OK) {
			L->error("Schema bootstrap failed: {}", err ? err : "?");
			if (err) sqlite3_free(err);
			return false;
		}

		// Validate schema version.
		sqlite3_stmt* st = nullptr;
		if (sqlite3_prepare_v2(_db, "SELECT value FROM corpus_meta WHERE key='schema_version'",
			-1, &st, nullptr) == SQLITE_OK)
		{
			if (sqlite3_step(st) == SQLITE_ROW) {
				const auto* v = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
				if (v && std::strcmp(v, "1") != 0) {
					L->error("Schema version mismatch: '{}' (expected '1'); disabling.", v);
					sqlite3_finalize(st);
					return false;
				}
			}
			sqlite3_finalize(st);
		}

		// Insert sessions row.
		{
			const char* sql = "INSERT INTO sessions(session_id, started_at, engine_runtime, plugin_version, config_snapshot_json) "
			                  "VALUES(?, ?, ?, ?, ?)";
			sqlite3_stmt* ins = nullptr;
			if (sqlite3_prepare_v2(_db, sql, -1, &ins, nullptr) == SQLITE_OK) {
				const auto started = IsoNowUtc();
				char cfg[128];
				std::snprintf(cfg, sizeof(cfg),
					"{\"iWriterFlushIntervalMs\":%u}",
					_cfg.flush_interval_ms);
				sqlite3_bind_text(ins, 1, _session_id.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(ins, 2, started.c_str(),     -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(ins, 3, _engine_runtime.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(ins, 4, _plugin_version.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(ins, 5, cfg, -1, SQLITE_TRANSIENT);
				sqlite3_step(ins);
				sqlite3_finalize(ins);
			}
		}

		// Prepare hot statements once; bound per write in the writer.
		const char* kInsertShader =
			"INSERT INTO shader_catalog("
			"  sha1, size_bytes, stage,"
			"  first_seen_timestamp, first_seen_session_id, last_seen_timestamp, seen_count,"
			"  source_pointer_va, source_module, creation_stack_top4, creation_thread_id, engine_runtime"
			") VALUES(?,?,?,?,?,?,1,?,?,?,?,?) "
			"ON CONFLICT(sha1) DO UPDATE SET "
			"  last_seen_timestamp = excluded.last_seen_timestamp, "
			"  seen_count = seen_count + 1";
		if (sqlite3_prepare_v2(_db, kInsertShader, -1, &_insertShader, nullptr) != SQLITE_OK) {
			L->error("prepare insert shader failed: {}", sqlite3_errmsg(_db));
			return false;
		}

		const char* kUpdateSession =
			"UPDATE sessions SET ended_at=?, "
			"  shaders_added_this_session=(SELECT COUNT(*) FROM shader_catalog WHERE first_seen_session_id=?) "
			"WHERE session_id=?";
		if (sqlite3_prepare_v2(_db, kUpdateSession, -1, &_updateSession, nullptr) != SQLITE_OK) {
			L->error("prepare update session failed: {}", sqlite3_errmsg(_db));
			return false;
		}

		return true;
	}

	void CatalogDB::EnqueueShader(const CatalogEntry& e) noexcept
	{
		// MPSC bounded ring; producers claim a slot via CAS on _enqPos. On full ring,
		// drop newest and bump the counter rather than blocking the render thread.
		std::uint64_t pos = _enqPos.load(std::memory_order_relaxed);
		for (;;) {
			Cell& cell = _ring[pos & (kCapacity - 1)];
			const auto seq = cell.sequence.load(std::memory_order_acquire);
			const std::int64_t diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(pos);
			if (diff == 0) {
				if (_enqPos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
					cell.data = e;
					cell.sequence.store(pos + 1, std::memory_order_release);
					_statEnqueued.fetch_add(1, std::memory_order_relaxed);
					return;
				}
			} else if (diff < 0) {
				// Ring full from this producer's view; drop.
				_statDropped.fetch_add(1, std::memory_order_relaxed);
				return;
			} else {
				pos = _enqPos.load(std::memory_order_relaxed);
			}
		}
	}

	CatalogDB::Stats CatalogDB::GetStats() const noexcept
	{
		Stats s;
		s.enqueued = _statEnqueued.load(std::memory_order_relaxed);
		s.dropped  = _statDropped.load(std::memory_order_relaxed);
		s.written  = _statWritten.load(std::memory_order_relaxed);
		return s;
	}

	std::string CatalogDB::ResolveModule(std::uintptr_t va)
	{
		HMODULE mod = nullptr;
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(va), &mod) || !mod)
			return {};

		const auto base = reinterpret_cast<std::uintptr_t>(mod);
		auto it = _moduleCache.find(base);
		if (it == _moduleCache.end()) {
			wchar_t pathW[MAX_PATH] = {};
			GetModuleFileNameW(mod, pathW, MAX_PATH);
			const auto leaf = std::filesystem::path(pathW).filename().string();
			it = _moduleCache.emplace(base, leaf).first;
		}
		char buf[320];
		std::snprintf(buf, sizeof(buf), "%s+0x%llx",
			it->second.c_str(),
			static_cast<unsigned long long>(va - base));
		return std::string(buf);
	}

	std::string CatalogDB::FormatStack(const std::array<void*, 4>& frames)
	{
		std::ostringstream ss;
		bool first = true;
		for (auto* p : frames) {
			if (!p) continue;
			if (!first) ss << " <- ";
			ss << ResolveModule(reinterpret_cast<std::uintptr_t>(p));
			first = false;
		}
		return ss.str();
	}

	void CatalogDB::PersistShader(const CatalogEntry& e)
	{
		if (!_insertShader) return;
		const auto sha1 = Sha1ToHex(Sha1Result{ e.sha1_bytes });
		const auto now = IsoNowUtc();
		char vaBuf[32];
		std::snprintf(vaBuf, sizeof(vaBuf), "0x%llx", static_cast<unsigned long long>(e.source_va));
		const auto srcMod = ResolveModule(e.source_va);
		const auto stack  = FormatStack(e.stack_frames);
		const auto stageText = StageCharToText(e.stage);

		sqlite3_reset(_insertShader);
		sqlite3_clear_bindings(_insertShader);
		sqlite3_bind_text  (_insertShader,  1, sha1.c_str(),            -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64 (_insertShader,  2, static_cast<sqlite3_int64>(e.bytecode_size));
		sqlite3_bind_text  (_insertShader,  3, stageText,               -1, SQLITE_STATIC);
		sqlite3_bind_text  (_insertShader,  4, now.c_str(),             -1, SQLITE_TRANSIENT);
		sqlite3_bind_text  (_insertShader,  5, _session_id.c_str(),     -1, SQLITE_TRANSIENT);
		sqlite3_bind_text  (_insertShader,  6, now.c_str(),             -1, SQLITE_TRANSIENT);
		sqlite3_bind_text  (_insertShader,  7, vaBuf,                   -1, SQLITE_TRANSIENT);
		if (srcMod.empty()) sqlite3_bind_null(_insertShader, 8);
		else                sqlite3_bind_text(_insertShader, 8, srcMod.c_str(), -1, SQLITE_TRANSIENT);
		if (stack.empty()) sqlite3_bind_null(_insertShader, 9);
		else               sqlite3_bind_text(_insertShader, 9, stack.c_str(),   -1, SQLITE_TRANSIENT);
		sqlite3_bind_int   (_insertShader, 10, static_cast<int>(e.thread_id));
		sqlite3_bind_text  (_insertShader, 11, _engine_runtime.c_str(), -1, SQLITE_TRANSIENT);

		if (sqlite3_step(_insertShader) == SQLITE_DONE) {
			_statWritten.fetch_add(1, std::memory_order_relaxed);
		} else {
			L->warn("insert shader failed: {}", sqlite3_errmsg(_db));
		}
	}

	void CatalogDB::WriterLoop()
	{
		using namespace std::chrono;
		std::uint32_t checkpointN = 0;
		const auto flushIv = milliseconds(_cfg.flush_interval_ms == 0 ? 5000 : _cfg.flush_interval_ms);

		while (_running.load(std::memory_order_acquire)) {
			// Sleep up to the flush interval. Shutdown signals via _running flip; the writer
			// will see it on the next wake. Worst case 1 flush-interval shutdown delay.
			std::this_thread::sleep_for(flushIv);

			char* err = nullptr;
			sqlite3_exec(_db, "BEGIN", nullptr, nullptr, &err);
			if (err) { sqlite3_free(err); err = nullptr; }

			// Drain shader ring.
			std::size_t drained = 0;
			for (;;) {
				const auto pos = _deqPos.load(std::memory_order_relaxed);
				Cell& cell = _ring[pos & (kCapacity - 1)];
				const auto seq = cell.sequence.load(std::memory_order_acquire);
				const std::int64_t diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(pos + 1);
				if (diff == 0) {
					CatalogEntry e = cell.data;
					cell.sequence.store(pos + kCapacity, std::memory_order_release);
					_deqPos.store(pos + 1, std::memory_order_relaxed);
					PersistShader(e);
					if (++drained >= 1024) break;  // checkpoint mid-batch on very large bursts
				} else {
					break;
				}
			}

			sqlite3_exec(_db, "COMMIT", nullptr, nullptr, &err);
			if (err) { sqlite3_free(err); err = nullptr; }

			// Periodic WAL truncate so the sidecar doesn't grow unbounded.
			if ((++checkpointN % 12) == 0) {
				sqlite3_exec(_db, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, &err);
				if (err) { sqlite3_free(err); err = nullptr; }
			}
		}

		// Final flush on the way out.
		{
			char* err = nullptr;
			sqlite3_exec(_db, "BEGIN", nullptr, nullptr, &err);
			if (err) { sqlite3_free(err); err = nullptr; }
			for (;;) {
				const auto pos = _deqPos.load(std::memory_order_relaxed);
				Cell& cell = _ring[pos & (kCapacity - 1)];
				const auto seq = cell.sequence.load(std::memory_order_acquire);
				const std::int64_t diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(pos + 1);
				if (diff == 0) {
					CatalogEntry e = cell.data;
					cell.sequence.store(pos + kCapacity, std::memory_order_release);
					_deqPos.store(pos + 1, std::memory_order_relaxed);
					PersistShader(e);
				} else {
					break;
				}
			}
			sqlite3_exec(_db, "COMMIT", nullptr, nullptr, &err);
			if (err) { sqlite3_free(err); err = nullptr; }
		}
	}

	void CatalogDB::FinalizeSession()
	{
		if (!_db || !_updateSession) return;
		const auto now = IsoNowUtc();
		sqlite3_reset(_updateSession);
		sqlite3_clear_bindings(_updateSession);
		sqlite3_bind_text(_updateSession, 1, now.c_str(),         -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(_updateSession, 2, _session_id.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(_updateSession, 3, _session_id.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_step(_updateSession);
	}
}
