#include "CatalogDB.h"

#include "ShaderShape.h"

#include <Windows.h>
#include <psapi.h>

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <utility>

namespace cs::features::catalog
{
	namespace
	{
		constexpr std::size_t kIngestBatch = 256;
		constexpr std::size_t kMaxPendingEnrichment = 512;
		constexpr std::size_t kMaxRetainedBacklogBytes = 64ull * 1024ull * 1024ull;
		constexpr int kMaxTransactionAttempts = 3;
		constexpr int kMaxHresultDetails = 8;

		constexpr const char* kLegacySchemaSql = R"sql(
CREATE TABLE IF NOT EXISTS sessions (
    session_id TEXT PRIMARY KEY,
    started_at TEXT NOT NULL,
    ended_at TEXT,
    engine_runtime TEXT NOT NULL CHECK (engine_runtime IN ('OG','NG','AE')),
    engine_build_hash TEXT,
    plugin_version TEXT NOT NULL,
    plugin_git_sha TEXT,
    shaders_added_this_session INTEGER NOT NULL DEFAULT 0,
    compiles_observed_this_session INTEGER NOT NULL DEFAULT 0,
    config_snapshot_json TEXT
);
CREATE TABLE IF NOT EXISTS shader_catalog (
    sha1 TEXT PRIMARY KEY,
    size_bytes INTEGER NOT NULL,
    stage TEXT NOT NULL CHECK (stage IN ('vs','ps','cs','gs','hs','ds')),
    profile TEXT,
    cb_count INTEGER,
    srv_count INTEGER,
    uav_count INTEGER,
    sampler_count INTEGER,
    output_count INTEGER,
    input_count INTEGER,
    input_has_position_only INTEGER CHECK (input_has_position_only IN (0,1)),
    instruction_count INTEGER,
    sample_call_count INTEGER,
    input_signature_summary TEXT,
    output_signature_summary TEXT,
    resource_summary TEXT,
    first_seen_timestamp TEXT NOT NULL,
    first_seen_session_id TEXT NOT NULL REFERENCES sessions(session_id),
    last_seen_timestamp TEXT NOT NULL,
    seen_count INTEGER NOT NULL DEFAULT 1,
    source_pointer_va TEXT,
    source_module TEXT,
    creation_stack_top4 TEXT,
    creation_thread_id INTEGER,
    engine_runtime TEXT NOT NULL CHECK (engine_runtime IN ('OG','NG','AE')),
    bsshader_subclass TEXT,
    bsshader_technique_bits INTEGER
);
CREATE TABLE IF NOT EXISTS compile_events (
    rowid INTEGER PRIMARY KEY AUTOINCREMENT,
    result_sha1 TEXT,
    hlsl_source_path TEXT,
    hlsl_source_sha1 TEXT,
    defines_json TEXT,
    entry_point TEXT,
    target TEXT,
    compile_flags INTEGER,
    effect_flags INTEGER,
    source_name TEXT,
    timestamp TEXT NOT NULL,
    session_id TEXT NOT NULL REFERENCES sessions(session_id),
    source_module TEXT,
    creation_stack_top4 TEXT,
    creation_thread_id INTEGER
);
CREATE TABLE IF NOT EXISTS corpus_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_catalog_stage ON shader_catalog(stage);
CREATE INDEX IF NOT EXISTS idx_catalog_subclass ON shader_catalog(bsshader_subclass);
CREATE INDEX IF NOT EXISTS idx_catalog_runtime ON shader_catalog(engine_runtime);
CREATE INDEX IF NOT EXISTS idx_catalog_module ON shader_catalog(source_module);
CREATE INDEX IF NOT EXISTS idx_compile_result ON compile_events(result_sha1);
CREATE INDEX IF NOT EXISTS idx_compile_hlslsha ON compile_events(hlsl_source_sha1);
CREATE INDEX IF NOT EXISTS idx_compile_session ON compile_events(session_id);
CREATE INDEX IF NOT EXISTS idx_sessions_runtime ON sessions(engine_runtime);
CREATE INDEX IF NOT EXISTS idx_sessions_started ON sessions(started_at);
)sql";

		constexpr const char* kV3SchemaSql = R"sql(
CREATE TABLE IF NOT EXISTS catalog_runs (
    generated_run_id TEXT PRIMARY KEY,
    external_run_id TEXT,
    scenario_id TEXT,
    config_id TEXT,
    source_id TEXT,
    evidence_mode INTEGER NOT NULL CHECK (evidence_mode IN (0,1)),
    evidence_ids_satisfied INTEGER NOT NULL CHECK (evidence_ids_satisfied IN (0,1)),
    environment_valid INTEGER NOT NULL CHECK (environment_valid IN (0,1)),
    lifecycle TEXT NOT NULL CHECK (lifecycle IN ('running','finalized','abandoned')),
    authoritative INTEGER NOT NULL DEFAULT 0 CHECK (authoritative IN (0,1)),
    started_at TEXT NOT NULL,
    ended_at TEXT,
    process_id INTEGER NOT NULL,
    runtime_family TEXT NOT NULL,
    runtime_version TEXT,
    plugin_version TEXT NOT NULL,
    plugin_build_describe TEXT NOT NULL,
    plugin_git_identity TEXT NOT NULL,
    graphics_adapter TEXT,
    graphics_feature_level TEXT,
    resolution_width INTEGER,
    resolution_height INTEGER,
    writer_drained INTEGER NOT NULL DEFAULT 0 CHECK (writer_drained IN (0,1)),
    raw_export_requested INTEGER NOT NULL CHECK (raw_export_requested IN (0,1)),
    raw_export_complete INTEGER NOT NULL DEFAULT 0 CHECK (raw_export_complete IN (0,1)),
    manifest_published INTEGER NOT NULL DEFAULT 0 CHECK (manifest_published IN (0,1)),
    manifest_relative_path TEXT,
    manifest_sha256 TEXT,
    manifest_size INTEGER,
    artifact_root_fingerprint TEXT NOT NULL,
    publication_pending INTEGER NOT NULL DEFAULT 0 CHECK (publication_pending IN (0,1)),
    pending_authoritative INTEGER NOT NULL DEFAULT 0 CHECK (pending_authoritative IN (0,1)),
    hook_coverage_ready INTEGER NOT NULL DEFAULT 0 CHECK (hook_coverage_ready IN (0,1)),
    orderly_finalizer_ready INTEGER NOT NULL DEFAULT 0 CHECK (orderly_finalizer_ready IN (0,1)),
    CHECK (authoritative=0 OR (
        lifecycle='finalized' AND manifest_published=1 AND publication_pending=0
        AND orderly_finalizer_ready=1
    ))
);
CREATE TABLE IF NOT EXISTS catalog_run_quality (
    generated_run_id TEXT PRIMARY KEY REFERENCES catalog_runs(generated_run_id),
    queue_overflow INTEGER NOT NULL DEFAULT 0 CHECK (queue_overflow >= 0),
    malformed_bytecode INTEGER NOT NULL DEFAULT 0 CHECK (malformed_bytecode >= 0),
    unsupported_size INTEGER NOT NULL DEFAULT 0 CHECK (unsupported_size >= 0),
    allocation_failure INTEGER NOT NULL DEFAULT 0 CHECK (allocation_failure >= 0),
    copy_failure INTEGER NOT NULL DEFAULT 0 CHECK (copy_failure >= 0),
    hash_failure INTEGER NOT NULL DEFAULT 0 CHECK (hash_failure >= 0),
    metadata_truncated INTEGER NOT NULL DEFAULT 0 CHECK (metadata_truncated >= 0),
    db_write_failure INTEGER NOT NULL DEFAULT 0 CHECK (db_write_failure >= 0),
    raw_export_failure INTEGER NOT NULL DEFAULT 0 CHECK (raw_export_failure >= 0),
    manifest_failure INTEGER NOT NULL DEFAULT 0 CHECK (manifest_failure >= 0),
    hook_observer_gap INTEGER NOT NULL DEFAULT 0 CHECK (hook_observer_gap >= 0),
    writer_drain_failure INTEGER NOT NULL DEFAULT 0 CHECK (writer_drain_failure >= 0),
    lifecycle_failure INTEGER NOT NULL DEFAULT 0 CHECK (lifecycle_failure >= 0),
    configuration_failure INTEGER NOT NULL DEFAULT 0 CHECK (configuration_failure >= 0)
);
CREATE TABLE IF NOT EXISTS catalog_content_identities (
    sha256 TEXT PRIMARY KEY,
    sha1 TEXT NOT NULL,
    size_bytes INTEGER NOT NULL CHECK (size_bytes > 0),
    profile TEXT,
    cb_count INTEGER,
    srv_count INTEGER,
    uav_count INTEGER,
    sampler_count INTEGER,
    output_count INTEGER,
    input_count INTEGER,
    input_has_position_only INTEGER CHECK (input_has_position_only IN (0,1)),
    instruction_count INTEGER,
    sample_call_count INTEGER,
    input_signature_summary TEXT,
    output_signature_summary TEXT,
    resource_summary TEXT
);
CREATE TABLE IF NOT EXISTS catalog_run_observations (
    generated_run_id TEXT NOT NULL REFERENCES catalog_runs(generated_run_id),
    observation_key TEXT NOT NULL,
    stage TEXT NOT NULL CHECK (stage IN ('vs','ps','cs','gs','hs','ds')),
    content_sha256 TEXT REFERENCES catalog_content_identities(sha256),
    bytecode_state TEXT NOT NULL CHECK (bytecode_state IN ('null','empty','exact','unsupported_size','allocation_failure','copy_failure','hash_failure')),
    submitted_size INTEGER NOT NULL CHECK (submitted_size >= 0),
    stream_output_digest TEXT,
    stream_output_declaration_state TEXT,
    stream_output_declaration_count INTEGER,
    stream_output_strides_state TEXT,
    stream_output_stride_count INTEGER,
    stream_output_rasterized_stream INTEGER,
    stream_output_metadata_truncated INTEGER CHECK (stream_output_metadata_truncated IN (0,1)),
    stream_output_state TEXT,
    attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts >= 0),
    successes INTEGER NOT NULL DEFAULT 0 CHECK (successes >= 0),
    failures INTEGER NOT NULL DEFAULT 0 CHECK (failures >= 0),
    output_requests INTEGER NOT NULL DEFAULT 0 CHECK (output_requests >= 0),
    null_outputs INTEGER NOT NULL DEFAULT 0 CHECK (null_outputs >= 0),
    raw_output_nonnull INTEGER NOT NULL DEFAULT 0 CHECK (raw_output_nonnull >= 0),
    resolver_invocations INTEGER NOT NULL DEFAULT 0 CHECK (resolver_invocations >= 0),
    resolver_reported_replacements INTEGER NOT NULL DEFAULT 0 CHECK (resolver_reported_replacements >= 0),
    final_stock INTEGER NOT NULL DEFAULT 0 CHECK (final_stock >= 0),
    final_replacement INTEGER NOT NULL DEFAULT 0 CHECK (final_replacement >= 0),
    final_null INTEGER NOT NULL DEFAULT 0 CHECK (final_null >= 0),
    replacement_sha256 TEXT,
    first_sequence INTEGER NOT NULL,
    last_sequence INTEGER NOT NULL,
    first_qpc INTEGER NOT NULL,
    last_qpc INTEGER NOT NULL,
    first_thread_id INTEGER NOT NULL,
    first_module TEXT,
    first_stack TEXT,
    other_hresult_count INTEGER NOT NULL DEFAULT 0 CHECK (other_hresult_count >= 0),
    hresult_details_truncated INTEGER NOT NULL DEFAULT 0 CHECK (hresult_details_truncated IN (0,1)),
    PRIMARY KEY (generated_run_id, observation_key)
);
CREATE TABLE IF NOT EXISTS catalog_run_hresult_details (
    generated_run_id TEXT NOT NULL,
    observation_key TEXT NOT NULL,
    hresult INTEGER NOT NULL,
    occurrence_count INTEGER NOT NULL DEFAULT 1 CHECK (occurrence_count > 0),
    PRIMARY KEY (generated_run_id, observation_key, hresult),
    FOREIGN KEY (generated_run_id, observation_key)
        REFERENCES catalog_run_observations(generated_run_id, observation_key)
);
CREATE TABLE IF NOT EXISTS catalog_run_attributions (
    generated_run_id TEXT NOT NULL REFERENCES catalog_runs(generated_run_id),
    sha1 TEXT NOT NULL,
    subclass_name TEXT NOT NULL,
    technique_bits INTEGER NOT NULL,
    attribution_kind TEXT NOT NULL CHECK (attribution_kind IN ('creation_context','observed_binding','technique_map_association')),
    object_kind TEXT NOT NULL CHECK (object_kind IN ('stock','replacement_unknown','originating_stock','submission_no_object')),
    occurrence_count INTEGER NOT NULL DEFAULT 1 CHECK (occurrence_count > 0),
    first_qpc INTEGER NOT NULL,
    last_qpc INTEGER NOT NULL,
    first_thread_id INTEGER NOT NULL,
    PRIMARY KEY (generated_run_id, sha1, subclass_name, technique_bits,attribution_kind,object_kind)
);
CREATE TABLE IF NOT EXISTS catalog_run_blobs (
    generated_run_id TEXT NOT NULL REFERENCES catalog_runs(generated_run_id),
    sha256 TEXT NOT NULL REFERENCES catalog_content_identities(sha256),
    relative_path TEXT NOT NULL,
    PRIMARY KEY (generated_run_id, sha256)
);
CREATE INDEX IF NOT EXISTS idx_catalog_runs_lifecycle ON catalog_runs(lifecycle);
CREATE INDEX IF NOT EXISTS idx_catalog_runs_external ON catalog_runs(external_run_id);
CREATE INDEX IF NOT EXISTS idx_catalog_observations_content ON catalog_run_observations(generated_run_id, content_sha256);
CREATE INDEX IF NOT EXISTS idx_catalog_observations_stage ON catalog_run_observations(generated_run_id, stage);
CREATE INDEX IF NOT EXISTS idx_catalog_attributions_sha1 ON catalog_run_attributions(generated_run_id, sha1);
CREATE INDEX IF NOT EXISTS idx_catalog_content_sha1 ON catalog_content_identities(sha1);
CREATE INDEX IF NOT EXISTS idx_catalog_run_blobs_content ON catalog_run_blobs(generated_run_id,sha256);
)sql";

		std::string IsoNowUtc()
		{
			using namespace std::chrono;
			const auto now = system_clock::now();
			const auto seconds = time_point_cast<std::chrono::seconds>(now);
			const auto millisecondsPart =
				duration_cast<std::chrono::milliseconds>(now - seconds).count();
			const auto time = system_clock::to_time_t(seconds);
			std::tm utc{};
			gmtime_s(&utc, &time);
			char result[40]{};
			std::snprintf(
				result, sizeof(result),
				"%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
				utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
				utc.tm_hour, utc.tm_min, utc.tm_sec,
				static_cast<long long>(millisecondsPart));
			return result;
		}

		std::shared_ptr<spdlog::logger> Logger()
		{
			if (auto logger = spdlog::get("cs.feature.shadercatalog"); logger)
				return logger;
			return spdlog::default_logger();
		}

		bool Exec(sqlite3* a_db, const char* a_sql, const char* a_operation)
		{
			char* error = nullptr;
			const int result = sqlite3_exec(a_db, a_sql, nullptr, nullptr, &error);
			if (result == SQLITE_OK)
				return true;
			if (auto logger = Logger(); logger) {
				logger->error(
					"{}: {}", a_operation,
					error ? error : sqlite3_errmsg(a_db));
			}
			if (error)
				sqlite3_free(error);
			return false;
		}

		bool Prepare(
			sqlite3* a_db,
			const char* a_sql,
			sqlite3_stmt** a_statement,
			const char* a_operation)
		{
			if (sqlite3_prepare_v2(a_db, a_sql, -1, a_statement, nullptr) == SQLITE_OK)
				return true;
			if (auto logger = Logger(); logger)
				logger->error("{}: {}", a_operation, sqlite3_errmsg(a_db));
			return false;
		}

		bool Reset(sqlite3_stmt* a_statement)
		{
			return sqlite3_reset(a_statement) == SQLITE_OK
				&& sqlite3_clear_bindings(a_statement) == SQLITE_OK;
		}

		bool BindText(sqlite3_stmt* a_statement, int a_index, std::string_view a_value)
		{
			return sqlite3_bind_text(
				a_statement, a_index, a_value.data(),
				static_cast<int>(a_value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
		}

		bool BindOptionalText(
			sqlite3_stmt* a_statement,
			int a_index,
			const std::optional<std::string>& a_value)
		{
			return a_value
				? BindText(a_statement, a_index, *a_value)
				: sqlite3_bind_null(a_statement, a_index) == SQLITE_OK;
		}

		bool BindInt64(sqlite3_stmt* a_statement, int a_index, std::int64_t a_value)
		{
			return sqlite3_bind_int64(
				a_statement, a_index, static_cast<sqlite3_int64>(a_value)) == SQLITE_OK;
		}

		bool BindUInt64(sqlite3_stmt* a_statement, int a_index, std::uint64_t a_value)
		{
			if (a_value > static_cast<std::uint64_t>(
					std::numeric_limits<sqlite3_int64>::max()))
				return false;
			return BindInt64(a_statement, a_index, static_cast<std::int64_t>(a_value));
		}

		std::optional<std::string> ColumnOptionalText(sqlite3_stmt* a_statement, int a_index)
		{
			if (sqlite3_column_type(a_statement, a_index) == SQLITE_NULL)
				return std::nullopt;
			const auto* value = reinterpret_cast<const char*>(
				sqlite3_column_text(a_statement, a_index));
			const int size = sqlite3_column_bytes(a_statement, a_index);
			if (!value || size < 0)
				return std::nullopt;
			return std::string(value, static_cast<std::size_t>(size));
		}

		std::optional<std::string> WideToUtf8(
			std::wstring_view a_value)
		{
			if (a_value.empty())
				return std::string{};
			const int required = WideCharToMultiByte(
				CP_UTF8, WC_ERR_INVALID_CHARS,
				a_value.data(), static_cast<int>(a_value.size()),
				nullptr, 0, nullptr, nullptr);
			if (required <= 0)
				return std::nullopt;
			std::string result(static_cast<std::size_t>(required), '\0');
			if (WideCharToMultiByte(
					CP_UTF8, WC_ERR_INVALID_CHARS,
					a_value.data(), static_cast<int>(a_value.size()),
					result.data(), required, nullptr, nullptr)
				!= required)
				return std::nullopt;
			return result;
		}

		bool TableExists(sqlite3* a_db, const char* a_name)
		{
			sqlite3_stmt* statement = nullptr;
			if (!Prepare(
					a_db,
					"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1",
					&statement, "prepare table existence check"))
				return false;
			const bool bound = BindText(statement, 1, a_name);
			const int first = bound ? sqlite3_step(statement) : SQLITE_ERROR;
			const bool exists = first == SQLITE_ROW;
			const bool complete = (first == SQLITE_DONE)
				|| (first == SQLITE_ROW
					&& sqlite3_step(statement) == SQLITE_DONE);
			sqlite3_finalize(statement);
			return complete && exists;
		}

		bool ColumnExists(sqlite3* a_db, const char* a_table, const char* a_column)
		{
			const std::string sql = "PRAGMA table_info(" + std::string(a_table) + ")";
			sqlite3_stmt* statement = nullptr;
			if (!Prepare(
					a_db, sql.c_str(), &statement,
					"prepare table column inspection"))
				return false;
			bool found = false;
			int step = SQLITE_OK;
			while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
				const auto name = ColumnOptionalText(statement, 1);
				if (name && *name == a_column)
					found = true;
			}
			sqlite3_finalize(statement);
			return step == SQLITE_DONE && found;
		}

		bool TableDefinitionContains(
			sqlite3* a_db,
			const char* a_table,
			std::string_view a_fragment)
		{
			sqlite3_stmt* statement = nullptr;
			if (!Prepare(
					a_db,
					"SELECT sql FROM sqlite_master "
					"WHERE type='table' AND name=?1",
					&statement, "prepare table definition inspection")
				|| !BindText(statement, 1, a_table)) {
				if (statement)
					sqlite3_finalize(statement);
				return false;
			}
			const int row = sqlite3_step(statement);
			const auto sql = row == SQLITE_ROW
				? ColumnOptionalText(statement, 0)
				: std::nullopt;
			const bool complete = row == SQLITE_ROW
				&& sqlite3_step(statement) == SQLITE_DONE;
			sqlite3_finalize(statement);
			return complete && sql
				&& sql->find(a_fragment) != std::string::npos;
		}

		bool ReadSchemaVersion(sqlite3* a_db, int& a_version, std::string& a_error)
		{
			a_version = 0;
			if (!TableExists(a_db, "corpus_meta")) {
				if (TableExists(a_db, "sessions")
					|| TableExists(a_db, "shader_catalog")
					|| TableExists(a_db, "compile_events")) {
					a_error = "legacy tables exist without corpus_meta";
					return false;
				}
				return true;
			}

			sqlite3_stmt* statement = nullptr;
			if (!Prepare(
					a_db,
					"SELECT value FROM corpus_meta WHERE key='schema_version'",
					&statement, "prepare schema version read")) {
				a_error = sqlite3_errmsg(a_db);
				return false;
			}
			const int step = sqlite3_step(statement);
			if (step == SQLITE_DONE) {
				sqlite3_finalize(statement);
				if (TableExists(a_db, "sessions")
					|| TableExists(a_db, "shader_catalog")
					|| TableExists(a_db, "compile_events")) {
					a_error = "corpus_meta has no schema_version";
					return false;
				}
				return true;
			}
			if (step != SQLITE_ROW) {
				a_error = sqlite3_errmsg(a_db);
				sqlite3_finalize(statement);
				return false;
			}
			const auto text = ColumnOptionalText(statement, 0);
			const bool complete = sqlite3_step(statement) == SQLITE_DONE;
			sqlite3_finalize(statement);
			if (!complete || !text) {
				a_error = "schema_version is null";
				return false;
			}
			const char* begin = text->data();
			const char* end = begin + text->size();
			const auto parse = std::from_chars(begin, end, a_version);
			if (parse.ec != std::errc{} || parse.ptr != end || a_version < 1) {
				a_error = "schema_version is not a positive integer";
				return false;
			}
			return true;
		}

		const char* StageText(char a_stage)
		{
			switch (a_stage) {
			case 'v':
				return "vs";
			case 'p':
				return "ps";
			case 'c':
				return "cs";
			case 'g':
				return "gs";
			case 'h':
				return "hs";
			case 'd':
				return "ds";
			default:
				return nullptr;
			}
		}

		const char* LegacyRuntime(std::string_view a_runtime)
		{
			if (a_runtime == "NG")
				return "NG";
			if (a_runtime == "AE")
				return "AE";
			return "OG";
		}

		const char* AttributionKindText(AttributionKind a_kind)
		{
			switch (a_kind) {
			case AttributionKind::kCreationContext:
				return "creation_context";
			case AttributionKind::kObservedBinding:
				return "observed_binding";
			case AttributionKind::kTechniqueMapAssociation:
				return "technique_map_association";
			}
			return "creation_context";
		}

		const char* AttributionObjectKindText(
			AttributionObjectKind a_kind)
		{
			switch (a_kind) {
			case AttributionObjectKind::kStock:
				return "stock";
			case AttributionObjectKind::kReplacementUnknown:
				return "replacement_unknown";
			case AttributionObjectKind::kOriginatingStock:
				return "originating_stock";
			case AttributionObjectKind::kSubmissionNoObject:
				return "submission_no_object";
			}
			return "stock";
		}

		bool IsObservationSuccess(const ObservationOutcome& a_observation)
		{
			return SUCCEEDED(static_cast<HRESULT>(a_observation.hresult))
				&& a_observation.outputRequested
				&& a_observation.outputNonNull;
		}

		std::string Sha1Hex(const std::array<std::uint8_t, 20>& a_sha1)
		{
			return HexLower(a_sha1.data(), a_sha1.size());
		}

		std::string Sha256Hex(const ContentDigest& a_digest)
		{
			return HexLower(a_digest.sha256.data(), a_digest.sha256.size());
		}
	}

	CatalogDB& CatalogDB::Get()
	{
		static auto* instance = new CatalogDB();
		return *instance;
	}

	CatalogDB::ProducerLease::~ProducerLease()
	{
		Reset();
	}

	CatalogDB::ProducerLease::ProducerLease(
		ProducerLease&& a_other) noexcept :
		_owner(std::exchange(a_other._owner, nullptr))
	{}

	CatalogDB::ProducerLease& CatalogDB::ProducerLease::operator=(
		ProducerLease&& a_other) noexcept
	{
		if (this != &a_other) {
			Reset();
			_owner = std::exchange(a_other._owner, nullptr);
		}
		return *this;
	}

	void CatalogDB::ProducerLease::Reset() noexcept
	{
		if (auto* owner = std::exchange(_owner, nullptr))
			owner->ReleaseProducerLease();
	}

	void CatalogDB::ResetState()
	{
		_enqueuePosition.store(0, std::memory_order_relaxed);
		_dequeuePosition.store(0, std::memory_order_relaxed);
		_nextSequence.store(1, std::memory_order_relaxed);
		for (std::size_t i = 0; i < kCapacity; ++i) {
			_ring[i].data = Event{};
			_ring[i].sequence.store(i, std::memory_order_relaxed);
		}

		_enrichmentQueue.clear();
		_queuedEnrichment.clear();
		_enrichedContents.clear();
		_exportedContents.clear();
		_exportAttempts.clear();
		_moduleCache.clear();
		_retainedBytes = 0;
		_graphicsAdapter.reset();
		_graphicsFeatureLevel.reset();
		_statsIdentity = {};

#define RESET_ATOMIC(name, value) name.store(value, std::memory_order_relaxed)
		RESET_ATOMIC(_statAttempts, 0);
		RESET_ATOMIC(_statSuccesses, 0);
		RESET_ATOMIC(_statFailures, 0);
		RESET_ATOMIC(_statUniqueObservations, 0);
		RESET_ATOMIC(_statUniqueContents, 0);
		RESET_ATOMIC(_statAttributionEvents, 0);
		RESET_ATOMIC(_statReflected, 0);
		RESET_ATOMIC(_statAttributedPs, 0);
		RESET_ATOMIC(_statTotalPs, 0);
		RESET_ATOMIC(_lifecycle, 0);
		RESET_ATOMIC(_authoritative, false);
		RESET_ATOMIC(_rawExportComplete, false);
		RESET_ATOMIC(_writerDrained, false);
		RESET_ATOMIC(_hookCoverageReady, false);
		RESET_ATOMIC(_orderlyFinalizerReady, false);
		RESET_ATOMIC(_writerPersistenceHealthy, true);
		RESET_ATOMIC(_producerAdmission, kProducerAdmissionClosed);
		RESET_ATOMIC(_qualityQueueOverflow, 0);
		RESET_ATOMIC(_qualityMalformedBytecode, 0);
		RESET_ATOMIC(_qualityUnsupportedSize, 0);
		RESET_ATOMIC(_qualityAllocationFailure, 0);
		RESET_ATOMIC(_qualityCopyFailure, 0);
		RESET_ATOMIC(_qualityHashFailure, 0);
		RESET_ATOMIC(_qualityMetadataTruncated, 0);
		RESET_ATOMIC(_qualityHresultOverflow, 0);
		RESET_ATOMIC(_qualityDbWriteFailure, 0);
		RESET_ATOMIC(_qualityRawExportFailure, 0);
		RESET_ATOMIC(_qualityManifestFailure, 0);
		RESET_ATOMIC(_qualityHookObserverGap, 0);
		RESET_ATOMIC(_qualityWriterDrainFailure, 0);
		RESET_ATOMIC(_qualityLifecycleFailure, 0);
		RESET_ATOMIC(_qualityConfigurationFailure, 0);
#undef RESET_ATOMIC
	}

	bool CatalogDB::Start(const DbConfig& a_config, RuntimeIdentity a_identity)
	{
		if (_running.load(std::memory_order_acquire)
			|| _accepting.load(std::memory_order_acquire)
			|| _db)
			return false;

		ResetState();
		_config = a_config;
#ifdef FO4CS_SHADER_CATALOG_TESTING
		_orderlyFinalizerReady.store(
			_config.orderlyFinalizerReadyForTesting,
			std::memory_order_relaxed);
#endif
		_identity = std::move(a_identity);
		_policy = _config.policyOverride
			? *_config.policyOverride
			: ReadRunPolicyFromEnvironment();
		const auto generated = GenerateUuidV4();
		if (!generated) {
			if (auto logger = Logger(); logger)
				logger->error("Unable to generate shader catalog run UUID.");
			return false;
		}
		_generatedRunId = *generated;
		_startedAt = IsoNowUtc();

		if (!_policy.environmentValid || !_policy.evidenceIdsSatisfied)
			_qualityConfigurationFailure.fetch_add(1, std::memory_order_relaxed);
		if (_policy.rawExportRequested && !_policy.exportRootValid)
			_qualityRawExportFailure.fetch_add(1, std::memory_order_relaxed);

		if (_config.artifactRootOverride) {
			_artifactRoot = *_config.artifactRootOverride;
			std::error_code ec;
			std::filesystem::create_directories(_artifactRoot, ec);
		} else if (_policy.corpusRoot && _policy.exportRootValid) {
			_artifactRoot = *_policy.corpusRoot;
		} else {
			auto parent = std::filesystem::absolute(
				std::filesystem::path(_config.catalogPath)).parent_path();
			_artifactRoot = parent / "shader-catalog-artifacts";
			std::error_code ec;
			std::filesystem::create_directories(_artifactRoot, ec);
		}
		std::string rootError;
		if (!FingerprintPublicationRoot(
				_artifactRoot, _artifactRootFingerprint, rootError)) {
			if (auto logger = Logger(); logger) {
				logger->error(
					"Unable to identify shader catalog artifact root: {}",
					rootError);
			}
			return false;
		}

		if (!OpenAndBootstrap()) {
			FinalizeStatements();
			if (_db) {
				sqlite3_close(_db);
				_db = nullptr;
			}
			return false;
		}

		StoreStatsIdentity();
		_lifecycle.store(1, std::memory_order_release);
		_producerAdmission.store(0, std::memory_order_release);
		_accepting.store(true, std::memory_order_release);
		_running.store(true, std::memory_order_release);
		_writer = std::thread(&CatalogDB::WriterLoop, this);
		if (auto logger = Logger(); logger) {
			logger->info(
				"Catalog run started: generated_run_id={} external_run_id={} scenario_id={} schema={}",
				_generatedRunId,
				_policy.externalRunId.value_or("null"),
				_policy.scenarioId.value_or("null"),
				kCatalogSchemaVersion);
		}
		return true;
	}

	bool CatalogDB::Stop()
	{
		if (!_db)
			return false;

		_accepting.store(false, std::memory_order_release);
		auto admission = _producerAdmission.fetch_or(
			kProducerAdmissionClosed, std::memory_order_acq_rel);
		admission |= kProducerAdmissionClosed;
		while ((admission & kProducerCountMask) != 0) {
			_producerAdmission.wait(admission, std::memory_order_acquire);
			admission = _producerAdmission.load(std::memory_order_acquire);
		}
		_running.store(false, std::memory_order_release);
		_wakeWriter.notify_one();
		if (_writer.joinable())
			_writer.join();

		const bool drained =
			_enqueuePosition.load(std::memory_order_acquire)
			== _dequeuePosition.load(std::memory_order_acquire);
		_writerDrained.store(drained, std::memory_order_release);
		if (!drained)
			_qualityWriterDrainFailure.fetch_add(1, std::memory_order_relaxed);
		if (!_hookCoverageReady.load(std::memory_order_acquire)) {
			std::uint64_t noReportedGap = 0;
			_qualityHookObserverGap.compare_exchange_strong(
				noReportedGap, 1, std::memory_order_relaxed);
		}

		bool complete =
			_writerPersistenceHealthy.load(std::memory_order_acquire);
		if (!RefreshStats()) {
			_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
			complete = false;
		}
		const std::string endedAt = IsoNowUtc();
		bool associationsComplete = true;
		if (_policy.rawExportRequested
			&& !CheckRawExportAssociations(associationsComplete)) {
			_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
			complete = false;
			associationsComplete = false;
		}
		const bool rawExportComplete =
			!_policy.rawExportRequested
			|| (_policy.exportRootValid
				&& associationsComplete
				&& _qualityRawExportFailure.load(std::memory_order_relaxed) == 0);
		_rawExportComplete.store(rawExportComplete, std::memory_order_release);

		const auto quality = QualitySnapshot();
		bool authoritative = complete
			&& _policy.environmentValid
			&& _policy.evidenceIdsSatisfied
			&& drained
			&& rawExportComplete
			&& _hookCoverageReady.load(std::memory_order_acquire)
			&& _orderlyFinalizerReady.load(std::memory_order_acquire)
			&& !quality.HasLossOrFailure();

		ManifestDocument document;
		if (!complete || !LoadManifestDocument(endedAt, document)) {
			_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
			authoritative = false;
			complete = false;
		}
		if (complete) {
			document.lifecycle = "finalized";
			document.endedAt = endedAt;
			document.writerDrained = drained;
			document.rawExportComplete = rawExportComplete;
			document.manifestPublished = true;
			document.hookCoverageReady =
				_hookCoverageReady.load(std::memory_order_acquire);
			document.orderlyFinalizerReady =
				_orderlyFinalizerReady.load(std::memory_order_acquire);
			document.authoritative = authoritative;
		}

		StagedManifestPublication staged;
		std::string manifestJson;
		std::string manifestSha256;
		if (complete) {
			manifestJson = BuildCanonicalManifest(std::move(document));
			ContentDigest manifestDigest{};
			if (!ComputeDigests(
					manifestJson.data(), manifestJson.size(),
					manifestDigest)) {
				_qualityManifestFailure.fetch_add(1, std::memory_order_relaxed);
				complete = false;
			} else {
				manifestSha256 = HexLower(
					manifestDigest.sha256.data(),
					manifestDigest.sha256.size());
			}
		}
		if (complete) {
			staged = StageManifest(
				_artifactRoot, _generatedRunId,
				manifestJson);
			if (!staged.result.success) {
				_qualityManifestFailure.fetch_add(1, std::memory_order_relaxed);
				complete = false;
			}
		}

		if (complete) {
			bool transactionStarted = false;
			if (_config.finalizationFaults.persistence
				|| !(transactionStarted =
					Exec(_db, "BEGIN IMMEDIATE", "finalization begin"))
				|| !PersistQuality()
				|| !FinalizeLegacySession(endedAt)
				|| !PersistFinalRunState(
					endedAt, rawExportComplete, authoritative,
					manifestSha256, manifestJson.size())
				|| !Exec(_db, "COMMIT", "finalization commit")) {
				if (transactionStarted)
					Exec(_db, "ROLLBACK", "finalization rollback");
				_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
				complete = false;
			}
		}

		if (complete
			&& (_config.finalizationFaults.checkpoint
				|| !Checkpoint(
					SQLITE_CHECKPOINT_PASSIVE,
					"final PASSIVE checkpoint"))) {
			_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
			complete = false;
		}

		if (complete) {
			if (!FinalizeStatements()) {
				_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
				complete = false;
			}
			const int closeResult = sqlite3_close(_db);
			if (closeResult != SQLITE_OK) {
				if (auto logger = Logger(); logger) {
					logger->error(
						"Catalog database close failed: {}",
						sqlite3_errstr(closeResult));
				}
				_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
				complete = false;
			} else {
				_db = nullptr;
			}
		}

		PublicationResult publication;
		if (complete) {
			publication = _config.finalizationFaults.publication
				? PublicationResult{}
				: PublishStagedManifest(staged);
			if (!publication.success) {
				_qualityManifestFailure.fetch_add(1, std::memory_order_relaxed);
				complete = false;
			}
		}

		if (!complete) {
			DiscardStagedManifest(staged);
			authoritative = false;
			RepairFailedPublication(endedAt);
		}

		_authoritative.store(authoritative, std::memory_order_release);
		_lifecycle.store(complete ? 2 : 0, std::memory_order_release);
		if (_db) {
			(void)FinalizeStatements();
			sqlite3_close(_db);
			_db = nullptr;
		}
		return complete && authoritative;
	}

	CatalogDB::ProducerLease CatalogDB::TryAcquireProducerLease() noexcept
	{
		if (!TryBeginProducerAdmission())
			return {};
		return ProducerLease(this);
	}

	bool CatalogDB::TryBeginProducerAdmission() noexcept
	{
		auto admission = _producerAdmission.load(std::memory_order_acquire);
		while ((admission & kProducerAdmissionClosed) == 0) {
			if ((admission & kProducerCountMask) == kProducerCountMask)
				return false;
			if (_producerAdmission.compare_exchange_weak(
					admission, admission + 1,
					std::memory_order_acq_rel,
					std::memory_order_acquire))
				return true;
		}
		return false;
	}

	void CatalogDB::EndProducerAdmission() noexcept
	{
		const auto previous =
			_producerAdmission.fetch_sub(1, std::memory_order_acq_rel);
		if ((previous & kProducerCountMask) == 1)
			_producerAdmission.notify_all();
	}

	void CatalogDB::ReleaseProducerLease() noexcept
	{
		EndProducerAdmission();
	}

	std::uint64_t CatalogDB::NextSequence() noexcept
	{
		return _nextSequence.fetch_add(1, std::memory_order_relaxed);
	}

	void CatalogDB::EnqueueObservation(
		ObservationOutcome a_observation,
		const ProducerLease* a_lease) noexcept
	{
		EnqueueObservationImpl(
			std::move(a_observation), a_lease && *a_lease);
	}

	void CatalogDB::EnqueueObservationAdmitted(
		ObservationOutcome a_observation) noexcept
	{
		EnqueueObservationImpl(std::move(a_observation), true);
	}

	void CatalogDB::EnqueueObservationImpl(
		ObservationOutcome a_observation,
		bool a_admitted) noexcept
	{
		switch (a_observation.prepared.bytecodeState) {
		case BytecodeState::kNull:
		case BytecodeState::kEmpty:
			_qualityMalformedBytecode.fetch_add(1, std::memory_order_relaxed);
			break;
		case BytecodeState::kUnsupportedSize:
			_qualityUnsupportedSize.fetch_add(1, std::memory_order_relaxed);
			break;
		case BytecodeState::kAllocationFailure:
			_qualityAllocationFailure.fetch_add(1, std::memory_order_relaxed);
			break;
		case BytecodeState::kCopyFailure:
			_qualityCopyFailure.fetch_add(1, std::memory_order_relaxed);
			break;
		case BytecodeState::kHashFailure:
			_qualityHashFailure.fetch_add(1, std::memory_order_relaxed);
			break;
		case BytecodeState::kExact:
			break;
		}
		switch (a_observation.prepared.streamOutput.state) {
		case StreamOutputState::kUnsupportedSize:
			_qualityUnsupportedSize.fetch_add(1, std::memory_order_relaxed);
			break;
		case StreamOutputState::kAllocationFailure:
			_qualityAllocationFailure.fetch_add(1, std::memory_order_relaxed);
			break;
		case StreamOutputState::kCopyFailure:
			_qualityCopyFailure.fetch_add(1, std::memory_order_relaxed);
			break;
		case StreamOutputState::kHashFailure:
			_qualityHashFailure.fetch_add(1, std::memory_order_relaxed);
			break;
		case StreamOutputState::kMetadataTruncated:
			_qualityMetadataTruncated.fetch_add(1, std::memory_order_relaxed);
			break;
		case StreamOutputState::kNotApplicable:
		case StreamOutputState::kExact:
			break;
		}

		Event event;
		event.kind = EventKind::kObservation;
		event.observation = std::move(a_observation);
		Enqueue(std::move(event), a_admitted);
	}

	void CatalogDB::EnqueueAttribution(
		const Sha1Result& a_sha,
		const char* a_subclassName,
		std::uint32_t a_techniqueBits,
		AttributionKind a_kind,
		AttributionObjectKind a_objectKind,
		const ProducerLease* a_lease) noexcept
	{
		EnqueueAttributionImpl(
			a_sha, a_subclassName, a_techniqueBits, a_kind,
			a_objectKind, a_lease && *a_lease);
	}

	void CatalogDB::EnqueueAttributionAdmitted(
		const Sha1Result& a_sha,
		const char* a_subclassName,
		std::uint32_t a_techniqueBits,
		AttributionKind a_kind,
		AttributionObjectKind a_objectKind) noexcept
	{
		EnqueueAttributionImpl(
			a_sha, a_subclassName, a_techniqueBits, a_kind,
			a_objectKind, true);
	}

	void CatalogDB::EnqueueAttributionImpl(
		const Sha1Result& a_sha,
		const char* a_subclassName,
		std::uint32_t a_techniqueBits,
		AttributionKind a_kind,
		AttributionObjectKind a_objectKind,
		bool a_admitted) noexcept
	{
		if (!a_subclassName || Sha1IsZero(a_sha))
			return;
		try {
			std::string subclass(a_subclassName);
			if (subclass.empty()
				|| subclass.size() > kMaxCatalogIdentifierBytes
				|| !IsValidUtf8(subclass)) {
				_qualityMetadataTruncated.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			Event event;
			event.kind = EventKind::kAttribution;
			event.attributionSha1 = a_sha.bytes;
			event.attributionSubclass = std::move(subclass);
			event.attributionTechnique = a_techniqueBits;
			event.attributionHasTechnique = true;
			event.attributionKind = a_kind;
			event.attributionObjectKind = a_objectKind;
			event.attributionThreadId = GetCurrentThreadId();
			LARGE_INTEGER counter{};
			if (QueryPerformanceCounter(&counter))
				event.attributionQpc = counter.QuadPart;
			Enqueue(std::move(event), a_admitted);
		} catch (const std::bad_alloc&) {
			_qualityAllocationFailure.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void CatalogDB::SetGraphicsFacts(
		std::optional<std::string> a_adapter,
		std::optional<std::string> a_featureLevel)
	{
		std::scoped_lock lock(_identityMutex);
		_graphicsAdapter = std::move(a_adapter);
		_graphicsFeatureLevel = std::move(a_featureLevel);
	}

	bool CatalogDB::MarkOrderlyFinalizerReady() noexcept
	{
		if (_orderlyFinalizerReady.load(std::memory_order_acquire))
			return true;
		if (!_running.load(std::memory_order_acquire)
			|| _generatedRunId.empty())
			return false;

		sqlite3* database = nullptr;
		sqlite3_stmt* statement = nullptr;
		bool success = false;
		if (sqlite3_open_v2(
				_config.catalogPath.c_str(), &database,
				SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
				nullptr) == SQLITE_OK) {
			(void)sqlite3_busy_timeout(database, 5000);
			if (sqlite3_prepare_v2(
					database,
					"UPDATE catalog_runs SET orderly_finalizer_ready=1 "
					"WHERE generated_run_id=? AND lifecycle='running' "
					"AND authoritative=0 AND publication_pending=0;",
					-1, &statement, nullptr) == SQLITE_OK
				&& BindText(statement, 1, _generatedRunId)
				&& sqlite3_step(statement) == SQLITE_DONE
				&& sqlite3_changes(database) == 1) {
				success = true;
			}
		}
		if (statement)
			sqlite3_finalize(statement);
		if (database)
			sqlite3_close(database);

		if (!success) {
			_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
			if (auto logger = Logger(); logger) {
				logger->error(
					"Unable to persist orderly finalizer readiness; "
					"catalog run cannot be authoritative.");
			}
			return false;
		}
		_orderlyFinalizerReady.store(true, std::memory_order_release);
		return true;
	}

	void CatalogDB::RecordHookObserverGap() noexcept
	{
		_qualityHookObserverGap.fetch_add(1, std::memory_order_relaxed);
	}

	void CatalogDB::RecordAllocationFailure() noexcept
	{
		_qualityAllocationFailure.fetch_add(1, std::memory_order_relaxed);
	}

	void CatalogDB::MarkHookCoverageReady() noexcept
	{
		_hookCoverageReady.store(true, std::memory_order_release);
	}

	bool CatalogDB::Enqueue(Event a_event, bool a_admitted) noexcept
	{
		ProducerLease localLease;
		if (!a_admitted) {
			localLease = TryAcquireProducerLease();
			if (!localLease)
				return false;
		}

		std::uint64_t position = _enqueuePosition.load(std::memory_order_relaxed);
		for (;;) {
			Cell& cell = _ring[position & (kCapacity - 1)];
			const auto sequence = cell.sequence.load(std::memory_order_acquire);
			const auto difference =
				static_cast<std::int64_t>(sequence)
				- static_cast<std::int64_t>(position);
			if (difference == 0) {
				if (_enqueuePosition.compare_exchange_weak(
						position, position + 1, std::memory_order_relaxed)) {
					cell.data = std::move(a_event);
					cell.sequence.store(position + 1, std::memory_order_release);
					if (_dequeuePosition.load(std::memory_order_acquire) == position)
						_wakeWriter.notify_one();
					return true;
				}
			} else if (difference < 0) {
				_qualityQueueOverflow.fetch_add(1, std::memory_order_relaxed);
				return false;
			} else {
				position = _enqueuePosition.load(std::memory_order_relaxed);
			}
		}
	}

	bool CatalogDB::RingHasReady() noexcept
	{
		const auto position = _dequeuePosition.load(std::memory_order_relaxed);
		const Cell& cell = _ring[position & (kCapacity - 1)];
		const auto sequence = cell.sequence.load(std::memory_order_acquire);
		return static_cast<std::int64_t>(sequence)
			- static_cast<std::int64_t>(position + 1) == 0;
	}

	std::vector<CatalogDB::Event> CatalogDB::DequeueBatch()
	{
		std::vector<Event> batch;
		batch.reserve(kIngestBatch);
		while (batch.size() < kIngestBatch) {
			const auto position = _dequeuePosition.load(std::memory_order_relaxed);
			Cell& cell = _ring[position & (kCapacity - 1)];
			const auto sequence = cell.sequence.load(std::memory_order_acquire);
			if (static_cast<std::int64_t>(sequence)
					- static_cast<std::int64_t>(position + 1)
				!= 0)
				break;
			batch.push_back(std::move(cell.data));
			cell.data = Event{};
			cell.sequence.store(position + kCapacity, std::memory_order_release);
			_dequeuePosition.store(position + 1, std::memory_order_relaxed);
		}
		return batch;
	}

	void CatalogDB::WriterLoop()
	{
		using namespace std::chrono;
		const auto interval = milliseconds(
			_config.flushIntervalMs == 0 ? 5000 : _config.flushIntervalMs);
		unsigned checkpointCounter = 0;

		while (_running.load(std::memory_order_acquire) || RingHasReady()) {
			if (!RingHasReady()) {
				std::unique_lock lock(_wakeMutex);
				_wakeWriter.wait_for(lock, interval, [this] {
					return !_running.load(std::memory_order_acquire)
						|| RingHasReady();
				});
			}

			while (RingHasReady()) {
				auto batch = DequeueBatch();
				if (batch.empty())
					break;
				if (PersistBatch(batch))
					ProcessDeferredWork(batch);
				else
					_qualityDbWriteFailure.fetch_add(
						batch.size(), std::memory_order_relaxed);
			}

			if (_running.load(std::memory_order_acquire) && !_enrichmentQueue.empty())
				EnrichOne();
			if (!RefreshStats() || !PersistQuality()) {
				_writerPersistenceHealthy.store(false, std::memory_order_release);
				_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
			}
			if ((++checkpointCounter % 12) == 0
				&& !Checkpoint(SQLITE_CHECKPOINT_PASSIVE, "operational PASSIVE checkpoint")) {
				_writerPersistenceHealthy.store(false, std::memory_order_release);
				_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
			}
		}

		while (!_enrichmentQueue.empty())
			EnrichOne();
		if (!RefreshStats() || !PersistQuality()) {
			_writerPersistenceHealthy.store(false, std::memory_order_release);
			_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
		}
	}

	bool CatalogDB::PersistBatch(std::vector<Event>& a_batch)
	{
		for (int attempt = 0; attempt < kMaxTransactionAttempts; ++attempt) {
			if (!Exec(_db, "BEGIN IMMEDIATE", "observation transaction begin")) {
				Sleep(static_cast<DWORD>((attempt + 1) * 10));
				continue;
			}
			bool success = true;
			for (const auto& event : a_batch) {
				success = event.kind == EventKind::kObservation
					? PersistObservation(event.observation)
					: PersistAttribution(event);
				if (!success)
					break;
			}
			if (success && Exec(_db, "COMMIT", "observation transaction commit"))
				return true;
			Exec(_db, "ROLLBACK", "observation transaction rollback");
			Sleep(static_cast<DWORD>((attempt + 1) * 10));
		}
		return false;
	}

	bool CatalogDB::PersistObservation(const ObservationOutcome& a_observation)
	{
		const auto* stage = StageText(a_observation.prepared.stage);
		if (!stage)
			return false;
		const auto key = ObservationKey(a_observation.prepared);
		std::optional<std::string> sha256;
		std::optional<std::string> sha1;
		if (a_observation.prepared.digest) {
			sha256 = Sha256Hex(*a_observation.prepared.digest);
			sha1 = Sha1Hex(a_observation.prepared.digest->sha1);
			if (!Reset(_upsertContent)
				|| !BindText(_upsertContent, 1, *sha256)
				|| !BindText(_upsertContent, 2, *sha1)
				|| !BindUInt64(
					_upsertContent, 3, a_observation.prepared.submittedSize)
				|| sqlite3_step(_upsertContent) != SQLITE_DONE
				|| sqlite3_changes(_db) != 1)
				return false;
		}

		const bool success = IsObservationSuccess(a_observation);
		const auto module = ResolveModule(a_observation.prepared.sourceVa);
		const auto stack = FormatStack(a_observation.prepared.stackFrames);
		const auto& streamOutput = a_observation.prepared.streamOutput;
		if (!Reset(_upsertObservation)
			|| !BindText(_upsertObservation, 1, _generatedRunId)
			|| !BindText(_upsertObservation, 2, key)
			|| !BindText(_upsertObservation, 3, stage)
			|| !(sha256
				? BindText(_upsertObservation, 4, *sha256)
				: sqlite3_bind_null(_upsertObservation, 4) == SQLITE_OK)
			|| !BindText(
				_upsertObservation, 5,
				BytecodeStateName(a_observation.prepared.bytecodeState))
			|| !BindUInt64(
				_upsertObservation, 6, a_observation.prepared.submittedSize))
			return false;

		if (streamOutput.present) {
			if (!(streamOutput.digestSha256.empty()
					? sqlite3_bind_null(_upsertObservation, 7) == SQLITE_OK
					: BindText(
						_upsertObservation, 7,
						streamOutput.digestSha256))
				|| !BindText(
					_upsertObservation, 8,
					streamOutput.declarationState)
				|| sqlite3_bind_int(
					_upsertObservation, 9,
					static_cast<int>(streamOutput.declarationCount)) != SQLITE_OK
				|| !BindText(
					_upsertObservation, 10,
					streamOutput.stridesState)
				|| sqlite3_bind_int(
					_upsertObservation, 11,
					static_cast<int>(streamOutput.strideCount)) != SQLITE_OK
				|| sqlite3_bind_int64(
					_upsertObservation, 12,
					static_cast<sqlite3_int64>(
						streamOutput.rasterizedStream)) != SQLITE_OK
				|| sqlite3_bind_int(
					_upsertObservation, 13,
					streamOutput.metadataTruncated ? 1 : 0) != SQLITE_OK)
				return false;
		} else {
			for (int index = 7; index <= 13; ++index) {
				if (sqlite3_bind_null(_upsertObservation, index) != SQLITE_OK)
					return false;
			}
		}

		if (sqlite3_bind_int(_upsertObservation, 14, success ? 1 : 0) != SQLITE_OK
			|| sqlite3_bind_int(_upsertObservation, 15, success ? 0 : 1) != SQLITE_OK
			|| sqlite3_bind_int(
				_upsertObservation, 16,
				a_observation.outputRequested ? 1 : 0) != SQLITE_OK
			|| sqlite3_bind_int(
				_upsertObservation, 17,
				a_observation.outputRequested
						&& !a_observation.outputNonNull
					? 1
					: 0) != SQLITE_OK
			|| !BindUInt64(
				_upsertObservation, 18, a_observation.prepared.sequence)
			|| !BindUInt64(
				_upsertObservation, 19, a_observation.prepared.sequence)
			|| !BindInt64(
				_upsertObservation, 20, a_observation.prepared.qpc)
			|| !BindInt64(
				_upsertObservation, 21, a_observation.prepared.qpc)
			|| sqlite3_bind_int64(
				_upsertObservation, 22,
				static_cast<sqlite3_int64>(
					a_observation.prepared.threadId)) != SQLITE_OK
			|| !(module.empty()
				? sqlite3_bind_null(_upsertObservation, 23) == SQLITE_OK
				: BindText(_upsertObservation, 23, module))
			|| !(stack.empty()
				? sqlite3_bind_null(_upsertObservation, 24) == SQLITE_OK
				: BindText(_upsertObservation, 24, stack))
			|| sqlite3_bind_int(
				_upsertObservation, 25,
				a_observation.resolverInvoked ? 1 : 0) != SQLITE_OK
			|| sqlite3_bind_int(
				_upsertObservation, 26,
				a_observation.resolverReportedReplacement ? 1 : 0) != SQLITE_OK
			|| sqlite3_bind_int(
				_upsertObservation, 27,
				a_observation.finalIsStock ? 1 : 0) != SQLITE_OK
			|| sqlite3_bind_int(
				_upsertObservation, 28,
				a_observation.finalIsReplacement ? 1 : 0) != SQLITE_OK
			|| sqlite3_bind_int(
				_upsertObservation, 29,
				a_observation.finalIsNull ? 1 : 0) != SQLITE_OK
			|| sqlite3_bind_null(_upsertObservation, 30) != SQLITE_OK
			|| sqlite3_bind_int(
				_upsertObservation, 31,
				a_observation.outputNonNull ? 1 : 0) != SQLITE_OK
			|| !BindText(
				_upsertObservation, 32,
				StreamOutputStateName(streamOutput.state))
			|| sqlite3_step(_upsertObservation) != SQLITE_DONE)
			return false;

		if (!success) {
			if (!Reset(_upsertHresult)
				|| !BindText(_upsertHresult, 1, _generatedRunId)
				|| !BindText(_upsertHresult, 2, key)
				|| sqlite3_bind_int(
					_upsertHresult, 3, a_observation.hresult) != SQLITE_OK
				|| !BindText(_upsertHresult, 4, _generatedRunId)
				|| !BindText(_upsertHresult, 5, key)
				|| sqlite3_bind_int(
					_upsertHresult, 6, a_observation.hresult) != SQLITE_OK
				|| !BindText(_upsertHresult, 7, _generatedRunId)
				|| !BindText(_upsertHresult, 8, key)
				|| sqlite3_bind_int(
					_upsertHresult, 9, kMaxHresultDetails) != SQLITE_OK
				|| sqlite3_step(_upsertHresult) != SQLITE_DONE)
				return false;
			if (sqlite3_changes(_db) == 0
				&& (!Reset(_upsertHresultOverflow)
					|| !BindText(
						_upsertHresultOverflow, 1, _generatedRunId)
					|| !BindText(_upsertHresultOverflow, 2, key)
					|| sqlite3_step(_upsertHresultOverflow) != SQLITE_DONE
					|| sqlite3_changes(_db) != 1))
				return false;
		}
		return PersistLegacyObservation(a_observation);
	}

	bool CatalogDB::PersistAttribution(const Event& a_event)
	{
		const auto sha1 = Sha1Hex(a_event.attributionSha1);
		const std::int64_t technique = a_event.attributionHasTechnique
			? static_cast<std::int64_t>(a_event.attributionTechnique)
			: -1;
		if (!Reset(_upsertAttribution)
			|| !BindText(_upsertAttribution, 1, _generatedRunId)
			|| !BindText(_upsertAttribution, 2, sha1)
			|| !BindText(
				_upsertAttribution, 3, a_event.attributionSubclass)
			|| !BindInt64(_upsertAttribution, 4, technique)
			|| !BindText(
				_upsertAttribution, 5,
				AttributionKindText(a_event.attributionKind))
			|| !BindText(
				_upsertAttribution, 6,
				AttributionObjectKindText(
					a_event.attributionObjectKind))
			|| !BindInt64(
				_upsertAttribution, 7, a_event.attributionQpc)
			|| !BindInt64(
				_upsertAttribution, 8, a_event.attributionQpc)
			|| sqlite3_bind_int64(
				_upsertAttribution, 9,
				static_cast<sqlite3_int64>(
					a_event.attributionThreadId)) != SQLITE_OK
			|| sqlite3_step(_upsertAttribution) != SQLITE_DONE)
			return false;
		return PersistLegacyAttribution(a_event);
	}

	bool CatalogDB::PersistLegacyObservation(
		const ObservationOutcome& a_observation)
	{
		if (!a_observation.prepared.digest)
			return true;
		const auto sha1 = Sha1Hex(a_observation.prepared.digest->sha1);
		const auto* stage = StageText(a_observation.prepared.stage);
		const auto now = IsoNowUtc();
		const auto module = ResolveModule(a_observation.prepared.sourceVa);
		const auto stack = FormatStack(a_observation.prepared.stackFrames);
		char pointer[32]{};
		std::snprintf(
			pointer, sizeof(pointer), "0x%llx",
			static_cast<unsigned long long>(
				a_observation.prepared.sourceVa));
		if (!Reset(_insertLegacyShader)
			|| !BindText(_insertLegacyShader, 1, sha1)
			|| !BindUInt64(
				_insertLegacyShader, 2,
				a_observation.prepared.submittedSize)
			|| !BindText(_insertLegacyShader, 3, stage)
			|| !BindText(_insertLegacyShader, 4, now)
			|| !BindText(_insertLegacyShader, 5, _generatedRunId)
			|| !BindText(_insertLegacyShader, 6, now)
			|| !BindText(_insertLegacyShader, 7, pointer)
			|| !(module.empty()
				? sqlite3_bind_null(_insertLegacyShader, 8) == SQLITE_OK
				: BindText(_insertLegacyShader, 8, module))
			|| !(stack.empty()
				? sqlite3_bind_null(_insertLegacyShader, 9) == SQLITE_OK
				: BindText(_insertLegacyShader, 9, stack))
			|| sqlite3_bind_int64(
				_insertLegacyShader, 10,
				static_cast<sqlite3_int64>(
					a_observation.prepared.threadId)) != SQLITE_OK
			|| !BindText(
				_insertLegacyShader, 11,
				LegacyRuntime(_identity.runtimeFamily))
			|| sqlite3_bind_null(_insertLegacyShader, 12) != SQLITE_OK
			|| sqlite3_bind_null(_insertLegacyShader, 13) != SQLITE_OK
			|| sqlite3_step(_insertLegacyShader) != SQLITE_DONE)
			return false;
		return true;
	}

	bool CatalogDB::PersistLegacyAttribution(const Event& a_event)
	{
		const auto sha1 = Sha1Hex(a_event.attributionSha1);
		const auto now = IsoNowUtc();
		if (!Reset(_upsertLegacyAttribution)
			|| !BindText(_upsertLegacyAttribution, 1, sha1)
			|| !BindText(_upsertLegacyAttribution, 2, now)
			|| !BindText(
				_upsertLegacyAttribution, 3, _generatedRunId)
			|| !BindText(_upsertLegacyAttribution, 4, now)
			|| sqlite3_bind_int64(
				_upsertLegacyAttribution, 5,
				static_cast<sqlite3_int64>(
					a_event.attributionThreadId)) != SQLITE_OK
			|| !BindText(
				_upsertLegacyAttribution, 6,
				LegacyRuntime(_identity.runtimeFamily))
			|| !BindText(
				_upsertLegacyAttribution, 7,
				a_event.attributionSubclass))
			return false;
		if (a_event.attributionHasTechnique) {
			if (!BindUInt64(
					_upsertLegacyAttribution, 8,
					a_event.attributionTechnique))
				return false;
		} else if (sqlite3_bind_null(
					   _upsertLegacyAttribution, 8)
				   != SQLITE_OK) {
			return false;
		}
		return sqlite3_step(_upsertLegacyAttribution) == SQLITE_DONE;
	}

	void CatalogDB::ProcessDeferredWork(std::vector<Event>& a_batch)
	{
		for (auto& event : a_batch) {
			if (event.kind != EventKind::kObservation)
				continue;
			auto& prepared = event.observation.prepared;
			if (prepared.bytecodeState != BytecodeState::kExact
				|| !prepared.digest
				|| !prepared.bytecode)
				continue;
			ExportObservation(event.observation);
			const auto sha256 = Sha256Hex(*prepared.digest);
			if (_enrichedContents.contains(sha256)
				|| _queuedEnrichment.contains(sha256)
				|| _enrichmentQueue.size() >= kMaxPendingEnrichment
				|| _retainedBytes + prepared.submittedSize
					> kMaxRetainedBacklogBytes)
				continue;
			PendingEnrichment item;
			item.sha256 = sha256;
			item.sha1 = Sha1Hex(prepared.digest->sha1);
			item.size = prepared.submittedSize;
			item.bytecode = std::move(prepared.bytecode);
			_retainedBytes += item.size;
			_queuedEnrichment.insert(item.sha256);
			_enrichmentQueue.push_back(std::move(item));
		}
	}

	void CatalogDB::ExportObservation(
		const ObservationOutcome& a_observation)
	{
		if (!_policy.rawExportRequested || !_policy.exportRootValid
			|| !a_observation.prepared.digest
			|| !a_observation.prepared.bytecode)
			return;
		const auto sha256 = Sha256Hex(*a_observation.prepared.digest);
		if (_exportedContents.contains(sha256))
			return;

		PublicationResult publication;
		for (int attempt = 0; attempt < 3; ++attempt) {
			publication = PublishBlob(
				*_policy.corpusRoot, sha256,
				a_observation.prepared.bytecode.get(),
				a_observation.prepared.submittedSize);
			if (publication.success)
				break;
			Sleep(static_cast<DWORD>((attempt + 1) * 10));
		}
		if (!publication.success) {
			_qualityRawExportFailure.fetch_add(1, std::memory_order_relaxed);
			if (auto logger = Logger(); logger) {
				logger->warn(
					"Raw shader export failed for {}: {}",
					sha256, publication.error);
			}
			return;
		}
		const auto relative = publication.relativePath.generic_string();
		bool linked = false;
		for (int attempt = 0; attempt < kMaxTransactionAttempts; ++attempt) {
			linked = Reset(_upsertRunBlob)
				&& BindText(_upsertRunBlob, 1, _generatedRunId)
				&& BindText(_upsertRunBlob, 2, sha256)
				&& BindText(_upsertRunBlob, 3, relative)
				&& sqlite3_step(_upsertRunBlob) == SQLITE_DONE
				&& sqlite3_changes(_db) == 1;
			if (linked)
				break;
			Sleep(static_cast<DWORD>((attempt + 1) * 10));
		}
		if (!linked) {
			_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		_exportedContents.insert(sha256);
	}

	void CatalogDB::EnrichOne()
	{
		if (_enrichmentQueue.empty())
			return;
		auto item = std::move(_enrichmentQueue.front());
		_enrichmentQueue.pop_front();
		_queuedEnrichment.erase(item.sha256);
		_retainedBytes -= std::min(_retainedBytes, item.size);
		if (UpdateContentShape(item)) {
			_enrichedContents.insert(item.sha256);
			_statReflected.fetch_add(1, std::memory_order_relaxed);
		}
	}

	bool CatalogDB::UpdateContentShape(const PendingEnrichment& a_item)
	{
		ShaderShape shape;
		if (!ExtractShaderShape(
				a_item.bytecode.get(), a_item.size, shape)
			|| !shape.extracted)
			return false;

		auto bindShape = [](sqlite3_stmt* a_statement, const ShaderShape& a_shape) {
			auto text = [&](int a_index, const std::optional<std::string>& a_value) {
				return BindOptionalText(a_statement, a_index, a_value);
			};
			auto integer = [&](int a_index, const std::optional<int>& a_value) {
				return a_value
					? sqlite3_bind_int(a_statement, a_index, *a_value) == SQLITE_OK
					: sqlite3_bind_null(a_statement, a_index) == SQLITE_OK;
			};
			return text(1, a_shape.profile)
				&& integer(2, a_shape.cb_count)
				&& integer(3, a_shape.srv_count)
				&& integer(4, a_shape.uav_count)
				&& integer(5, a_shape.sampler_count)
				&& integer(6, a_shape.output_count)
				&& integer(7, a_shape.input_count)
				&& integer(8, a_shape.input_has_position_only)
				&& integer(9, a_shape.instruction_count)
				&& integer(10, a_shape.sample_call_count)
				&& text(11, a_shape.input_signature_summary)
				&& text(12, a_shape.output_signature_summary)
				&& text(13, a_shape.resource_summary);
		};

		if (!Exec(_db, "BEGIN IMMEDIATE", "shape transaction begin"))
			return false;
		const bool contentUpdated =
			Reset(_updateContentShape)
			&& bindShape(_updateContentShape, shape)
			&& BindText(_updateContentShape, 14, a_item.sha256)
			&& sqlite3_step(_updateContentShape) == SQLITE_DONE;
		const bool legacyUpdated =
			Reset(_updateLegacyShape)
			&& bindShape(_updateLegacyShape, shape)
			&& BindText(_updateLegacyShape, 14, a_item.sha1)
			&& sqlite3_step(_updateLegacyShape) == SQLITE_DONE;
		if (contentUpdated && legacyUpdated
			&& Exec(_db, "COMMIT", "shape transaction commit"))
			return true;
		Exec(_db, "ROLLBACK", "shape transaction rollback");
		_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	QualityCounters CatalogDB::QualitySnapshot() const noexcept
	{
		QualityCounters result;
		result.queueOverflow = _qualityQueueOverflow.load(std::memory_order_relaxed);
		result.malformedBytecode = _qualityMalformedBytecode.load(std::memory_order_relaxed);
		result.unsupportedSize = _qualityUnsupportedSize.load(std::memory_order_relaxed);
		result.allocationFailure = _qualityAllocationFailure.load(std::memory_order_relaxed);
		result.copyFailure = _qualityCopyFailure.load(std::memory_order_relaxed);
		result.hashFailure = _qualityHashFailure.load(std::memory_order_relaxed);
		result.metadataTruncated = _qualityMetadataTruncated.load(std::memory_order_relaxed);
		result.metadataTruncated +=
			_qualityHresultOverflow.load(std::memory_order_relaxed);
		result.dbWriteFailure = _qualityDbWriteFailure.load(std::memory_order_relaxed);
		result.rawExportFailure = _qualityRawExportFailure.load(std::memory_order_relaxed);
		result.manifestFailure = _qualityManifestFailure.load(std::memory_order_relaxed);
		result.hookObserverGap = _qualityHookObserverGap.load(std::memory_order_relaxed);
		result.writerDrainFailure = _qualityWriterDrainFailure.load(std::memory_order_relaxed);
		result.lifecycleFailure = _qualityLifecycleFailure.load(std::memory_order_relaxed);
		result.configurationFailure = _qualityConfigurationFailure.load(std::memory_order_relaxed);
		return result;
	}

	bool CatalogDB::PersistQuality()
	{
		if (!_updateQuality)
			return false;
		const auto quality = QualitySnapshot();
		if (!Reset(_updateQuality))
			return false;
		const std::uint64_t values[] = {
			quality.queueOverflow,
			quality.malformedBytecode,
			quality.unsupportedSize,
			quality.allocationFailure,
			quality.copyFailure,
			quality.hashFailure,
			quality.metadataTruncated,
			quality.dbWriteFailure,
			quality.rawExportFailure,
			quality.manifestFailure,
			quality.hookObserverGap,
			quality.writerDrainFailure,
			quality.lifecycleFailure,
			quality.configurationFailure
		};
		for (int i = 0; i < static_cast<int>(std::size(values)); ++i) {
			if (!BindUInt64(_updateQuality, i + 1, values[i]))
				return false;
		}
		return BindText(_updateQuality, 15, _generatedRunId)
			&& sqlite3_step(_updateQuality) == SQLITE_DONE;
	}

	bool CatalogDB::RefreshStats()
	{
		sqlite3_stmt* statement = nullptr;
		constexpr const char* sql =
			"SELECT "
			"COALESCE(SUM(attempts),0), COALESCE(SUM(successes),0), "
			"COALESCE(SUM(failures),0), COUNT(*), "
			"COUNT(DISTINCT content_sha256), "
			"(SELECT COALESCE(SUM(occurrence_count),0) FROM catalog_run_attributions WHERE generated_run_id=?1), "
			"SUM(CASE WHEN stage='ps' THEN 1 ELSE 0 END), "
			"SUM(CASE WHEN stage='ps' AND EXISTS("
			" SELECT 1 FROM catalog_run_attributions a "
			" JOIN catalog_content_identities c ON c.sha1=a.sha1 "
			" WHERE a.generated_run_id=?1 AND c.sha256=catalog_run_observations.content_sha256"
			") THEN 1 ELSE 0 END), "
			"COALESCE(SUM(other_hresult_count),0) "
			"FROM catalog_run_observations WHERE generated_run_id=?1";
		if (!Prepare(_db, sql, &statement, "prepare run stats")
			|| !BindText(statement, 1, _generatedRunId)) {
			if (statement)
				sqlite3_finalize(statement);
			return false;
		}

		if (sqlite3_step(statement) != SQLITE_ROW) {
			sqlite3_finalize(statement);
			return false;
		}
		_statAttempts.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0)),
			std::memory_order_relaxed);
		_statSuccesses.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 1)),
			std::memory_order_relaxed);
		_statFailures.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 2)),
			std::memory_order_relaxed);
		_statUniqueObservations.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 3)),
			std::memory_order_relaxed);
		_statUniqueContents.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 4)),
			std::memory_order_relaxed);
		_statAttributionEvents.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 5)),
			std::memory_order_relaxed);
		_statTotalPs.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 6)),
			std::memory_order_relaxed);
		_statAttributedPs.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 7)),
			std::memory_order_relaxed);
		_qualityHresultOverflow.store(
			static_cast<std::uint64_t>(sqlite3_column_int64(statement, 8)),
			std::memory_order_relaxed);
		const bool complete = sqlite3_step(statement) == SQLITE_DONE;
		sqlite3_finalize(statement);
		return complete;
	}

	bool CatalogDB::CheckRawExportAssociations(bool& a_complete)
	{
		a_complete = true;
		sqlite3_stmt* statement = nullptr;
		if (!Prepare(
				_db,
				"SELECT exact.sha256,b.relative_path FROM ("
				"SELECT DISTINCT content_sha256 AS sha256 "
				"FROM catalog_run_observations "
				"WHERE generated_run_id=?1 AND bytecode_state='exact' "
				"AND content_sha256 IS NOT NULL"
				") exact LEFT JOIN catalog_run_blobs b "
				"ON b.generated_run_id=?2 AND b.sha256=exact.sha256 "
				"ORDER BY exact.sha256",
				&statement, "prepare raw export association check")
			|| !BindText(statement, 1, _generatedRunId)
			|| !BindText(statement, 2, _generatedRunId)) {
			if (statement)
				sqlite3_finalize(statement);
			return false;
		}
		int step = SQLITE_OK;
		while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
			const auto sha256 =
				ColumnOptionalText(statement, 0).value_or("");
			const auto path = ColumnOptionalText(statement, 1);
			if (!IsLowerHexDigest(sha256, 64)
				|| !path
				|| *path != BlobRelativePath(sha256)) {
				a_complete = false;
			}
		}
		sqlite3_finalize(statement);
		return step == SQLITE_DONE;
	}

	CatalogDB::Stats CatalogDB::GetStats() const noexcept
	{
		Stats result;
		try {
			std::scoped_lock lock(_identityMutex);
			result = _statsIdentity;
		} catch (...) {
		}
		const int lifecycle = _lifecycle.load(std::memory_order_acquire);
		result.lifecycle = lifecycle == 1
			? "running"
			: (lifecycle == 2 ? "finalized" : "inactive");
		result.authoritative = _authoritative.load(std::memory_order_acquire);
		result.rawExportComplete = _rawExportComplete.load(std::memory_order_acquire);
		result.writerDrained = _writerDrained.load(std::memory_order_acquire);
		result.hookCoverageReady =
			_hookCoverageReady.load(std::memory_order_acquire);
		result.orderlyFinalizerReady =
			_orderlyFinalizerReady.load(std::memory_order_acquire);
		result.attempts = _statAttempts.load(std::memory_order_relaxed);
		result.successes = _statSuccesses.load(std::memory_order_relaxed);
		result.failures = _statFailures.load(std::memory_order_relaxed);
		result.uniqueObservations = _statUniqueObservations.load(std::memory_order_relaxed);
		result.uniqueContents = _statUniqueContents.load(std::memory_order_relaxed);
		result.attributionEvents = _statAttributionEvents.load(std::memory_order_relaxed);
		result.reflected = _statReflected.load(std::memory_order_relaxed);
		result.attributedPs = _statAttributedPs.load(std::memory_order_relaxed);
		result.totalPs = _statTotalPs.load(std::memory_order_relaxed);
		result.quality = QualitySnapshot();
		return result;
	}

	void CatalogDB::StoreStatsIdentity()
	{
		std::scoped_lock lock(_identityMutex);
		_statsIdentity.generatedRunId = _generatedRunId;
		_statsIdentity.externalRunId = _policy.externalRunId;
		_statsIdentity.scenarioId = _policy.scenarioId;
		_statsIdentity.rawExportRequested = _policy.rawExportRequested;
	}

	std::string CatalogDB::ResolveModule(std::uintptr_t a_address)
	{
		if (a_address == 0)
			return {};
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
					| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(a_address), &module)
			|| !module)
			return {};
		const auto base = reinterpret_cast<std::uintptr_t>(module);
		auto found = _moduleCache.find(base);
		if (found == _moduleCache.end()) {
			wchar_t path[MAX_PATH]{};
			if (!GetModuleFileNameW(module, path, MAX_PATH))
				return {};
			const auto filename = WideToUtf8(
				std::filesystem::path(path).filename().native());
			if (!filename)
				return {};
			found = _moduleCache.emplace(
				base, *filename).first;
		}
		char result[320]{};
		std::snprintf(
			result, sizeof(result), "%s+0x%llx",
			found->second.c_str(),
			static_cast<unsigned long long>(a_address - base));
		return result;
	}

	std::string CatalogDB::FormatStack(
		const std::array<std::uintptr_t, 4>& a_frames)
	{
		std::ostringstream result;
		bool first = true;
		for (const auto frame : a_frames) {
			const auto module = ResolveModule(frame);
			if (module.empty())
				continue;
			if (!first)
				result << " <- ";
			result << module;
			first = false;
		}
		return result.str();
	}

	bool CatalogDB::OpenAndBootstrap()
	{
		const auto databasePath = std::filesystem::path(_config.catalogPath);
		std::error_code ec;
		if (!databasePath.parent_path().empty())
			std::filesystem::create_directories(databasePath.parent_path(), ec);
		if (ec) {
			if (auto logger = Logger(); logger)
				logger->error("Unable to create catalog directory: {}", ec.message());
			return false;
		}

		if (sqlite3_open_v2(
				_config.catalogPath.c_str(), &_db,
				SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
				nullptr) != SQLITE_OK)
			return false;
		sqlite3_extended_result_codes(_db, 1);
		if (!Exec(_db, "PRAGMA foreign_keys=ON", "enable foreign keys")
			|| !Exec(_db, "PRAGMA busy_timeout=2000", "set busy timeout")
			|| !Exec(_db, "PRAGMA synchronous=NORMAL", "set synchronous mode"))
			return false;

		sqlite3_stmt* journal = nullptr;
		if (!Prepare(
				_db, "PRAGMA journal_mode=WAL", &journal,
				"prepare WAL mode")
			|| sqlite3_step(journal) != SQLITE_ROW) {
			if (journal)
				sqlite3_finalize(journal);
			return false;
		}
		const auto journalMode = ColumnOptionalText(journal, 0);
		const bool journalComplete =
			sqlite3_step(journal) == SQLITE_DONE;
		sqlite3_finalize(journal);
		if (!journalComplete || !journalMode || *journalMode != "wal") {
			if (auto logger = Logger(); logger)
				logger->error("Catalog database refused WAL journal mode.");
			return false;
		}

		if (!MigrateSchema()
			|| !RecoverPublicationWindows()
			|| !RecoverAbandonedRuns()
			|| !InsertRun()
			|| !PrepareStatements())
			return false;
		return true;
	}

	bool CatalogDB::MigrateSchema()
	{
		sqlite3_stmt* integrity = nullptr;
		if (!Prepare(
				_db, "PRAGMA quick_check", &integrity,
				"prepare database integrity check")
			|| sqlite3_step(integrity) != SQLITE_ROW) {
			if (integrity)
				sqlite3_finalize(integrity);
			return false;
		}
		const auto integrityResult = ColumnOptionalText(integrity, 0);
		const bool integrityComplete =
			sqlite3_step(integrity) == SQLITE_DONE;
		sqlite3_finalize(integrity);
		if (!integrityComplete
			|| !integrityResult || *integrityResult != "ok") {
			if (auto logger = Logger(); logger)
				logger->error("Catalog database is corrupt; refusing migration.");
			return false;
		}

		int version = 0;
		std::string versionError;
		if (!ReadSchemaVersion(_db, version, versionError)) {
			if (auto logger = Logger(); logger)
				logger->error("Catalog schema rejected: {}", versionError);
			return false;
		}
		if (version > kCatalogSchemaVersion) {
			if (auto logger = Logger(); logger) {
				logger->error(
					"Catalog schema {} is newer than supported {}; refusing to open.",
					version, kCatalogSchemaVersion);
			}
			return false;
		}

		if (!Exec(_db, "BEGIN IMMEDIATE", "schema migration begin"))
			return false;
		bool success = Exec(_db, kLegacySchemaSql, "legacy schema bootstrap");
		if (success && !ColumnExists(
				_db, "shader_catalog", "resource_summary")) {
			success = Exec(
				_db,
				"ALTER TABLE shader_catalog ADD COLUMN resource_summary TEXT",
				"migrate schema v1 resource summary");
		}
		success = success && Exec(
			_db,
			"CREATE INDEX IF NOT EXISTS idx_catalog_shape "
			"ON shader_catalog(stage, srv_count, output_count, cb_count)",
			"create legacy shape index");
		const bool rebuildObservations = success
			&& TableExists(_db, "catalog_run_observations")
			&& !TableDefinitionContains(
				_db, "catalog_run_observations", "'copy_failure'");
		const bool observationsHaveRawOutput = rebuildObservations
			&& ColumnExists(
				_db, "catalog_run_observations", "raw_output_nonnull");
		const bool observationsHaveOutputRequests = rebuildObservations
			&& ColumnExists(
				_db, "catalog_run_observations", "output_requests");
		const bool observationsHaveOtherHresults = rebuildObservations
			&& ColumnExists(
				_db, "catalog_run_observations", "other_hresult_count");
		const bool observationsHaveHresultTruncation = rebuildObservations
			&& ColumnExists(
				_db, "catalog_run_observations",
				"hresult_details_truncated");
		const bool observationsHaveStreamOutputState = rebuildObservations
			&& ColumnExists(
				_db, "catalog_run_observations",
				"stream_output_state");
		if (rebuildObservations) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_run_hresult_details "
				"RENAME TO catalog_run_hresult_details_legacy_v3",
				"stage HRESULT table rebuild")
				&& Exec(
					_db,
					"ALTER TABLE catalog_run_observations "
					"RENAME TO catalog_run_observations_legacy_v3",
					"stage observation table rebuild");
		}
		const bool attributionsHaveKinds = success
			&& TableExists(_db, "catalog_run_attributions")
			&& ColumnExists(
				_db, "catalog_run_attributions", "attribution_kind");
		const bool rebuildAttributions = success
			&& TableExists(_db, "catalog_run_attributions")
			&& (!attributionsHaveKinds
				|| !TableDefinitionContains(
					_db, "catalog_run_attributions",
					"'technique_map_association'")
				|| !TableDefinitionContains(
					_db, "catalog_run_attributions",
					"'submission_no_object'"));
		if (rebuildAttributions) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_run_attributions "
				"RENAME TO catalog_run_attributions_legacy_v3",
				"stage attribution table rebuild");
		}
		success = success
			&& Exec(_db, kV3SchemaSql, "schema v3 bootstrap");
		if (success && !ColumnExists(_db, "catalog_runs", "resolution_width")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs ADD COLUMN resolution_width INTEGER",
				"add run resolution width");
		}
		if (success && !ColumnExists(_db, "catalog_runs", "resolution_height")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs ADD COLUMN resolution_height INTEGER",
				"add run resolution height");
		}
		if (success && !ColumnExists(
				_db, "catalog_run_observations", "output_requests")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_run_observations "
				"ADD COLUMN output_requests INTEGER NOT NULL DEFAULT 0 "
				"CHECK (output_requests >= 0)",
				"add observation output requests");
		}
		if (success && !ColumnExists(
				_db, "catalog_run_observations", "raw_output_nonnull")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_run_observations "
				"ADD COLUMN raw_output_nonnull INTEGER NOT NULL DEFAULT 0 "
				"CHECK (raw_output_nonnull >= 0)",
				"add raw output counter");
		}
		if (success && !ColumnExists(
				_db, "catalog_run_observations", "other_hresult_count")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_run_observations "
				"ADD COLUMN other_hresult_count INTEGER NOT NULL DEFAULT 0 "
				"CHECK (other_hresult_count >= 0)",
				"add HRESULT overflow counter");
		}
		if (success && !ColumnExists(
				_db, "catalog_run_observations",
				"hresult_details_truncated")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_run_observations "
				"ADD COLUMN hresult_details_truncated INTEGER NOT NULL DEFAULT 0 "
				"CHECK (hresult_details_truncated IN (0,1))",
				"add HRESULT truncation flag");
		}
		if (success && !ColumnExists(
				_db, "catalog_run_observations",
				"stream_output_state")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_run_observations "
				"ADD COLUMN stream_output_state TEXT",
				"add stream-output state");
		}
		if (success && !ColumnExists(
				_db, "catalog_runs", "publication_pending")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs "
				"ADD COLUMN publication_pending INTEGER NOT NULL DEFAULT 0 "
				"CHECK (publication_pending IN (0,1))",
				"add publication pending flag");
		}
		const bool addPendingAuthoritative = success && !ColumnExists(
			_db, "catalog_runs", "pending_authoritative");
		if (addPendingAuthoritative) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs "
				"ADD COLUMN pending_authoritative INTEGER NOT NULL DEFAULT 0 "
				"CHECK (pending_authoritative IN (0,1))",
				"add pending authority flag");
			if (success) {
				success = Exec(
					_db,
					"UPDATE catalog_runs SET "
					"pending_authoritative=authoritative "
					"WHERE publication_pending=1",
					"preserve pending authority");
			}
		}
		if (success && !ColumnExists(
				_db, "catalog_runs", "manifest_sha256")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs "
				"ADD COLUMN manifest_sha256 TEXT",
				"add manifest digest");
		}
		if (success && !ColumnExists(
				_db, "catalog_runs", "manifest_size")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs "
				"ADD COLUMN manifest_size INTEGER",
				"add manifest size");
		}
		if (success && !ColumnExists(
				_db, "catalog_runs", "artifact_root_fingerprint")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs "
				"ADD COLUMN artifact_root_fingerprint TEXT "
				"NOT NULL DEFAULT ''",
				"add artifact root fingerprint");
		}
		if (success && !ColumnExists(
				_db, "catalog_runs", "hook_coverage_ready")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs "
				"ADD COLUMN hook_coverage_ready INTEGER NOT NULL DEFAULT 0 "
				"CHECK (hook_coverage_ready IN (0,1))",
				"add hook coverage readiness");
		}
		if (success && !ColumnExists(
				_db, "catalog_runs", "orderly_finalizer_ready")) {
			success = Exec(
				_db,
				"ALTER TABLE catalog_runs "
				"ADD COLUMN orderly_finalizer_ready INTEGER NOT NULL DEFAULT 0 "
				"CHECK (orderly_finalizer_ready IN (0,1))",
				"add orderly finalizer readiness");
		}
		if (success) {
			success = Exec(
				_db,
				"UPDATE catalog_runs SET lifecycle='running',"
				"authoritative=0,manifest_published=0 "
				"WHERE publication_pending=1",
				"normalize pending publication state")
				&& Exec(
					_db,
					"UPDATE catalog_runs SET authoritative=0,"
					"pending_authoritative=0 "
					"WHERE orderly_finalizer_ready=0",
					"normalize orderly finalizer authority")
				&& Exec(
					_db,
					"DROP TRIGGER IF EXISTS catalog_runs_authority_insert",
					"drop authority insert invariant")
				&& Exec(
					_db,
					"DROP TRIGGER IF EXISTS catalog_runs_authority_update",
					"drop authority update invariant")
				&& Exec(
					_db,
					"CREATE TRIGGER "
					"catalog_runs_authority_insert "
					"BEFORE INSERT ON catalog_runs WHEN "
					"(NEW.publication_pending=1 AND "
					"(NEW.lifecycle='finalized' OR NEW.authoritative=1 "
					"OR NEW.manifest_published=1)) OR "
					"(NEW.authoritative=1 AND NOT "
					"(NEW.lifecycle='finalized' AND "
					"NEW.manifest_published=1 AND "
					"NEW.publication_pending=0 AND "
					"NEW.orderly_finalizer_ready=1)) "
					"BEGIN SELECT RAISE(ABORT,'invalid catalog authority'); END",
					"create authority insert invariant")
				&& Exec(
					_db,
					"CREATE TRIGGER "
					"catalog_runs_authority_update "
					"BEFORE UPDATE ON catalog_runs WHEN "
					"(NEW.publication_pending=1 AND "
					"(NEW.lifecycle='finalized' OR NEW.authoritative=1 "
					"OR NEW.manifest_published=1)) OR "
					"(NEW.authoritative=1 AND NOT "
					"(NEW.lifecycle='finalized' AND "
					"NEW.manifest_published=1 AND "
					"NEW.publication_pending=0 AND "
					"NEW.orderly_finalizer_ready=1)) "
					"BEGIN SELECT RAISE(ABORT,'invalid catalog authority'); END",
					"create authority update invariant");
		}
		if (success && rebuildAttributions) {
			const char* copySql = attributionsHaveKinds
				? "INSERT INTO catalog_run_attributions("
				  "generated_run_id,sha1,subclass_name,technique_bits,"
				  "attribution_kind,object_kind,occurrence_count,"
				  "first_qpc,last_qpc,first_thread_id) "
				  "SELECT generated_run_id,sha1,subclass_name,technique_bits,"
				  "attribution_kind,object_kind,occurrence_count,"
				  "first_qpc,last_qpc,first_thread_id "
				  "FROM catalog_run_attributions_legacy_v3"
				: "INSERT INTO catalog_run_attributions("
				  "generated_run_id,sha1,subclass_name,technique_bits,"
				  "attribution_kind,object_kind,occurrence_count,"
				  "first_qpc,last_qpc,first_thread_id) "
				  "SELECT generated_run_id,sha1,subclass_name,technique_bits,"
				  "'creation_context','stock',occurrence_count,"
				  "first_qpc,last_qpc,first_thread_id "
				  "FROM catalog_run_attributions_legacy_v3";
			success = Exec(
				_db, copySql, "copy legacy v3 attribution rows")
				&& Exec(
					_db,
					"DROP TABLE catalog_run_attributions_legacy_v3",
					"drop rebuilt attribution source")
				&& Exec(
					_db,
					"CREATE INDEX IF NOT EXISTS idx_catalog_attributions_sha1 "
					"ON catalog_run_attributions(generated_run_id,sha1)",
					"recreate attribution index");
		}
		if (success && rebuildObservations) {
			std::string copyObservations =
				"INSERT INTO catalog_run_observations("
				"generated_run_id,observation_key,stage,content_sha256,"
				"bytecode_state,submitted_size,stream_output_digest,"
				"stream_output_declaration_state,"
				"stream_output_declaration_count,stream_output_strides_state,"
				"stream_output_stride_count,stream_output_rasterized_stream,"
				"stream_output_metadata_truncated,attempts,successes,failures,"
				"output_requests,null_outputs,resolver_invocations,"
				"resolver_reported_replacements,final_stock,final_replacement,"
				"final_null,replacement_sha256,first_sequence,last_sequence,"
				"first_qpc,last_qpc,first_thread_id,first_module,first_stack,"
				"raw_output_nonnull,other_hresult_count,"
				"hresult_details_truncated,stream_output_state) "
				"SELECT generated_run_id,observation_key,stage,content_sha256,"
				"bytecode_state,submitted_size,stream_output_digest,"
				"stream_output_declaration_state,"
				"stream_output_declaration_count,stream_output_strides_state,"
				"stream_output_stride_count,stream_output_rasterized_stream,"
				"stream_output_metadata_truncated,attempts,successes,failures,";
			copyObservations += observationsHaveOutputRequests
				? "output_requests,"
				: "0,";
			copyObservations +=
				"null_outputs,resolver_invocations,"
				"resolver_reported_replacements,final_stock,final_replacement,"
				"final_null,replacement_sha256,first_sequence,last_sequence,"
				"first_qpc,last_qpc,first_thread_id,first_module,first_stack,";
			copyObservations += observationsHaveRawOutput
				? "raw_output_nonnull,"
				: "0,";
			copyObservations += observationsHaveOtherHresults
				? "other_hresult_count,"
				: "0,";
			copyObservations += observationsHaveHresultTruncation
				? "hresult_details_truncated,"
				: "0,";
			copyObservations += observationsHaveStreamOutputState
				? "stream_output_state "
				: "NULL ";
			copyObservations +=
				"FROM catalog_run_observations_legacy_v3";
			success = Exec(
				_db, copyObservations.c_str(),
				"copy legacy v3 observation rows")
				&& Exec(
					_db,
					"INSERT INTO catalog_run_hresult_details("
					"generated_run_id,observation_key,hresult,occurrence_count) "
					"SELECT generated_run_id,observation_key,hresult,"
					"occurrence_count "
					"FROM catalog_run_hresult_details_legacy_v3",
					"copy legacy v3 HRESULT rows")
				&& Exec(
					_db,
					"DROP TABLE catalog_run_hresult_details_legacy_v3",
					"drop rebuilt HRESULT source")
				&& Exec(
					_db,
					"DROP TABLE catalog_run_observations_legacy_v3",
					"drop rebuilt observation source")
				&& Exec(
					_db,
					"CREATE INDEX IF NOT EXISTS "
					"idx_catalog_observations_content "
					"ON catalog_run_observations("
					"generated_run_id,content_sha256)",
					"recreate observation content index")
				&& Exec(
					_db,
					"CREATE INDEX IF NOT EXISTS "
					"idx_catalog_observations_stage "
					"ON catalog_run_observations(generated_run_id,stage)",
					"recreate observation stage index");
		}
		success = success
			&& Exec(
				_db,
				"INSERT INTO corpus_meta(key,value) VALUES"
				"('schema_source','FO4CommunityShaders ShaderCatalog schema v3') "
				"ON CONFLICT(key) DO UPDATE SET value=excluded.value",
				"update schema source")
			&& Exec(
				_db,
				"INSERT INTO corpus_meta(key,value) VALUES"
				"('writer_invariant','prepare bounded identity before Create*Shader; persist outcome after original call') "
				"ON CONFLICT(key) DO UPDATE SET value=excluded.value",
				"update writer invariant");

		const std::pair<const char*, const char*> requiredColumns[] = {
			{ "sessions", "session_id" },
			{ "sessions", "started_at" },
			{ "shader_catalog", "sha1" },
			{ "shader_catalog", "resource_summary" },
			{ "compile_events", "session_id" },
			{ "catalog_runs", "generated_run_id" },
			{ "catalog_runs", "publication_pending" },
			{ "catalog_runs", "pending_authoritative" },
			{ "catalog_runs", "manifest_sha256" },
			{ "catalog_runs", "manifest_size" },
			{ "catalog_runs", "artifact_root_fingerprint" },
			{ "catalog_runs", "hook_coverage_ready" },
			{ "catalog_runs", "orderly_finalizer_ready" },
			{ "catalog_run_quality", "generated_run_id" },
			{ "catalog_content_identities", "sha256" },
			{ "catalog_run_observations", "observation_key" },
			{ "catalog_run_observations", "other_hresult_count" },
			{ "catalog_run_observations", "stream_output_state" },
			{ "catalog_run_hresult_details", "hresult" },
			{ "catalog_run_attributions", "attribution_kind" },
			{ "catalog_run_attributions", "object_kind" },
			{ "catalog_run_blobs", "relative_path" }
		};
		for (const auto& [table, column] : requiredColumns)
			success = success && ColumnExists(_db, table, column);

		sqlite3_stmt* foreignKeys = nullptr;
		if (success) {
			success = Prepare(
				_db, "PRAGMA foreign_key_check", &foreignKeys,
				"prepare foreign key check");
			if (success)
				success = sqlite3_step(foreignKeys) == SQLITE_DONE;
		}
		if (foreignKeys)
			sqlite3_finalize(foreignKeys);

		if (success) {
			success = Exec(
				_db,
				"INSERT INTO corpus_meta(key,value) VALUES('schema_version','3') "
				"ON CONFLICT(key) DO UPDATE SET value='3'",
				"set schema version");
		}
		if (!success || !Exec(_db, "COMMIT", "schema migration commit")) {
			Exec(_db, "ROLLBACK", "schema migration rollback");
			return false;
		}

		int migratedVersion = 0;
		if (!ReadSchemaVersion(_db, migratedVersion, versionError)
			|| migratedVersion != kCatalogSchemaVersion) {
			if (auto logger = Logger(); logger)
				logger->error("Catalog schema v3 verification failed.");
			return false;
		}

		return true;
	}

	bool CatalogDB::RecoverPublicationWindows()
	{
		struct Pending
		{
			std::string runId;
			std::string relativePath;
			std::string sha256;
			std::size_t size = 0;
			std::string rootFingerprint;
		};
		std::vector<Pending> pending;
		sqlite3_stmt* query = nullptr;
		if (!Prepare(
				_db,
				"SELECT generated_run_id,manifest_relative_path,"
				"manifest_sha256,manifest_size,artifact_root_fingerprint "
				"FROM catalog_runs WHERE publication_pending=1",
				&query, "prepare publication recovery query")) {
			if (query)
				sqlite3_finalize(query);
			return false;
		}
		int step = SQLITE_OK;
		while ((step = sqlite3_step(query)) == SQLITE_ROW) {
			Pending item;
			item.runId = ColumnOptionalText(query, 0).value_or("");
			item.relativePath =
				ColumnOptionalText(query, 1).value_or("");
			item.sha256 = ColumnOptionalText(query, 2).value_or("");
			const auto size = sqlite3_column_int64(query, 3);
			if (size > 0)
				item.size = static_cast<std::size_t>(size);
			item.rootFingerprint =
				ColumnOptionalText(query, 4).value_or("");
			pending.push_back(std::move(item));
		}
		sqlite3_finalize(query);
		if (step != SQLITE_DONE)
			return false;
		if (pending.empty())
			return true;
		if (!Exec(_db, "BEGIN IMMEDIATE", "publication recovery begin"))
			return false;
		bool success = true;
		std::vector<PinnedPublishedFile> pinnedFiles;
		pinnedFiles.reserve(pending.size());
		for (const auto& item : pending) {
			const auto expected = std::filesystem::path("runs")
				/ item.runId / "manifest.v1.json";
			PinnedPublishedFile pinned;
			if (item.rootFingerprint == _artifactRootFingerprint
				&& item.relativePath == expected.generic_string()) {
				pinned = PinPublishedFile(
					_artifactRoot, expected, item.size, item.sha256);
			}
			const bool confirmed = pinned.success;
			if (confirmed)
				pinnedFiles.push_back(std::move(pinned));
			sqlite3_stmt* update = nullptr;
			const char* sql = confirmed
				? "UPDATE catalog_runs SET lifecycle='finalized',"
				  "authoritative=pending_authoritative,"
				  "manifest_published=1,publication_pending=0,"
				  "pending_authoritative=0 "
				  "WHERE generated_run_id=?1 AND publication_pending=1"
				: "UPDATE catalog_runs SET lifecycle='abandoned',"
				  "authoritative=0,manifest_published=0,"
				  "manifest_relative_path=NULL,manifest_sha256=NULL,"
				  "manifest_size=NULL,publication_pending=0,"
				  "pending_authoritative=0 "
				  "WHERE generated_run_id=?1 AND publication_pending=1";
			success = Prepare(
				_db, sql, &update, "prepare publication recovery update")
				&& BindText(update, 1, item.runId)
				&& sqlite3_step(update) == SQLITE_DONE
				&& sqlite3_changes(_db) == 1;
			if (update)
				sqlite3_finalize(update);
			if (!success)
				break;
			if (!confirmed) {
				sqlite3_stmt* quality = nullptr;
				success = Prepare(
					_db,
					"UPDATE catalog_run_quality SET "
					"lifecycle_failure=lifecycle_failure+1 "
					"WHERE generated_run_id=?1",
					&quality,
					"prepare publication recovery quality")
					&& BindText(quality, 1, item.runId)
					&& sqlite3_step(quality) == SQLITE_DONE
					&& sqlite3_changes(_db) == 1;
				if (quality)
					sqlite3_finalize(quality);
				if (!success)
					break;
			}
		}
		if (success
			&& Exec(_db, "COMMIT", "publication recovery commit"))
			return true;
		Exec(_db, "ROLLBACK", "publication recovery rollback");
		return false;
	}

	bool CatalogDB::RecoverAbandonedRuns()
	{
		if (!Exec(_db, "BEGIN IMMEDIATE", "abandoned-run recovery begin"))
			return false;
		const auto now = IsoNowUtc();
		sqlite3_stmt* quality = nullptr;
		sqlite3_stmt* runs = nullptr;
		const bool prepared =
			Prepare(
				_db,
				"UPDATE catalog_run_quality SET lifecycle_failure=lifecycle_failure+1 "
				"WHERE generated_run_id IN "
				"(SELECT generated_run_id FROM catalog_runs WHERE lifecycle='running')",
				&quality, "prepare abandoned quality update")
			&& Prepare(
				_db,
				"UPDATE catalog_runs SET lifecycle='abandoned', ended_at=?1, "
				"authoritative=0 WHERE lifecycle='running'",
				&runs, "prepare abandoned run update");
		const bool success = prepared
			&& sqlite3_step(quality) == SQLITE_DONE
			&& BindText(runs, 1, now)
			&& sqlite3_step(runs) == SQLITE_DONE;
		if (quality)
			sqlite3_finalize(quality);
		if (runs)
			sqlite3_finalize(runs);
		if (success && Exec(_db, "COMMIT", "abandoned-run recovery commit"))
			return true;
		Exec(_db, "ROLLBACK", "abandoned-run recovery rollback");
		return false;
	}

	bool CatalogDB::InsertRun()
	{
		if (!Exec(_db, "BEGIN IMMEDIATE", "run insert begin"))
			return false;
		sqlite3_stmt* run = nullptr;
		sqlite3_stmt* quality = nullptr;
		sqlite3_stmt* legacy = nullptr;
		constexpr const char* runSql =
			"INSERT INTO catalog_runs("
			"generated_run_id,external_run_id,scenario_id,config_id,source_id,"
			"evidence_mode,evidence_ids_satisfied,environment_valid,lifecycle,"
			"started_at,process_id,runtime_family,runtime_version,plugin_version,"
			"plugin_build_describe,plugin_git_identity,raw_export_requested,"
			"artifact_root_fingerprint,orderly_finalizer_ready"
			") VALUES(?1,?2,?3,?4,?5,?6,?7,?8,'running',?9,?10,?11,?12,?13,?14,?15,?16,?17,?18)";
		constexpr const char* legacySql =
			"INSERT INTO sessions("
			"session_id,started_at,engine_runtime,engine_build_hash,"
			"plugin_version,plugin_git_sha,config_snapshot_json"
			") VALUES(?1,?2,?3,?4,?5,?6,?7)";
		bool success =
			Prepare(_db, runSql, &run, "prepare run insert")
			&& Prepare(
				_db,
				"INSERT INTO catalog_run_quality(generated_run_id) VALUES(?1)",
				&quality, "prepare run quality insert")
			&& Prepare(_db, legacySql, &legacy, "prepare legacy session insert");
		if (success) {
			success =
				BindText(run, 1, _generatedRunId)
				&& BindOptionalText(run, 2, _policy.externalRunId)
				&& BindOptionalText(run, 3, _policy.scenarioId)
				&& BindOptionalText(run, 4, _policy.configId)
				&& BindOptionalText(run, 5, _policy.sourceId)
				&& sqlite3_bind_int(run, 6, _policy.evidenceMode ? 1 : 0) == SQLITE_OK
				&& sqlite3_bind_int(
					run, 7, _policy.evidenceIdsSatisfied ? 1 : 0) == SQLITE_OK
				&& sqlite3_bind_int(
					run, 8, _policy.environmentValid ? 1 : 0) == SQLITE_OK
				&& BindText(run, 9, _startedAt)
				&& sqlite3_bind_int64(
					run, 10,
					static_cast<sqlite3_int64>(GetCurrentProcessId())) == SQLITE_OK
				&& BindText(run, 11, _identity.runtimeFamily)
				&& BindOptionalText(run, 12, _identity.runtimeVersion)
				&& BindText(run, 13, _identity.pluginVersion)
				&& BindText(run, 14, _identity.pluginBuildDescribe)
				&& BindText(run, 15, _identity.pluginGitIdentity)
				&& sqlite3_bind_int(
					run, 16, _policy.rawExportRequested ? 1 : 0) == SQLITE_OK
				&& BindText(run, 17, _artifactRootFingerprint)
				&& sqlite3_bind_int(
					run, 18,
					_orderlyFinalizerReady.load(std::memory_order_acquire)
						? 1
						: 0) == SQLITE_OK
				&& sqlite3_step(run) == SQLITE_DONE
				&& BindText(quality, 1, _generatedRunId)
				&& sqlite3_step(quality) == SQLITE_DONE;
		}
		const std::string configSnapshot =
			"{\"writer_flush_interval_ms\":"
			+ std::to_string(_config.flushIntervalMs) + "}";
		if (success) {
			success =
				BindText(legacy, 1, _generatedRunId)
				&& BindText(legacy, 2, _startedAt)
				&& BindText(
					legacy, 3, LegacyRuntime(_identity.runtimeFamily))
				&& BindOptionalText(legacy, 4, _identity.runtimeVersion)
				&& BindText(legacy, 5, _identity.pluginVersion)
				&& BindText(legacy, 6, _identity.pluginGitIdentity)
				&& BindText(legacy, 7, configSnapshot)
				&& sqlite3_step(legacy) == SQLITE_DONE;
		}
		if (run)
			sqlite3_finalize(run);
		if (quality)
			sqlite3_finalize(quality);
		if (legacy)
			sqlite3_finalize(legacy);
		if (success && Exec(_db, "COMMIT", "run insert commit"))
			return true;
		Exec(_db, "ROLLBACK", "run insert rollback");
		return false;
	}

	bool CatalogDB::PrepareStatements()
	{
		constexpr const char* contentSql =
			"INSERT INTO catalog_content_identities(sha256,sha1,size_bytes) "
			"VALUES(?1,?2,?3) ON CONFLICT(sha256) DO UPDATE SET "
			"sha1=excluded.sha1,size_bytes=excluded.size_bytes "
			"WHERE catalog_content_identities.sha1=excluded.sha1 "
			"AND catalog_content_identities.size_bytes=excluded.size_bytes";
		constexpr const char* observationSql =
			"INSERT INTO catalog_run_observations("
			"generated_run_id,observation_key,stage,content_sha256,bytecode_state,submitted_size,"
			"stream_output_digest,stream_output_declaration_state,stream_output_declaration_count,"
			"stream_output_strides_state,stream_output_stride_count,stream_output_rasterized_stream,"
			"stream_output_metadata_truncated,attempts,successes,failures,output_requests,null_outputs,"
			"first_sequence,last_sequence,first_qpc,last_qpc,first_thread_id,first_module,first_stack,"
			"resolver_invocations,resolver_reported_replacements,final_stock,final_replacement,final_null,"
			"replacement_sha256,raw_output_nonnull,stream_output_state"
			") VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,"
			"1,?14,?15,?16,?17,?18,?19,?20,?21,?22,?23,?24,?25,?26,?27,?28,?29,?30,?31,?32) "
			"ON CONFLICT(generated_run_id,observation_key) DO UPDATE SET "
			"attempts=attempts+1,successes=successes+excluded.successes,"
			"failures=failures+excluded.failures,"
			"output_requests=output_requests+excluded.output_requests,"
			"null_outputs=null_outputs+excluded.null_outputs,"
			"raw_output_nonnull=raw_output_nonnull+excluded.raw_output_nonnull,"
			"first_thread_id=CASE WHEN excluded.first_sequence < first_sequence "
			"THEN excluded.first_thread_id ELSE first_thread_id END,"
			"first_module=CASE WHEN excluded.first_sequence < first_sequence "
			"THEN excluded.first_module ELSE first_module END,"
			"first_stack=CASE WHEN excluded.first_sequence < first_sequence "
			"THEN excluded.first_stack ELSE first_stack END,"
			"first_sequence=MIN(first_sequence,excluded.first_sequence),"
			"last_sequence=MAX(last_sequence,excluded.last_sequence),"
			"first_qpc=MIN(first_qpc,excluded.first_qpc),"
			"last_qpc=MAX(last_qpc,excluded.last_qpc),"
			"resolver_invocations=resolver_invocations+excluded.resolver_invocations,"
			"resolver_reported_replacements=resolver_reported_replacements+excluded.resolver_reported_replacements,"
			"final_stock=final_stock+excluded.final_stock,"
			"final_replacement=final_replacement+excluded.final_replacement,"
			"final_null=final_null+excluded.final_null";
		constexpr const char* hresultSql =
			"INSERT INTO catalog_run_hresult_details("
			"generated_run_id,observation_key,hresult,occurrence_count"
			") SELECT ?1,?2,?3,1 WHERE "
			"EXISTS(SELECT 1 FROM catalog_run_hresult_details "
			"WHERE generated_run_id=?4 AND observation_key=?5 AND hresult=?6) "
			"OR (SELECT COUNT(*) FROM catalog_run_hresult_details "
			"WHERE generated_run_id=?7 AND observation_key=?8) < ?9 "
			"ON CONFLICT(generated_run_id,observation_key,hresult) DO UPDATE SET "
			"occurrence_count=occurrence_count+1";
		constexpr const char* attributionSql =
			"INSERT INTO catalog_run_attributions("
			"generated_run_id,sha1,subclass_name,technique_bits,"
			"attribution_kind,object_kind,occurrence_count,"
			"first_qpc,last_qpc,first_thread_id"
			") VALUES(?1,?2,?3,?4,?5,?6,1,?7,?8,?9) "
			"ON CONFLICT(generated_run_id,sha1,subclass_name,technique_bits,"
			"attribution_kind,object_kind) DO UPDATE SET "
			"occurrence_count=occurrence_count+1,"
			"first_qpc=MIN(first_qpc,excluded.first_qpc),"
			"last_qpc=MAX(last_qpc,excluded.last_qpc)";
		constexpr const char* legacyShaderSql =
			"INSERT INTO shader_catalog("
			"sha1,size_bytes,stage,first_seen_timestamp,first_seen_session_id,"
			"last_seen_timestamp,seen_count,source_pointer_va,source_module,"
			"creation_stack_top4,creation_thread_id,engine_runtime,"
			"bsshader_subclass,bsshader_technique_bits"
			") VALUES(?1,?2,?3,?4,?5,?6,1,?7,?8,?9,?10,?11,?12,?13) "
			"ON CONFLICT(sha1) DO UPDATE SET "
			"last_seen_timestamp=excluded.last_seen_timestamp,seen_count=seen_count+1,"
			"size_bytes=CASE WHEN shader_catalog.size_bytes=0 THEN excluded.size_bytes ELSE shader_catalog.size_bytes END,"
			"source_pointer_va=COALESCE(shader_catalog.source_pointer_va,excluded.source_pointer_va),"
			"source_module=COALESCE(shader_catalog.source_module,excluded.source_module),"
			"creation_stack_top4=COALESCE(shader_catalog.creation_stack_top4,excluded.creation_stack_top4),"
			"creation_thread_id=COALESCE(shader_catalog.creation_thread_id,excluded.creation_thread_id)";
		constexpr const char* legacyAttributionSql =
			"INSERT INTO shader_catalog("
			"sha1,size_bytes,stage,first_seen_timestamp,first_seen_session_id,"
			"last_seen_timestamp,seen_count,creation_thread_id,engine_runtime,"
			"bsshader_subclass,bsshader_technique_bits"
			") VALUES(?1,0,'ps',?2,?3,?4,0,?5,?6,?7,?8) "
			"ON CONFLICT(sha1) DO UPDATE SET "
			"bsshader_subclass=COALESCE(shader_catalog.bsshader_subclass,excluded.bsshader_subclass),"
			"bsshader_technique_bits=COALESCE(shader_catalog.bsshader_technique_bits,excluded.bsshader_technique_bits)";
		constexpr const char* shapeSql =
			"UPDATE catalog_content_identities SET "
			"profile=?1,cb_count=?2,srv_count=?3,uav_count=?4,sampler_count=?5,"
			"output_count=?6,input_count=?7,input_has_position_only=?8,"
			"instruction_count=?9,sample_call_count=?10,input_signature_summary=?11,"
			"output_signature_summary=?12,resource_summary=?13 WHERE sha256=?14";
		constexpr const char* legacyShapeSql =
			"UPDATE shader_catalog SET "
			"profile=?1,cb_count=?2,srv_count=?3,uav_count=?4,sampler_count=?5,"
			"output_count=?6,input_count=?7,input_has_position_only=?8,"
			"instruction_count=?9,sample_call_count=?10,input_signature_summary=?11,"
			"output_signature_summary=?12,resource_summary=?13 WHERE sha1=?14";
		constexpr const char* qualitySql =
			"UPDATE catalog_run_quality SET "
			"queue_overflow=?1,malformed_bytecode=?2,unsupported_size=?3,"
			"allocation_failure=?4,copy_failure=?5,hash_failure=?6,"
			"metadata_truncated=?7,db_write_failure=?8,raw_export_failure=?9,"
			"manifest_failure=?10,hook_observer_gap=?11,writer_drain_failure=?12,"
			"lifecycle_failure=?13,configuration_failure=?14 "
			"WHERE generated_run_id=?15";

		return Prepare(_db, contentSql, &_upsertContent, "prepare content upsert")
			&& Prepare(_db, observationSql, &_upsertObservation, "prepare observation upsert")
			&& Prepare(_db, hresultSql, &_upsertHresult, "prepare HRESULT upsert")
			&& Prepare(
				_db,
				"UPDATE catalog_run_observations SET "
				"other_hresult_count=other_hresult_count+1,"
				"hresult_details_truncated=1 "
				"WHERE generated_run_id=?1 AND observation_key=?2",
				&_upsertHresultOverflow,
				"prepare HRESULT overflow update")
			&& Prepare(_db, attributionSql, &_upsertAttribution, "prepare attribution upsert")
			&& Prepare(
				_db,
				"INSERT INTO catalog_run_blobs("
				"generated_run_id,sha256,relative_path) VALUES(?1,?2,?3) "
				"ON CONFLICT(generated_run_id,sha256) DO UPDATE SET "
				"relative_path=excluded.relative_path",
				&_upsertRunBlob, "prepare run blob association upsert")
			&& Prepare(_db, shapeSql, &_updateContentShape, "prepare content shape update")
			&& Prepare(_db, legacyShaderSql, &_insertLegacyShader, "prepare legacy shader upsert")
			&& Prepare(
				_db, legacyAttributionSql, &_upsertLegacyAttribution,
				"prepare legacy attribution upsert")
			&& Prepare(_db, legacyShapeSql, &_updateLegacyShape, "prepare legacy shape update")
			&& Prepare(
				_db,
				"UPDATE sessions SET ended_at=?1,"
				"shaders_added_this_session=("
				"SELECT COUNT(*) FROM shader_catalog WHERE first_seen_session_id=?2 AND seen_count>0"
				") WHERE session_id=?3",
				&_updateLegacySession, "prepare legacy session finalization")
			&& Prepare(_db, qualitySql, &_updateQuality, "prepare quality update");
	}

	bool CatalogDB::FinalizeStatements()
	{
		bool success = true;
		sqlite3_stmt** statements[] = {
			&_upsertContent,
			&_upsertObservation,
			&_upsertHresult,
			&_upsertHresultOverflow,
			&_upsertAttribution,
			&_upsertRunBlob,
			&_updateContentShape,
			&_insertLegacyShader,
			&_upsertLegacyAttribution,
			&_updateLegacyShape,
			&_updateLegacySession,
			&_updateQuality
		};
		for (auto** statement : statements) {
			if (*statement) {
				success =
					sqlite3_finalize(*statement) == SQLITE_OK
					&& success;
				*statement = nullptr;
			}
		}
		return success;
	}

	bool CatalogDB::FinalizeLegacySession(const std::string& a_endedAt)
	{
		return Reset(_updateLegacySession)
			&& BindText(_updateLegacySession, 1, a_endedAt)
			&& BindText(_updateLegacySession, 2, _generatedRunId)
			&& BindText(_updateLegacySession, 3, _generatedRunId)
			&& sqlite3_step(_updateLegacySession) == SQLITE_DONE;
	}

	bool CatalogDB::PersistFinalRunState(
		const std::string& a_endedAt,
		bool a_rawExportComplete,
		bool a_authoritative,
		std::string_view a_manifestSha256,
		std::size_t a_manifestSize)
	{
		sqlite3_stmt* statement = nullptr;
		constexpr const char* sql =
			"UPDATE catalog_runs SET lifecycle='running',ended_at=?1,"
			"authoritative=0,writer_drained=?2,raw_export_complete=?3,"
			"manifest_published=0,manifest_relative_path=?4,"
			"manifest_sha256=?5,manifest_size=?6,"
			"publication_pending=1,pending_authoritative=?7,"
			"hook_coverage_ready=?8,orderly_finalizer_ready=?9,"
			"graphics_adapter=?10,graphics_feature_level=?11 "
			"WHERE generated_run_id=?12 AND lifecycle='running'";
		if (!Prepare(_db, sql, &statement, "prepare run finalization"))
			return false;
		std::optional<std::string> adapter;
		std::optional<std::string> featureLevel;
		{
			std::scoped_lock lock(_identityMutex);
			adapter = _graphicsAdapter;
			featureLevel = _graphicsFeatureLevel;
		}
		const std::string manifestPath =
			"runs/" + _generatedRunId + "/manifest.v1.json";
		const bool success =
			BindText(statement, 1, a_endedAt)
			&& sqlite3_bind_int(
				statement, 2,
				_writerDrained.load(std::memory_order_relaxed) ? 1 : 0) == SQLITE_OK
			&& sqlite3_bind_int(
				statement, 3, a_rawExportComplete ? 1 : 0) == SQLITE_OK
			&& BindText(statement, 4, manifestPath)
			&& BindText(statement, 5, a_manifestSha256)
			&& BindUInt64(statement, 6, a_manifestSize)
			&& sqlite3_bind_int(
				statement, 7, a_authoritative ? 1 : 0) == SQLITE_OK
			&& sqlite3_bind_int(
				statement, 8,
				_hookCoverageReady.load(std::memory_order_acquire)
					? 1
					: 0) == SQLITE_OK
			&& sqlite3_bind_int(
				statement, 9,
				_orderlyFinalizerReady.load(std::memory_order_acquire)
					? 1
					: 0) == SQLITE_OK
			&& BindOptionalText(statement, 10, adapter)
			&& BindOptionalText(statement, 11, featureLevel)
			&& BindText(statement, 12, _generatedRunId)
			&& sqlite3_step(statement) == SQLITE_DONE
			&& sqlite3_changes(_db) == 1;
		sqlite3_finalize(statement);
		return success;
	}

	bool CatalogDB::RepairFailedPublication(
		const std::string& a_endedAt)
	{
		_qualityLifecycleFailure.fetch_add(1, std::memory_order_relaxed);
		if (!_db) {
			if (sqlite3_open_v2(
					_config.catalogPath.c_str(), &_db,
					SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
					nullptr) != SQLITE_OK)
				return false;
			sqlite3_extended_result_codes(_db, 1);
			if (!Exec(_db, "PRAGMA foreign_keys=ON", "repair foreign keys")
				|| !Exec(
					_db, "PRAGMA busy_timeout=2000",
					"repair busy timeout")
				|| !PrepareStatements())
				return false;
		}
		if (!Exec(_db, "BEGIN IMMEDIATE", "publication repair begin"))
			return false;
		const bool legacyFinalized = FinalizeLegacySession(a_endedAt);
		if (!legacyFinalized)
			_qualityDbWriteFailure.fetch_add(1, std::memory_order_relaxed);
		sqlite3_stmt* run = nullptr;
		const bool success =
			PersistQuality()
			&& Prepare(
				_db,
				"UPDATE catalog_runs SET lifecycle='abandoned',"
				"ended_at=?1,authoritative=0,manifest_published=0,"
				"manifest_relative_path=NULL,manifest_sha256=NULL,"
				"manifest_size=NULL,publication_pending=0,"
				"pending_authoritative=0 "
				"WHERE generated_run_id=?2",
				&run, "prepare publication repair")
			&& BindText(run, 1, a_endedAt)
			&& BindText(run, 2, _generatedRunId)
			&& sqlite3_step(run) == SQLITE_DONE
			&& sqlite3_changes(_db) == 1;
		if (run)
			sqlite3_finalize(run);
		if (success
			&& Exec(_db, "COMMIT", "publication repair commit")
			&& Checkpoint(
				SQLITE_CHECKPOINT_PASSIVE,
				"publication repair PASSIVE checkpoint"))
			return true;
		Exec(_db, "ROLLBACK", "publication repair rollback");
		return false;
	}

	bool CatalogDB::Checkpoint(int a_mode, const char* a_name)
	{
		int logFrames = 0;
		int checkpointedFrames = 0;
		const int result = sqlite3_wal_checkpoint_v2(
			_db, nullptr, a_mode, &logFrames, &checkpointedFrames);
		if (result == SQLITE_OK)
			return true;
		if (auto logger = Logger(); logger) {
			logger->warn(
				"{} failed: {} (log={}, checkpointed={})",
				a_name, sqlite3_errstr(result), logFrames, checkpointedFrames);
		}
		return false;
	}

	bool CatalogDB::LoadManifestDocument(
		const std::string&,
		ManifestDocument& a_document)
	{
		a_document.producerVersion = _identity.pluginVersion;
		a_document.producerBuildDescribe = _identity.pluginBuildDescribe;
		a_document.producerGitIdentity = _identity.pluginGitIdentity;
		a_document.generatedRunId = _generatedRunId;
		a_document.externalRunId = _policy.externalRunId;
		a_document.scenarioId = _policy.scenarioId;
		a_document.configId = _policy.configId;
		a_document.sourceId = _policy.sourceId;
		a_document.runtimeFamily = _identity.runtimeFamily;
		a_document.runtimeVersion = _identity.runtimeVersion;
		a_document.processId = GetCurrentProcessId();
		{
			std::scoped_lock lock(_identityMutex);
			a_document.graphicsAdapter = _graphicsAdapter;
			a_document.graphicsFeatureLevel = _graphicsFeatureLevel;
		}
		a_document.evidenceMode = _policy.evidenceMode;
		a_document.evidenceIdsSatisfied = _policy.evidenceIdsSatisfied;
		a_document.rawExportRequested = _policy.rawExportRequested;
		a_document.hookCoverageReady =
			_hookCoverageReady.load(std::memory_order_acquire);
		a_document.startedAt = _startedAt;
		a_document.quality = QualitySnapshot();
		a_document.counters.attempts = _statAttempts.load(std::memory_order_relaxed);
		a_document.counters.successes = _statSuccesses.load(std::memory_order_relaxed);
		a_document.counters.failures = _statFailures.load(std::memory_order_relaxed);
		a_document.counters.uniqueObservations =
			_statUniqueObservations.load(std::memory_order_relaxed);
		a_document.counters.uniqueContents =
			_statUniqueContents.load(std::memory_order_relaxed);
		a_document.counters.attributionEvents =
			_statAttributionEvents.load(std::memory_order_relaxed);

		sqlite3_stmt* blobs = nullptr;
		constexpr const char* blobsSql =
			"SELECT c.sha256,c.sha1,c.size_bytes,b.relative_path,"
			"c.profile,c.cb_count,c.srv_count,c.uav_count,c.sampler_count,"
			"c.output_count,c.input_count,c.input_has_position_only,"
			"c.instruction_count,c.sample_call_count,c.input_signature_summary,"
			"c.output_signature_summary,c.resource_summary "
			"FROM catalog_content_identities c "
			"LEFT JOIN catalog_run_blobs b "
			"ON b.generated_run_id=?1 AND b.sha256=c.sha256 "
			"WHERE EXISTS("
			"SELECT 1 FROM catalog_run_observations o "
			"WHERE o.generated_run_id=?1 AND o.content_sha256=c.sha256"
			") ORDER BY c.sha256";
		if (!Prepare(_db, blobsSql, &blobs, "prepare manifest blobs")
			|| !BindText(blobs, 1, _generatedRunId)) {
			if (blobs)
				sqlite3_finalize(blobs);
			return false;
		}
		int blobStep = SQLITE_OK;
		while ((blobStep = sqlite3_step(blobs)) == SQLITE_ROW) {
			ManifestBlob blob;
			blob.sha256 = ColumnOptionalText(blobs, 0).value_or("");
			blob.sha1 = ColumnOptionalText(blobs, 1).value_or("");
			blob.sizeBytes = static_cast<std::uint64_t>(
				sqlite3_column_int64(blobs, 2));
			blob.relativePath = ColumnOptionalText(blobs, 3);
			if (!IsLowerHexDigest(blob.sha256, 64)
				|| !IsLowerHexDigest(blob.sha1, 40)
				|| (blob.relativePath
					&& *blob.relativePath
						!= BlobRelativePath(blob.sha256))) {
				sqlite3_finalize(blobs);
				return false;
			}
			auto optionalInt = [&](int a_index) -> std::optional<int> {
				if (sqlite3_column_type(blobs, a_index) == SQLITE_NULL)
					return std::nullopt;
				return sqlite3_column_int(blobs, a_index);
			};
			blob.shape.profile = ColumnOptionalText(blobs, 4);
			blob.shape.cbCount = optionalInt(5);
			blob.shape.srvCount = optionalInt(6);
			blob.shape.uavCount = optionalInt(7);
			blob.shape.samplerCount = optionalInt(8);
			blob.shape.outputCount = optionalInt(9);
			blob.shape.inputCount = optionalInt(10);
			blob.shape.inputHasPositionOnly = optionalInt(11);
			blob.shape.instructionCount = optionalInt(12);
			blob.shape.sampleCallCount = optionalInt(13);
			blob.shape.inputSignatureSummary = ColumnOptionalText(blobs, 14);
			blob.shape.outputSignatureSummary = ColumnOptionalText(blobs, 15);
			blob.shape.resourceSummary = ColumnOptionalText(blobs, 16);
			a_document.blobs.push_back(std::move(blob));
		}
		sqlite3_finalize(blobs);
		if (blobStep != SQLITE_DONE)
			return false;

		sqlite3_stmt* observations = nullptr;
		constexpr const char* observationsSql =
			"SELECT o.observation_key,o.stage,o.content_sha256,c.sha1,"
			"o.bytecode_state,o.submitted_size,o.stream_output_digest,"
			"o.stream_output_declaration_state,o.stream_output_declaration_count,"
			"o.stream_output_strides_state,o.stream_output_stride_count,"
			"o.stream_output_rasterized_stream,o.stream_output_metadata_truncated,"
			"o.attempts,o.successes,o.failures,o.output_requests,o.null_outputs,"
			"o.raw_output_nonnull,"
			"o.resolver_invocations,o.resolver_reported_replacements,"
			"o.final_stock,o.final_replacement,o.final_null,o.replacement_sha256,"
			"o.first_sequence,o.last_sequence,o.first_qpc,o.last_qpc,"
			"o.first_thread_id,o.first_module,o.first_stack,"
			"o.other_hresult_count,o.hresult_details_truncated,"
			"o.stream_output_state "
			"FROM catalog_run_observations o "
			"LEFT JOIN catalog_content_identities c ON c.sha256=o.content_sha256 "
			"WHERE o.generated_run_id=?1 ORDER BY o.observation_key";
		if (!Prepare(
				_db, observationsSql, &observations,
				"prepare manifest observations")
			|| !BindText(observations, 1, _generatedRunId)) {
			if (observations)
				sqlite3_finalize(observations);
			return false;
		}
		int observationStep = SQLITE_OK;
		while ((observationStep = sqlite3_step(observations)) == SQLITE_ROW) {
			ManifestObservation observation;
			observation.key = ColumnOptionalText(observations, 0).value_or("");
			observation.stage = ColumnOptionalText(observations, 1).value_or("");
			observation.sha256 = ColumnOptionalText(observations, 2);
			observation.sha1 = ColumnOptionalText(observations, 3);
			observation.bytecodeState =
				ColumnOptionalText(observations, 4).value_or("unknown");
			observation.submittedSize = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 5));
			observation.streamOutput.present =
				sqlite3_column_type(observations, 7) != SQLITE_NULL;
			if (observation.streamOutput.present) {
				observation.streamOutput.digestSha256 =
					ColumnOptionalText(observations, 6).value_or("");
				observation.streamOutput.declarationState =
					ColumnOptionalText(observations, 7).value_or("unknown");
				observation.streamOutput.declarationCount =
					static_cast<std::uint32_t>(
						sqlite3_column_int64(observations, 8));
				observation.streamOutput.stridesState =
					ColumnOptionalText(observations, 9).value_or("unknown");
				observation.streamOutput.strideCount =
					static_cast<std::uint32_t>(
						sqlite3_column_int64(observations, 10));
				observation.streamOutput.rasterizedStream =
					static_cast<std::uint32_t>(
						sqlite3_column_int64(observations, 11));
				observation.streamOutput.metadataTruncated =
					sqlite3_column_int(observations, 12) != 0;
				observation.streamOutput.valid =
					!observation.streamOutput.digestSha256.empty();
				const auto state =
					ColumnOptionalText(observations, 34)
						.value_or("not_applicable");
				if (state == "exact")
					observation.streamOutput.state = StreamOutputState::kExact;
				else if (state == "unsupported_size")
					observation.streamOutput.state =
						StreamOutputState::kUnsupportedSize;
				else if (state == "allocation_failure")
					observation.streamOutput.state =
						StreamOutputState::kAllocationFailure;
				else if (state == "copy_failure")
					observation.streamOutput.state =
						StreamOutputState::kCopyFailure;
				else if (state == "hash_failure")
					observation.streamOutput.state =
						StreamOutputState::kHashFailure;
				else if (state == "metadata_truncated")
					observation.streamOutput.state =
						StreamOutputState::kMetadataTruncated;
			}
			observation.attempts = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 13));
			observation.successes = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 14));
			observation.failures = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 15));
			observation.outputRequests = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 16));
			observation.nullOutputs = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 17));
			observation.rawOutputNonNull = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 18));
			observation.resolverInvocations = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 19));
			observation.resolverReportedReplacements = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 20));
			observation.finalStock = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 21));
			observation.finalReplacement = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 22));
			observation.finalNull = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 23));
			observation.replacementSha256 = ColumnOptionalText(observations, 24);
			observation.firstSequence = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 25));
			observation.lastSequence = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 26));
			observation.firstQpc = sqlite3_column_int64(observations, 27);
			observation.lastQpc = sqlite3_column_int64(observations, 28);
			observation.firstThreadId = static_cast<std::uint32_t>(
				sqlite3_column_int64(observations, 29));
			observation.firstModule = ColumnOptionalText(observations, 30);
			observation.firstStack = ColumnOptionalText(observations, 31);
			observation.otherHresultCount = static_cast<std::uint64_t>(
				sqlite3_column_int64(observations, 32));
			observation.hresultDetailsTruncated =
				sqlite3_column_int(observations, 33) != 0;
			observation.streamOutput.copyFailure =
				observation.streamOutput.declarationState == "copy_failure"
				|| observation.streamOutput.stridesState == "copy_failure";

			sqlite3_stmt* hresults = nullptr;
			if (!Prepare(
					_db,
					"SELECT hresult,occurrence_count FROM catalog_run_hresult_details "
					"WHERE generated_run_id=?1 AND observation_key=?2 ORDER BY hresult",
					&hresults, "prepare manifest HRESULT details")
				|| !BindText(hresults, 1, _generatedRunId)
				|| !BindText(hresults, 2, observation.key)) {
				if (hresults)
					sqlite3_finalize(hresults);
				sqlite3_finalize(observations);
				return false;
			}
			int hresultStep = SQLITE_OK;
			while ((hresultStep = sqlite3_step(hresults)) == SQLITE_ROW) {
				observation.failedHresults.push_back({
					sqlite3_column_int(hresults, 0),
					static_cast<std::uint64_t>(
						sqlite3_column_int64(hresults, 1))
				});
			}
			sqlite3_finalize(hresults);
			if (hresultStep != SQLITE_DONE) {
				sqlite3_finalize(observations);
				return false;
			}
			a_document.observations.push_back(std::move(observation));
		}
		sqlite3_finalize(observations);
		if (observationStep != SQLITE_DONE)
			return false;

		sqlite3_stmt* attributions = nullptr;
		constexpr const char* attributionSql =
			"SELECT a.sha1,"
			"(SELECT CASE WHEN COUNT(*)=1 THEN MIN(c.sha256) ELSE NULL END "
			" FROM catalog_content_identities c WHERE c.sha1=a.sha1),"
			"a.subclass_name,a.technique_bits,a.occurrence_count,"
			"a.attribution_kind,a.object_kind "
			"FROM catalog_run_attributions a WHERE a.generated_run_id=?1 "
			"ORDER BY a.sha1,a.subclass_name,a.technique_bits";
		if (!Prepare(
				_db, attributionSql, &attributions,
				"prepare manifest attributions")
			|| !BindText(attributions, 1, _generatedRunId)) {
			if (attributions)
				sqlite3_finalize(attributions);
			return false;
		}
		int attributionStep = SQLITE_OK;
		while ((attributionStep = sqlite3_step(attributions)) == SQLITE_ROW) {
			ManifestAttribution attribution;
			const auto stockSha1 = ColumnOptionalText(attributions, 0);
			attribution.subclassName =
				ColumnOptionalText(attributions, 2).value_or("");
			const auto technique = sqlite3_column_int64(attributions, 3);
			if (technique >= 0)
				attribution.techniqueBits =
					static_cast<std::uint32_t>(technique);
			attribution.count = static_cast<std::uint64_t>(
				sqlite3_column_int64(attributions, 4));
			attribution.attributionKind =
				ColumnOptionalText(attributions, 5).value_or("");
			attribution.objectKind =
				ColumnOptionalText(attributions, 6).value_or("");
			if (attribution.objectKind == "replacement_unknown") {
				attribution.originatingStockSha1 = stockSha1;
			} else {
				attribution.sha1 = stockSha1;
				attribution.sha256 = ColumnOptionalText(attributions, 1);
			}
			if (!stockSha1
				|| !IsLowerHexDigest(*stockSha1, 40)
				|| (attribution.sha256
					&& !IsLowerHexDigest(*attribution.sha256, 64))
				|| (attribution.attributionKind != "creation_context"
					&& attribution.attributionKind != "observed_binding"
					&& attribution.attributionKind
						!= "technique_map_association")
				|| (attribution.objectKind != "stock"
					&& attribution.objectKind != "replacement_unknown"
					&& attribution.objectKind != "originating_stock"
					&& attribution.objectKind != "submission_no_object")) {
				sqlite3_finalize(attributions);
				return false;
			}
			a_document.attributions.push_back(std::move(attribution));
		}
		sqlite3_finalize(attributions);
		return attributionStep == SQLITE_DONE;
	}

	bool CatalogDB::InspectSchemaVersion(
		const std::filesystem::path& a_path,
		int& a_version,
		std::string& a_error)
	{
		sqlite3* database = nullptr;
		const auto utf8Path = a_path.string();
		if (sqlite3_open_v2(
				utf8Path.c_str(), &database,
				SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
				nullptr) != SQLITE_OK) {
			a_error = database ? sqlite3_errmsg(database) : "unable to open database";
			if (database)
				sqlite3_close(database);
			return false;
		}
		const bool success = ReadSchemaVersion(database, a_version, a_error);
		sqlite3_close(database);
		return success;
	}
}
