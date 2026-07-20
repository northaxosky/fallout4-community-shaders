#include "CatalogDB.h"

#include <Windows.h>
#include <bcrypt.h>
#include <psapi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>

#include <sqlite3.h>

#include "Log.h"
#include "ShaderShape.h"

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace cs::features::catalog
{
	namespace { auto* L = cs::log::Get("cs.feature.shadercatalog"); }

	namespace
	{
		// Enrichment tunables (writer-thread backlog); base rows are never bound by these.
		constexpr std::size_t kEnrichBatch              = 8;                  // shaders reflected per Phase B pass
		constexpr std::size_t kMaxPendingEnrichment     = 512;               // unique shas retained for enrichment
		constexpr std::size_t kMaxRetainedBacklogBytes  = 64ull * 1024 * 1024;  // total retained bytecode cap
		constexpr int         kMaxEnrichAttempts        = 3;                  // transient-failure retries per shader
	}

	// If this schema changes, bump corpus_meta.schema_version (see MigrateSchema); the importer enforces it.
	static const char* kSchemaSql = R"sql(
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
    resource_summary        TEXT,

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

	bool CatalogDB::Start(const DbConfig& cfg, const char* engine_runtime, const char* plugin_version, const char* engine_build)
	{
		if (_running.load(std::memory_order_acquire))
			return true;

		_cfg = cfg;
		_engine_runtime = engine_runtime ? engine_runtime : "OG";
		_engine_build = engine_build ? engine_build : "";
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
		_wakeWriter.notify_one();

		// With _running false, writer skips enrichment after a final base-row flush, bounding join; join avoids UAF of ring/owned bytecode.
		if (_writer.joinable())
			_writer.join();

		FinalizeSession();

		if (_updateShape)       { sqlite3_finalize(_updateShape);       _updateShape = nullptr; }
		if (_insertShader)      { sqlite3_finalize(_insertShader);      _insertShader = nullptr; }
		if (_upsertAttribution) { sqlite3_finalize(_upsertAttribution); _upsertAttribution = nullptr; }
		if (_updateSession)     { sqlite3_finalize(_updateSession);     _updateSession = nullptr; }
		if (_db)                { sqlite3_close(_db); _db = nullptr; }
	}

	bool CatalogDB::MigrateSchema()
	{
		char* err = nullptr;
		auto fail = [&](const char* what) {
			L->error("{}: {}", what, err ? err : sqlite3_errmsg(_db));
			if (err) { sqlite3_free(err); err = nullptr; }
			sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
			return false;
		};

		// One transaction so a half-applied migration rolls back cleanly and is retry-safe.
		if (sqlite3_exec(_db, "BEGIN IMMEDIATE;", nullptr, nullptr, &err) != SQLITE_OK)
			return fail("migration begin failed");

		if (sqlite3_exec(_db, kSchemaSql, nullptr, nullptr, &err) != SQLITE_OK)
			return fail("schema bootstrap failed");

		// Fresh DBs already have resource_summary from the CREATE; only a v1 DB needs the column added.
		bool hasResourceSummary = false;
		{
			sqlite3_stmt* st = nullptr;
			if (sqlite3_prepare_v2(_db, "PRAGMA table_info(shader_catalog)", -1, &st, nullptr) != SQLITE_OK)
				return fail("table_info failed");
			while (sqlite3_step(st) == SQLITE_ROW) {
				const auto* col = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
				if (col && std::strcmp(col, "resource_summary") == 0) {
					hasResourceSummary = true;
					break;
				}
			}
			sqlite3_finalize(st);
		}
		if (!hasResourceSummary) {
			if (sqlite3_exec(_db, "ALTER TABLE shader_catalog ADD COLUMN resource_summary TEXT;",
				nullptr, nullptr, &err) != SQLITE_OK)
				return fail("add resource_summary failed");
		}

		// Shape index is created here (not in kSchemaSql) so the column is guaranteed to exist first.
		if (sqlite3_exec(_db,
			"CREATE INDEX IF NOT EXISTS idx_catalog_shape ON shader_catalog(stage, srv_count, output_count, cb_count);",
			nullptr, nullptr, &err) != SQLITE_OK)
			return fail("create idx_catalog_shape failed");

		// Bump to 2; guarded so re-running never downgrades a newer schema.
		if (sqlite3_exec(_db,
			"INSERT INTO corpus_meta(key, value) VALUES('schema_version','2') "
			"ON CONFLICT(key) DO UPDATE SET value='2' WHERE CAST(corpus_meta.value AS INTEGER) < 2;",
			nullptr, nullptr, &err) != SQLITE_OK)
			return fail("schema_version upsert failed");

		if (sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, &err) != SQLITE_OK)
			return fail("migration commit failed");

		// Validate the version landed at >= 2.
		int version = 0;
		{
			sqlite3_stmt* st = nullptr;
			if (sqlite3_prepare_v2(_db, "SELECT CAST(value AS INTEGER) FROM corpus_meta WHERE key='schema_version'",
				-1, &st, nullptr) == SQLITE_OK) {
				if (sqlite3_step(st) == SQLITE_ROW)
					version = sqlite3_column_int(st, 0);
				sqlite3_finalize(st);
			}
		}
		if (version < 2) {
			L->error("Schema migration did not reach version 2 (got {}); disabling.", version);
			return false;
		}
		return true;
	}

	bool CatalogDB::OpenAndBootstrap()
	{
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(_cfg.catalog_path).parent_path(), ec);

		if (sqlite3_open(_cfg.catalog_path.c_str(), &_db) != SQLITE_OK)
			return false;

		char* err = nullptr;
		// SQLite leaves foreign keys off by default; enable them or schema constraints are inert.
		sqlite3_exec(_db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }
		// WAL for crash resilience; busy_timeout so concurrent readers (importer) don't trip us.
		sqlite3_exec(_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }
		sqlite3_exec(_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }
		sqlite3_exec(_db, "PRAGMA busy_timeout=2000;", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }

		if (!MigrateSchema())
			return false;

		{
			const char* sql = "INSERT INTO sessions(session_id, started_at, engine_runtime, engine_build_hash, plugin_version, config_snapshot_json) "
			                  "VALUES(?, ?, ?, ?, ?, ?)";
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
				if (_engine_build.empty()) sqlite3_bind_null(ins, 4);
				else                       sqlite3_bind_text(ins, 4, _engine_build.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(ins, 5, _plugin_version.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(ins, 6, cfg, -1, SQLITE_TRANSIENT);
				if (sqlite3_step(ins) != SQLITE_DONE)
					L->error("Session row insert failed: {}", sqlite3_errmsg(_db));
				sqlite3_finalize(ins);
			}
		}

		// Prepare hot statements once; writer only re-binds values; base rows leave shape columns NULL for kUpdateShape; COALESCE preserves enrichment across bytecode-less events.
		const char* kInsertShader =
			"INSERT INTO shader_catalog("
			"  sha1, size_bytes, stage,"
			"  first_seen_timestamp, first_seen_session_id, last_seen_timestamp, seen_count,"
			"  source_pointer_va, source_module, creation_stack_top4, creation_thread_id, engine_runtime,"
			"  bsshader_subclass, bsshader_technique_bits,"
			"  profile, cb_count, srv_count, uav_count, sampler_count, output_count, input_count,"
			"  input_has_position_only, instruction_count, sample_call_count,"
			"  input_signature_summary, output_signature_summary, resource_summary"
			") VALUES(?,?,?,?,?,?,1,?,?,?,?,?,?,?, NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL) "
			"ON CONFLICT(sha1) DO UPDATE SET "
			"  last_seen_timestamp = excluded.last_seen_timestamp, "
			"  seen_count = seen_count + 1, "
			"  size_bytes = CASE WHEN shader_catalog.size_bytes = 0 THEN excluded.size_bytes ELSE shader_catalog.size_bytes END, "
			"  source_pointer_va = COALESCE(shader_catalog.source_pointer_va, excluded.source_pointer_va), "
			"  source_module = COALESCE(shader_catalog.source_module, excluded.source_module), "
			"  creation_stack_top4 = COALESCE(shader_catalog.creation_stack_top4, excluded.creation_stack_top4), "
			"  creation_thread_id = COALESCE(shader_catalog.creation_thread_id, excluded.creation_thread_id), "
			"  bsshader_subclass = COALESCE(shader_catalog.bsshader_subclass, excluded.bsshader_subclass), "
			"  bsshader_technique_bits = COALESCE(shader_catalog.bsshader_technique_bits, excluded.bsshader_technique_bits), "
			"  profile = COALESCE(shader_catalog.profile, excluded.profile), "
			"  cb_count = COALESCE(shader_catalog.cb_count, excluded.cb_count), "
			"  srv_count = COALESCE(shader_catalog.srv_count, excluded.srv_count), "
			"  uav_count = COALESCE(shader_catalog.uav_count, excluded.uav_count), "
			"  sampler_count = COALESCE(shader_catalog.sampler_count, excluded.sampler_count), "
			"  output_count = COALESCE(shader_catalog.output_count, excluded.output_count), "
			"  input_count = COALESCE(shader_catalog.input_count, excluded.input_count), "
			"  input_has_position_only = COALESCE(shader_catalog.input_has_position_only, excluded.input_has_position_only), "
			"  instruction_count = COALESCE(shader_catalog.instruction_count, excluded.instruction_count), "
			"  sample_call_count = COALESCE(shader_catalog.sample_call_count, excluded.sample_call_count), "
			"  input_signature_summary = COALESCE(shader_catalog.input_signature_summary, excluded.input_signature_summary), "
			"  output_signature_summary = COALESCE(shader_catalog.output_signature_summary, excluded.output_signature_summary), "
			"  resource_summary = COALESCE(shader_catalog.resource_summary, excluded.resource_summary)";
		if (sqlite3_prepare_v2(_db, kInsertShader, -1, &_insertShader, nullptr) != SQLITE_OK) {
			L->error("prepare insert shader failed: {}", sqlite3_errmsg(_db));
			return false;
		}

		const char* kUpsertAttribution =
			"INSERT INTO shader_catalog("
			"  sha1, size_bytes, stage,"
			"  first_seen_timestamp, first_seen_session_id, last_seen_timestamp, seen_count,"
			"  creation_thread_id, engine_runtime, bsshader_subclass, bsshader_technique_bits"
			") VALUES(?,0,'ps',?,?,?,0,?,?,?,?) "
			"ON CONFLICT(sha1) DO UPDATE SET "
			"  bsshader_subclass = COALESCE(shader_catalog.bsshader_subclass, excluded.bsshader_subclass), "
			"  bsshader_technique_bits = COALESCE(shader_catalog.bsshader_technique_bits, excluded.bsshader_technique_bits)";
		if (sqlite3_prepare_v2(_db, kUpsertAttribution, -1, &_upsertAttribution, nullptr) != SQLITE_OK) {
			L->error("prepare upsert attribution failed: {}", sqlite3_errmsg(_db));
			return false;
		}

		// Enrichment writes shapes directly; the base row already exists, so no COALESCE is needed here.
		const char* kUpdateShape =
			"UPDATE shader_catalog SET "
			"  profile=?1, cb_count=?2, srv_count=?3, uav_count=?4, sampler_count=?5, "
			"  output_count=?6, input_count=?7, input_has_position_only=?8, instruction_count=?9, "
			"  sample_call_count=?10, input_signature_summary=?11, output_signature_summary=?12, "
			"  resource_summary=?13 "
			"WHERE sha1=?14";
		if (sqlite3_prepare_v2(_db, kUpdateShape, -1, &_updateShape, nullptr) != SQLITE_OK) {
			L->error("prepare update shape failed: {}", sqlite3_errmsg(_db));
			return false;
		}

		const char* kUpdateSession =
			"UPDATE sessions SET ended_at=?, "
			"  shaders_added_this_session=(SELECT COUNT(*) FROM shader_catalog WHERE first_seen_session_id=? AND seen_count > 0) "
			"WHERE session_id=?";
		if (sqlite3_prepare_v2(_db, kUpdateSession, -1, &_updateSession, nullptr) != SQLITE_OK) {
			L->error("prepare update session failed: {}", sqlite3_errmsg(_db));
			return false;
		}

		return true;
	}

	void CatalogDB::EnqueueShader(CatalogEntry e) noexcept
	{
		if (!_running.load(std::memory_order_acquire))
			return;  // e (and any owned bytecode) is freed on return.

		// Bounded MPSC ring drops newest on overflow instead of blocking the render thread.
		std::uint64_t pos = _enqPos.load(std::memory_order_relaxed);
		for (;;) {
			Cell& cell = _ring[pos & (kCapacity - 1)];
			const auto seq = cell.sequence.load(std::memory_order_acquire);
			const std::int64_t diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(pos);
			if (diff == 0) {
				if (_enqPos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
					cell.data = std::move(e);
					cell.sequence.store(pos + 1, std::memory_order_release);
					_statEnqueued.fetch_add(1, std::memory_order_relaxed);
					// Wake the writer on empty->nonempty so base rows persist promptly; the timed wait handles a consumer notify race.
					if (_deqPos.load(std::memory_order_acquire) == pos)
						_wakeWriter.notify_one();
					return;
				}
			} else if (diff < 0) {
				_statDropped.fetch_add(1, std::memory_order_relaxed);
				return;  // ring full; e is freed on return.
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
		s.reflected = _statReflected.load(std::memory_order_relaxed);
		s.attributed_ps = _statAttributedPs.load(std::memory_order_relaxed);
		s.total_ps      = _statTotalPs.load(std::memory_order_relaxed);
		s.attribution_events = _statAttributionEvents.load(std::memory_order_relaxed);
		return s;
	}

	void CatalogDB::EnqueueAttribution(const Sha1Result& sha, const char* subclassName, std::uint32_t techniqueBits) noexcept
	{
		if (!subclassName || Sha1IsZero(sha))
			return;

		CatalogEntry e{};
		e.sha1_bytes = sha.bytes;
		e.stage = 'p';
		e.thread_id = ::GetCurrentThreadId();
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		e.timestamp_qpc = c.QuadPart;
		e.subclass_name = subclassName;
		e.technique_bits = techniqueBits;
		e.has_subclass = true;
		e.has_technique_bits = true;
		e.attribution_only = true;
		EnqueueShader(std::move(e));
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

	void CatalogDB::PersistShader(const CatalogEntry& e, const std::string& sha1Hex)
	{
		if (e.attribution_only) {
			PersistAttribution(e);
			return;
		}

		if (!_insertShader) return;
		const auto now = IsoNowUtc();
		char vaBuf[32];
		std::snprintf(vaBuf, sizeof(vaBuf), "0x%llx", static_cast<unsigned long long>(e.source_va));
		const auto srcMod = ResolveModule(e.source_va);
		const auto stack  = FormatStack(e.stack_frames);
		const auto stageText = StageCharToText(e.stage);

		sqlite3_reset(_insertShader);
		sqlite3_clear_bindings(_insertShader);
		sqlite3_bind_text  (_insertShader,  1, sha1Hex.c_str(),         -1, SQLITE_TRANSIENT);
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
		if (e.has_subclass && e.subclass_name) sqlite3_bind_text(_insertShader, 12, e.subclass_name, -1, SQLITE_STATIC);
		else                                   sqlite3_bind_null(_insertShader, 12);
		if (e.has_technique_bits) sqlite3_bind_int64(_insertShader, 13, static_cast<sqlite3_int64>(e.technique_bits));
		else                      sqlite3_bind_null (_insertShader, 13);

		if (sqlite3_step(_insertShader) == SQLITE_DONE) {
			_statWritten.fetch_add(1, std::memory_order_relaxed);
		} else {
			L->warn("insert shader failed: {}", sqlite3_errmsg(_db));
		}
	}

	void CatalogDB::PersistAttribution(const CatalogEntry& e)
	{
		if (!_upsertAttribution || !e.has_subclass || !e.subclass_name)
			return;

		const auto sha1 = Sha1ToHex(Sha1Result{ e.sha1_bytes });
		const auto now = IsoNowUtc();

		sqlite3_reset(_upsertAttribution);
		sqlite3_clear_bindings(_upsertAttribution);
		sqlite3_bind_text (_upsertAttribution, 1, sha1.c_str(),        -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (_upsertAttribution, 2, now.c_str(),         -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (_upsertAttribution, 3, _session_id.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (_upsertAttribution, 4, now.c_str(),         -1, SQLITE_TRANSIENT);
		sqlite3_bind_int  (_upsertAttribution, 5, static_cast<int>(e.thread_id));
		sqlite3_bind_text (_upsertAttribution, 6, _engine_runtime.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (_upsertAttribution, 7, e.subclass_name, -1, SQLITE_STATIC);
		if (e.has_technique_bits) sqlite3_bind_int64(_upsertAttribution, 8, static_cast<sqlite3_int64>(e.technique_bits));
		else                      sqlite3_bind_null (_upsertAttribution, 8);

		if (sqlite3_step(_upsertAttribution) == SQLITE_DONE) {
			_statAttributionEvents.fetch_add(1, std::memory_order_relaxed);
		} else {
			L->warn("upsert attribution failed: {}", sqlite3_errmsg(_db));
		}
	}

	void CatalogDB::RefreshCatalogCounts()
	{
		if (!_db)
			return;

		auto count = [&](const char* sql) -> std::uint64_t {
			sqlite3_stmt* stmt = nullptr;
			std::uint64_t value = 0;
			if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
				if (sqlite3_step(stmt) == SQLITE_ROW)
					value = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 0));
			}
			if (stmt)
				sqlite3_finalize(stmt);
			return value;
		};

		_statTotalPs.store(count("SELECT COUNT(*) FROM shader_catalog WHERE stage='ps'"), std::memory_order_relaxed);
		_statAttributedPs.store(count("SELECT COUNT(*) FROM shader_catalog WHERE stage='ps' AND bsshader_subclass IS NOT NULL"), std::memory_order_relaxed);
	}

	bool CatalogDB::RingHasReady() noexcept
	{
		const auto pos = _deqPos.load(std::memory_order_relaxed);
		Cell& cell = _ring[pos & (kCapacity - 1)];
		const auto seq = cell.sequence.load(std::memory_order_acquire);
		return static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(pos + 1) == 0;
	}

	void CatalogDB::PreloadReflectedShas()
	{
		if (!_db) return;
		// Seed the dedup set from rows already enriched so we don't re-disassemble the whole corpus.
		sqlite3_stmt* st = nullptr;
		if (sqlite3_prepare_v2(_db, "SELECT sha1 FROM shader_catalog WHERE instruction_count IS NOT NULL",
			-1, &st, nullptr) != SQLITE_OK)
			return;
		while (sqlite3_step(st) == SQLITE_ROW) {
			const auto* sha = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
			if (sha)
				_reflectedShas.emplace(sha);
		}
		sqlite3_finalize(st);
	}

	void CatalogDB::IngestPhase()
	{
		if (!_db) return;
		// Retain bytecode only while running; on shutdown we just flush base rows.
		const bool retain = _running.load(std::memory_order_acquire);

		char* err = nullptr;
		sqlite3_exec(_db, "BEGIN", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }

		std::size_t drained = 0;
		for (;;) {
			const auto pos = _deqPos.load(std::memory_order_relaxed);
			Cell& cell = _ring[pos & (kCapacity - 1)];
			const auto seq = cell.sequence.load(std::memory_order_acquire);
			const std::int64_t diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(pos + 1);
			if (diff != 0)
				break;

			CatalogEntry e = std::move(cell.data);
			cell.sequence.store(pos + kCapacity, std::memory_order_release);
			_deqPos.store(pos + 1, std::memory_order_relaxed);

			const std::string sha1Hex = Sha1ToHex(Sha1Result{ e.sha1_bytes });
			PersistShader(e, sha1Hex);

			// Move unseen retained bytecode within caps to the enrichment backlog; otherwise drop only bytecode because the base row already persisted with NULL shapes.
			if (retain && e.bytecode && e.bytecode_size > 0 && !e.attribution_only &&
				_reflectedShas.find(sha1Hex) == _reflectedShas.end() &&
				_pendingShas.find(sha1Hex) == _pendingShas.end() &&
				_pendingShas.size() < kMaxPendingEnrichment &&
				_retainedBytes + e.bytecode_size <= kMaxRetainedBacklogBytes)
			{
				PendingEnrichment pe;
				pe.sha1_hex = sha1Hex;
				pe.size = e.bytecode_size;
				pe.bytecode = std::move(e.bytecode);
				_retainedBytes += pe.size;
				_pendingShas.insert(sha1Hex);
				_enrichQueue.push_back(std::move(pe));
			}

			if (++drained >= 1024) break;  // bound one transaction; the writer loop re-enters for the rest
		}

		sqlite3_exec(_db, "COMMIT", nullptr, nullptr, &err);
		if (err) { sqlite3_free(err); err = nullptr; }
		if (drained > 0)
			RefreshCatalogCounts();
	}

	bool CatalogDB::EnrichPhase()
	{
		if (!_db || !_updateShape)
			return false;

		// Transient DB failures get a bounded retry (keep the only bytecode copy); poison items drop.
		auto requeue = [this](PendingEnrichment&& item) {
			if (++item.attempts >= kMaxEnrichAttempts)
				return;  // give up; bytecode freed here. Sha stays unreflected so a re-create can retry.
			_pendingShas.insert(item.sha1_hex);
			_retainedBytes += item.size;
			_enrichQueue.push_back(std::move(item));
		};

		std::size_t processed = 0;
		while (!_enrichQueue.empty() && processed < kEnrichBatch) {
			PendingEnrichment pe = std::move(_enrichQueue.front());
			_enrichQueue.pop_front();
			_pendingShas.erase(pe.sha1_hex);
			_retainedBytes -= (pe.size <= _retainedBytes) ? pe.size : _retainedBytes;
			++processed;

			// Abandon in-progress enrichment on shutdown; base rows are already durable.
			if (!_running.load(std::memory_order_acquire))
				return false;

			if (_reflectedShas.find(pe.sha1_hex) != _reflectedShas.end())
				continue;  // already enriched; drop the duplicate bytecode.

			ShaderShape shape;
			if (!ExtractShaderShape(pe.bytecode.get(), pe.size, shape) || !shape.extracted)
				continue;  // deterministic reflect+disasm failure; drop (a re-create can retry later).

			char* err = nullptr;
			if (sqlite3_exec(_db, "BEGIN", nullptr, nullptr, &err) != SQLITE_OK) {
				if (err) { sqlite3_free(err); err = nullptr; }
				requeue(std::move(pe));  // transient; keep the bytecode for another attempt.
				continue;
			}

			sqlite3_reset(_updateShape);
			sqlite3_clear_bindings(_updateShape);
			auto bindOptText = [&](int idx, const std::optional<std::string>& v) {
				if (v) sqlite3_bind_text(_updateShape, idx, v->c_str(), -1, SQLITE_TRANSIENT);
				else   sqlite3_bind_null(_updateShape, idx);
			};
			auto bindOptInt = [&](int idx, const std::optional<int>& v) {
				if (v) sqlite3_bind_int(_updateShape, idx, *v);
				else   sqlite3_bind_null(_updateShape, idx);
			};
			bindOptText(1,  shape.profile);
			bindOptInt (2,  shape.cb_count);
			bindOptInt (3,  shape.srv_count);
			bindOptInt (4,  shape.uav_count);
			bindOptInt (5,  shape.sampler_count);
			bindOptInt (6,  shape.output_count);
			bindOptInt (7,  shape.input_count);
			bindOptInt (8,  shape.input_has_position_only);
			bindOptInt (9,  shape.instruction_count);
			bindOptInt (10, shape.sample_call_count);
			bindOptText(11, shape.input_signature_summary);
			bindOptText(12, shape.output_signature_summary);
			bindOptText(13, shape.resource_summary);
			sqlite3_bind_text(_updateShape, 14, pe.sha1_hex.c_str(), -1, SQLITE_TRANSIENT);

			bool committed = false;
			int changed = 0;
			if (sqlite3_step(_updateShape) == SQLITE_DONE) {
				changed = sqlite3_changes(_db);  // read before COMMIT; 0 means the base row isn't present yet
				if (sqlite3_exec(_db, "COMMIT", nullptr, nullptr, &err) == SQLITE_OK) {
					committed = true;
				} else {
					if (err) { sqlite3_free(err); err = nullptr; }
					sqlite3_exec(_db, "ROLLBACK", nullptr, nullptr, nullptr);
				}
			} else {
				L->warn("update shape failed: {}", sqlite3_errmsg(_db));
				sqlite3_exec(_db, "ROLLBACK", nullptr, nullptr, nullptr);
			}

			if (committed && changed > 0) {
				// Mark reflected only after a committed UPDATE that actually hit a row.
				_reflectedShas.insert(pe.sha1_hex);
				_statReflected.fetch_add(1, std::memory_order_relaxed);
			} else {
				// Transient: BEGIN/step/commit failed, or 0 rows changed (base row not persisted yet). Retry bounded.
				requeue(std::move(pe));
			}
		}

		return !_enrichQueue.empty();
	}

	void CatalogDB::WriterLoop()
	{
		using namespace std::chrono;
		std::uint32_t checkpointN = 0;
		const auto flushIv = milliseconds(_cfg.flush_interval_ms == 0 ? 5000 : _cfg.flush_interval_ms);

		PreloadReflectedShas();

		while (_running.load(std::memory_order_acquire)) {
			// Wake on new ingress or pending backlog; timed wait is the fallback so teardown is prompt.
			{
				std::unique_lock lock(_wakeMutex);
				_wakeWriter.wait_for(lock, flushIv, [this] {
					return !_running.load(std::memory_order_acquire) || RingHasReady() || !_enrichQueue.empty();
				});
			}

			// Phase A: FULLY drain the ring to base rows before slow D3DDisassemble enrichment to prevent burst overflow; base-row persistence always wins.
			while (_running.load(std::memory_order_acquire) && RingHasReady())
				IngestPhase();

			if (!_running.load(std::memory_order_acquire))
				break;

			// Phase B: with the ring empty, reflect a small bounded batch, then return to Phase A before the next batch.
			EnrichPhase();

			// Periodically truncate WAL so the sidecar stays bounded.
			if ((++checkpointN % 12) == 0) {
				char* err = nullptr;
				sqlite3_exec(_db, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, &err);
				if (err) { sqlite3_free(err); err = nullptr; }
			}
		}

		// Shutdown: fully drain remaining base rows (bounded by the queued count), then release backlog.
		while (RingHasReady())
			IngestPhase();
		_enrichQueue.clear();
		_pendingShas.clear();
		_reflectedShas.clear();
		_retainedBytes = 0;
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
