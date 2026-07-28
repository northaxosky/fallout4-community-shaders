#include "AttributionPolicy.h"
#include "CatalogDB.h"
#include "HookCoverage.h"
#include "OrderlyExit.h"
#include "PixelShaderTracker.h"
#include "Provenance.h"
#include "Render/PixelShaderSwapBroker.h"
#include "RouteCaptureCoordinator.h"

#include <Windows.h>

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
	using namespace cs::features::catalog;

	struct Failure : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	class FakePixelShader final : public ID3D11PixelShader
	{
	public:
		HRESULT STDMETHODCALLTYPE QueryInterface(
			REFIID, void**) override
		{
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++references;
		}
		ULONG STDMETHODCALLTYPE Release() override
		{
			return --references;
		}
		void STDMETHODCALLTYPE GetDevice(ID3D11Device** a_device) override
		{
			if (a_device)
				*a_device = nullptr;
		}
		HRESULT STDMETHODCALLTYPE GetPrivateData(
			REFGUID, UINT*, void*) override
		{
			return E_NOTIMPL;
		}
		HRESULT STDMETHODCALLTYPE SetPrivateData(
			REFGUID, UINT, const void*) override
		{
			return E_NOTIMPL;
		}
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
			REFGUID, const IUnknown*) override
		{
			return E_NOTIMPL;
		}

		std::atomic<ULONG> references{ 1 };
	};

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw Failure(std::string(a_message));
	}

	bool BeginTestPixelAdmission() noexcept
	{
		return CatalogDB::Get().TryBeginProducerAdmission();
	}

	void EndTestPixelAdmission() noexcept
	{
		CatalogDB::Get().EndProducerAdmission();
	}

	void* FailTestPixelTokenAllocation(
		const void*, std::size_t) noexcept
	{
		CatalogDB::Get().RecordAllocationFailure();
		return nullptr;
	}

	void CompleteTestPixelObserver(
		void*, const cs::engine::PixelShaderSwapCompletion&) noexcept
	{}

	std::filesystem::path TempDirectory(std::string_view a_name)
	{
		const auto uuid = GenerateUuidV4();
		Check(uuid.has_value(), "UUID generation failed");
		const auto path = std::filesystem::temp_directory_path()
			/ ("fo4cs-catalog-" + std::string(a_name) + "-" + *uuid);
		std::filesystem::create_directories(path);
		return path;
	}

	struct TempTree
	{
		explicit TempTree(std::string_view a_name) :
			path(TempDirectory(a_name))
		{}
		~TempTree()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
		std::filesystem::path path;
	};

	std::filesystem::path g_directoryRaceTarget;
	std::atomic<unsigned> g_directoryRaceWins{ 0 };
	std::atomic<bool> g_directoryRaceFailed{ false };

	void WinDirectoryCreateRace(
		const std::filesystem::path& a_path) noexcept
	{
		try {
			if (a_path != g_directoryRaceTarget)
				return;
			std::error_code error;
			if (std::filesystem::create_directory(a_path, error)
				&& !error) {
				g_directoryRaceWins.fetch_add(
					1, std::memory_order_relaxed);
			} else {
				g_directoryRaceFailed.store(
					true, std::memory_order_relaxed);
			}
		} catch (...) {
			g_directoryRaceFailed.store(
				true, std::memory_order_relaxed);
		}
	}

	struct DirectoryCreateRace
	{
		explicit DirectoryCreateRace(
			std::filesystem::path a_target)
		{
			g_directoryRaceTarget = std::move(a_target);
			g_directoryRaceWins.store(0, std::memory_order_relaxed);
			g_directoryRaceFailed.store(false, std::memory_order_relaxed);
			SetBeforeDirectoryCreateForTesting(
				&WinDirectoryCreateRace);
		}

		~DirectoryCreateRace()
		{
			SetBeforeDirectoryCreateForTesting(nullptr);
		}
	};

	void SqlExec(const std::filesystem::path& a_path, const char* a_sql)
	{
		sqlite3* database = nullptr;
		Check(
			sqlite3_open(a_path.string().c_str(), &database) == SQLITE_OK,
			"SQLite open failed");
		char* error = nullptr;
		const int result = sqlite3_exec(database, a_sql, nullptr, nullptr, &error);
		const std::string message = error ? error : "";
		if (error)
			sqlite3_free(error);
		sqlite3_close(database);
		Check(result == SQLITE_OK, "SQLite exec failed: " + message);
	}

	bool SqlExecFails(
		const std::filesystem::path& a_path,
		const char* a_sql)
	{
		sqlite3* database = nullptr;
		Check(
			sqlite3_open(a_path.string().c_str(), &database) == SQLITE_OK,
			"SQLite open failed");
		const int result =
			sqlite3_exec(database, a_sql, nullptr, nullptr, nullptr);
		sqlite3_close(database);
		return result != SQLITE_OK;
	}

	std::int64_t SqlInt(
		const std::filesystem::path& a_path,
		const char* a_sql)
	{
		sqlite3* database = nullptr;
		Check(
			sqlite3_open_v2(
				a_path.string().c_str(), &database,
				SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK,
			"SQLite readonly open failed");
		sqlite3_stmt* statement = nullptr;
		Check(
			sqlite3_prepare_v2(database, a_sql, -1, &statement, nullptr)
				== SQLITE_OK,
			"SQLite prepare failed");
		Check(sqlite3_step(statement) == SQLITE_ROW, "SQLite query returned no row");
		const auto value = sqlite3_column_int64(statement, 0);
		sqlite3_finalize(statement);
		sqlite3_close(database);
		return value;
	}

	std::string SqlText(
		const std::filesystem::path& a_path,
		const char* a_sql)
	{
		sqlite3* database = nullptr;
		Check(
			sqlite3_open_v2(
				a_path.string().c_str(), &database,
				SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK,
			"SQLite readonly open failed");
		sqlite3_stmt* statement = nullptr;
		Check(
			sqlite3_prepare_v2(database, a_sql, -1, &statement, nullptr)
				== SQLITE_OK,
			"SQLite prepare failed");
		Check(sqlite3_step(statement) == SQLITE_ROW, "SQLite query returned no row");
		const auto* text = reinterpret_cast<const char*>(
			sqlite3_column_text(statement, 0));
		const std::string value = text ? text : "";
		sqlite3_finalize(statement);
		sqlite3_close(database);
		return value;
	}

	DbConfig TestConfig(
		const std::filesystem::path& a_database,
		const std::filesystem::path& a_artifacts)
	{
		DbConfig config;
		config.catalogPath = a_database.string();
		config.flushIntervalMs = 10;
		config.subclassAttributionRequested = true;
		config.subclassAttributionEnabled = false;
		config.policyOverride = ParseRunPolicy({});
		config.artifactRootOverride = a_artifacts;
		config.orderlyFinalizerReadyForTesting = true;
		return config;
	}

	RuntimeIdentity TestIdentity()
	{
		RuntimeIdentity identity;
		identity.runtimeFamily = "unknown";
		identity.pluginVersion = "1.2.3";
		identity.pluginBuildDescribe = "test-build";
		identity.pluginGitIdentity = "0123456789abcdef";
		return identity;
	}

	bool StartReady(CatalogDB& a_catalog, const DbConfig& a_config)
	{
		if (!a_catalog.Start(a_config, TestIdentity()))
			return false;
		a_catalog.MarkHookCoverageReady();
		return true;
	}

	DbConfig RawExportConfig(
		const std::filesystem::path& a_database,
		const std::filesystem::path& a_root)
	{
		auto config = TestConfig(a_database, a_root);
		EnvironmentValues values;
		values.corpusRoot = a_root.string();
		config.policyOverride = ParseRunPolicy(values);
		return config;
	}

	std::array<std::byte, 32> SyntheticDxbc()
	{
		std::array<std::byte, 32> bytes{};
		constexpr char marker[] = "DXBC synthetic host fixture";
		std::copy_n(
			reinterpret_cast<const std::byte*>(marker),
			std::min(bytes.size(), sizeof(marker) - 1),
			bytes.begin());
		return bytes;
	}

	ObservationOutcome MakeOutcome(
		const std::array<std::byte, 32>& a_bytes,
		std::uint64_t a_sequence,
		HRESULT a_result,
		bool a_outputNonNull)
	{
		ObservationOutcome outcome;
		outcome.prepared = PrepareObservation(
			'p', a_bytes.data(), a_bytes.size(), a_sequence);
		outcome.hresult = static_cast<std::int32_t>(a_result);
		outcome.outputRequested = true;
		outcome.outputNonNull = a_outputNonNull;
		outcome.finalIsStock = SUCCEEDED(a_result) && a_outputNonNull;
		outcome.finalIsNull = !a_outputNonNull;
		return outcome;
	}

	// A null submission the producer path must classify as malformed bytecode.
	ObservationOutcome MakeMalformedOutcome(std::uint64_t a_sequence)
	{
		ObservationOutcome outcome;
		outcome.prepared = PrepareObservation('p', nullptr, 0, a_sequence);
		outcome.hresult = static_cast<std::int32_t>(S_OK);
		outcome.outputRequested = true;
		outcome.outputNonNull = true;
		return outcome;
	}

	// A subclass name past the identifier limit, which counts as metadata truncation.
	std::string OversizedSubclassName()
	{
		return std::string(kMaxCatalogIdentifierBytes + 1, 'x');
	}

	static_assert(
		sizeof(QualityCounters) == 14 * sizeof(std::uint64_t),
		"QualityTuple must cover every durable quality counter");

	// Complete counter tuple so a new counter cannot silently escape comparison.
	std::array<std::uint64_t, 14> QualityTuple(
		const QualityCounters& a_quality) noexcept
	{
		return {
			a_quality.queueOverflow,
			a_quality.malformedBytecode,
			a_quality.unsupportedSize,
			a_quality.allocationFailure,
			a_quality.copyFailure,
			a_quality.hashFailure,
			a_quality.metadataTruncated,
			a_quality.dbWriteFailure,
			a_quality.rawExportFailure,
			a_quality.manifestFailure,
			a_quality.hookObserverGap,
			a_quality.writerDrainFailure,
			a_quality.lifecycleFailure,
			a_quality.configurationFailure
		};
	}

	// Every stat a rejected producer entry could move, compared as one tuple.
	std::array<std::uint64_t, 9> StatTuple(
		const CatalogDB::Stats& a_stats) noexcept
	{
		return {
			a_stats.attempts,
			a_stats.successes,
			a_stats.failures,
			a_stats.uniqueObservations,
			a_stats.uniqueContents,
			a_stats.attributionEvents,
			a_stats.reflected,
			a_stats.attributedPs,
			a_stats.totalPs
		};
	}

	// Drives every non-admitted public producer entry point, valid and malformed.
	void SubmitRejectedProducerEntries(
		CatalogDB& a_catalog,
		std::uint64_t a_sequence)
	{
		const auto bytes = SyntheticDxbc();
		ContentDigest digest{};
		Check(
			ComputeDigests(bytes.data(), bytes.size(), digest),
			"rejected-entry digest failed");
		Sha1Result sha{};
		sha.bytes = digest.sha1;
		a_catalog.EnqueueObservation(
			MakeOutcome(bytes, a_sequence, S_OK, true));
		a_catalog.EnqueueObservation(
			MakeMalformedOutcome(a_sequence + 1));
		a_catalog.EnqueueAttribution(
			sha,
			"BSDFLightShader",
			0x01200202,
			AttributionKind::kCreationContext,
			AttributionObjectKind::kStock);
		a_catalog.EnqueueAttribution(
			sha,
			OversizedSubclassName().c_str(),
			0x01200202,
			AttributionKind::kCreationContext,
			AttributionObjectKind::kStock);
	}

	void TestHashes()
	{
		constexpr std::string_view input = "abc";
		ContentDigest digest{};
		Check(ComputeDigests(input.data(), input.size(), digest), "digest failed");
		Check(
			HexLower(digest.sha1.data(), digest.sha1.size())
				== "a9993e364706816aba3e25717850c26c9cd0d89d",
			"SHA-1 vector mismatch");
		Check(
			HexLower(digest.sha256.data(), digest.sha256.size())
				== "ba7816bf8f01cfea414140de5dae2223"
				   "b00361a396177a9cb410ff61f20015ad",
			"SHA-256 vector mismatch");
	}

	void TestRunPolicy()
	{
		EnvironmentValues evidence;
		evidence.evidenceMode = "true";
		auto policy = ParseRunPolicy(evidence);
		Check(policy.evidenceMode, "evidence mode not enabled");
		Check(!policy.evidenceIdsSatisfied, "missing evidence IDs accepted");

		evidence.externalRunId = "run-01";
		evidence.scenarioId = "scene:outdoor";
		policy = ParseRunPolicy(evidence);
		Check(policy.evidenceIdsSatisfied, "valid evidence IDs rejected");

		evidence.evidenceMode = "TRUE";
		policy = ParseRunPolicy(evidence);
		Check(!policy.environmentValid, "non-strict boolean accepted");

		evidence.evidenceMode = "true";
		evidence.externalRunId = "../escape";
		policy = ParseRunPolicy(evidence);
		Check(!policy.environmentValid, "path injection identifier accepted");
		Check(!policy.externalRunId, "invalid run ID was retained");
	}

	void TestStreamOutputIdentity()
	{
		const UINT strides[] = { 16, 32 };
		D3D11_SO_DECLARATION_ENTRY entry{
			1, "POSITION", 2, 1, 3, 0
		};
		const auto baseline = PrepareStreamOutputIdentity(
			&entry, 1, strides, 2, 1);
		Check(baseline.valid, "baseline stream-output identity invalid");
		Check(!baseline.digestSha256.empty(), "stream-output digest missing");

		auto mutation = entry;
		mutation.Stream = 2;
		Check(
			PrepareStreamOutputIdentity(&mutation, 1, strides, 2, 1).digestSha256
				!= baseline.digestSha256,
			"Stream mutation was not encoded");
		mutation = entry;
		mutation.SemanticName = "NORMAL";
		Check(
			PrepareStreamOutputIdentity(&mutation, 1, strides, 2, 1).digestSha256
				!= baseline.digestSha256,
			"SemanticName mutation was not encoded");
		mutation = entry;
		++mutation.SemanticIndex;
		Check(
			PrepareStreamOutputIdentity(&mutation, 1, strides, 2, 1).digestSha256
				!= baseline.digestSha256,
			"SemanticIndex mutation was not encoded");
		mutation = entry;
		++mutation.StartComponent;
		Check(
			PrepareStreamOutputIdentity(&mutation, 1, strides, 2, 1).digestSha256
				!= baseline.digestSha256,
			"StartComponent mutation was not encoded");
		mutation = entry;
		++mutation.ComponentCount;
		Check(
			PrepareStreamOutputIdentity(&mutation, 1, strides, 2, 1).digestSha256
				!= baseline.digestSha256,
			"ComponentCount mutation was not encoded");
		mutation = entry;
		++mutation.OutputSlot;
		Check(
			PrepareStreamOutputIdentity(&mutation, 1, strides, 2, 1).digestSha256
				!= baseline.digestSha256,
			"OutputSlot mutation was not encoded");

		const UINT changedStrides[] = { 16, 36 };
		Check(
			PrepareStreamOutputIdentity(
				&entry, 1, changedStrides, 2, 1).digestSha256
				!= baseline.digestSha256,
			"stride mutation was not encoded");
		Check(
			PrepareStreamOutputIdentity(&entry, 1, strides, 2, 2).digestSha256
				!= baseline.digestSha256,
			"rasterized stream mutation was not encoded");

		mutation = entry;
		mutation.SemanticName = nullptr;
		const auto nullSemantic = PrepareStreamOutputIdentity(
			&mutation, 1, strides, 2, 1);
		mutation.SemanticName = "";
		const auto emptySemantic = PrepareStreamOutputIdentity(
			&mutation, 1, strides, 2, 1);
		Check(
			nullSemantic.digestSha256 != emptySemantic.digestSha256,
			"null and empty semantics were conflated");

		std::string longSemantic(kMaxSemanticBytes + 20, 'x');
		mutation.SemanticName = longSemantic.c_str();
		const auto truncated = PrepareStreamOutputIdentity(
			&mutation, 1, strides, 2, 1);
		Check(truncated.metadataTruncated, "truncated semantic was not identified");

		const auto nullDeclaration = PrepareStreamOutputIdentity(
			nullptr, 0, nullptr, 0, 0);
		const auto emptyDeclaration = PrepareStreamOutputIdentity(
			&entry, 0, strides, 0, 0);
		Check(
			nullDeclaration.digestSha256 != emptyDeclaration.digestSha256,
			"null and empty declarations were conflated");

		const auto huge = PrepareStreamOutputIdentity(
			nullptr, kMaxStreamOutputEntries + 1, nullptr, 0, 0);
		Check(!huge.valid, "huge stream-output metadata accepted");
	}

	ManifestDocument SampleManifest()
	{
		ManifestDocument document;
		document.producerVersion = "1.2.3";
		document.producerBuildDescribe = "v1.2.3-test";
		document.producerGitIdentity = "abcdef";
		document.generatedRunId = "11111111-2222-4333-8444-555555555555";
		document.externalRunId = "external-01";
		document.scenarioId = "scenario-01";
		document.runtimeFamily = "unknown";
		document.processId = 42;
		document.evidenceMode = true;
		document.evidenceIdsSatisfied = true;
		document.writerFlushIntervalMs = 1234;
		document.subclassAttributionRequested = true;
		document.subclassAttributionEnabled = false;
		document.lifecycle = "finalized";
		document.startedAt = "2026-01-01T00:00:00.000Z";
		document.endedAt = "2026-01-01T00:00:01.000Z";
		document.writerDrained = true;
		document.rawExportComplete = true;
		document.manifestPublished = true;
		document.authoritative = true;
		document.counters.attempts = 2;
		document.counters.successes = 1;
		document.counters.failures = 1;
		document.counters.uniqueObservations = 2;
		document.counters.uniqueContents = 2;

		ManifestBlob b;
		b.sha256 = std::string(64, 'b');
		b.sha1 = std::string(40, 'b');
		b.sizeBytes = 16;
		b.relativePath = BlobRelativePath(b.sha256);
		ManifestBlob a = b;
		a.sha256 = std::string(64, 'a');
		a.sha1 = std::string(40, 'a');
		a.relativePath = BlobRelativePath(a.sha256);
		document.blobs = { b, a };

		ManifestObservation second;
		second.key = "p:exact:z";
		second.stage = "ps";
		second.bytecodeState = "exact";
		second.attempts = 1;
		second.failures = 1;
		second.failedHresults.push_back({
			static_cast<std::int32_t>(E_FAIL), 1
		});
		ManifestObservation first = second;
		first.key = "p:exact:a";
		first.successes = 1;
		first.failures = 0;
		first.failedHresults.clear();
		document.observations = { second, first };

		ManifestAttribution z;
		z.sha1 = std::string(40, 'b');
		z.subclassName = "ZShader";
		z.count = 1;
		ManifestAttribution aAttribution = z;
		aAttribution.sha1 = std::string(40, 'a');
		aAttribution.subclassName = "AShader";
		document.attributions = { z, aAttribution };
		return document;
	}

	void TestManifestDeterminism()
	{
		auto first = SampleManifest();
		auto second = first;
		std::reverse(second.blobs.begin(), second.blobs.end());
		std::reverse(second.observations.begin(), second.observations.end());
		std::reverse(second.attributions.begin(), second.attributions.end());
		const auto firstJson = BuildCanonicalManifest(std::move(first));
		const auto secondJson = BuildCanonicalManifest(std::move(second));
		Check(firstJson == secondJson, "manifest depends on insertion order");
		Check(firstJson.ends_with('\n'), "manifest has no final LF");
		Check(
			firstJson.starts_with(
				"{\"schema\":\"fo4cs.shader-catalog-run\",\"schema_version\":2"),
			"manifest key order or schema is wrong");
		Check(
			firstJson.find(
				"\"writer_flush_interval_ms\":1234,"
				"\"subclass_attribution_requested\":true,"
				"\"subclass_attribution_enabled\":false")
				!= std::string::npos,
			"manifest omitted effective catalog settings");
		Check(
			firstJson.find("C:\\\\") == std::string::npos,
			"manifest contains an absolute Windows path");
		Check(
			firstJson.find("source_pointer") == std::string::npos,
			"manifest contains a pointer field");
		Check(
			firstJson.find("DXBC synthetic host fixture") == std::string::npos,
			"manifest contains native shader bytes");
	}

	void TestBlobPublication()
	{
		TempTree tree("blob");
		const auto bytes = SyntheticDxbc();
		ContentDigest digest{};
		Check(
			ComputeDigests(bytes.data(), bytes.size(), digest),
			"blob digest failed");
		const auto sha256 = HexLower(
			digest.sha256.data(), digest.sha256.size());
		const auto expected =
			"blobs/sha256/" + sha256.substr(0, 2) + "/" + sha256 + ".dxbc";
		Check(BlobRelativePath(sha256) == expected, "relative blob path mismatch");

		const auto first = PublishBlob(
			tree.path, sha256, bytes.data(), bytes.size());
		Check(
			first.success,
			"first blob publication failed: " + first.error);
		Check(first.relativePath.generic_string() == expected, "published path mismatch");
		const auto second = PublishBlob(
			tree.path, sha256, bytes.data(), bytes.size());
		Check(
			second.success && second.alreadyExisted,
			"idempotent blob publication failed");

		{
			std::ofstream corrupt(tree.path / first.relativePath, std::ios::binary | std::ios::trunc);
			corrupt << "corrupt";
		}
		const auto rejected = PublishBlob(
			tree.path, sha256, bytes.data(), bytes.size());
		Check(!rejected.success, "corrupt existing blob was accepted");
		Check(
			!PublishBlob(
				tree.path, "../escape", bytes.data(), bytes.size()).success,
			"traversal digest was accepted");
	}

	void TestDirectoryCreateConvergence()
	{
		const std::string manifest = "{\"value\":1}\n";
		{
			TempTree tree("manifest-directory-race");
			std::filesystem::create_directory(tree.path / "runs");
			DirectoryCreateRace race(
				tree.path / "runs" / "concurrent-run");
			auto staged = StageManifest(
				tree.path, "concurrent-run", manifest);
			Check(
				staged.result.success,
				"manifest staging rejected concurrent directory creation");
			Check(
				g_directoryRaceWins.load(std::memory_order_relaxed) == 1
					&& !g_directoryRaceFailed.load(
						std::memory_order_relaxed),
				"manifest directory race seam did not win exactly once");
			DiscardStagedManifest(staged);
		}

		const auto bytes = SyntheticDxbc();
		ContentDigest digest{};
		Check(
			ComputeDigests(bytes.data(), bytes.size(), digest),
			"directory convergence blob digest failed");
		const auto sha256 = HexLower(
			digest.sha256.data(), digest.sha256.size());
		const auto prefix = sha256.substr(0, 2);
		{
			TempTree tree("blob-directory-race");
			std::filesystem::create_directories(
				tree.path / "blobs" / "sha256");
			DirectoryCreateRace race(
				tree.path / "blobs" / "sha256" / prefix);
			const auto result = PublishBlob(
				tree.path, sha256, bytes.data(), bytes.size());
			Check(
				result.success,
				"blob publication rejected concurrent directory creation");
			Check(
				g_directoryRaceWins.load(std::memory_order_relaxed) == 1
					&& !g_directoryRaceFailed.load(
						std::memory_order_relaxed),
				"blob directory race seam did not win exactly once");
		}

		{
			TempTree tree("manifest-directory-file");
			std::filesystem::create_directory(tree.path / "runs");
			std::ofstream(tree.path / "runs" / "file-component")
				<< "not a directory";
			auto staged = StageManifest(
				tree.path, "file-component", manifest);
			Check(
				!staged.result.success,
				"manifest accepted a file as a directory component");
		}
		{
			TempTree tree("blob-directory-file");
			std::filesystem::create_directories(
				tree.path / "blobs" / "sha256");
			std::ofstream(
				tree.path / "blobs" / "sha256" / prefix)
				<< "not a directory";
			Check(
				!PublishBlob(
					tree.path, sha256, bytes.data(), bytes.size()).success,
				"blob publication accepted a file as a directory component");
		}
	}

	void TestReparseDefense()
	{
		TempTree tree("reparse");
		const auto target = tree.path / "target";
		const auto link = tree.path / "link";
		std::filesystem::create_directory(target);
		const DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY
			| SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
		if (!CreateSymbolicLinkW(
				link.c_str(), target.c_str(), flags)) {
			std::cout << "reparse test skipped: symbolic links unavailable\n";
			return;
		}
		std::string error;
		Check(
			!ValidatePublicationRoot(link, error),
			"reparse publication root was accepted");
		std::filesystem::create_directory(target / "nested");
		error.clear();
		Check(
			!ValidatePublicationRoot(link / "nested", error),
			"reparse publication ancestor was accepted");
	}

	void TestImmutableManifestPublication()
	{
		TempTree tree("immutable-manifest");
		const auto root = tree.path / "root";
		std::filesystem::create_directory(root);
		const std::string first = "{\"value\":1}\n";
		const std::string second = "{\"value\":2}\n";

		auto firstA = StageManifest(root, "same-winner", first);
		auto firstB = StageManifest(root, "same-winner", first);
		Check(
			firstA.result.success && firstB.result.success,
			"identical manifests did not stage concurrently");
		Check(
			PublishStagedManifest(firstA).success,
			"first manifest publication failed");
		const auto secondWinner = PublishStagedManifest(firstB);
		Check(
			secondWinner.success && secondWinner.alreadyExisted,
			"identical staged loser did not verify its winner");

		auto heldA = StageManifest(root, "held-winner", first);
		auto heldB = StageManifest(root, "held-winner", first);
		Check(
			heldA.result.success && heldB.result.success,
			"retained-winner manifests did not stage");
		HoldNextPublishedWinnerForTesting();
		auto heldWinner = std::async(std::launch::async, [&] {
			return PublishStagedManifest(heldA);
		});
		bool winnerHeld = false;
		for (unsigned attempt = 0; attempt < 200 && !winnerHeld; ++attempt) {
			winnerHeld = PublishedWinnerHeldForTesting();
			if (!winnerHeld)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (!winnerHeld) {
			ReleasePublishedWinnerForTesting();
			(void)heldWinner.get();
			Check(false, "winner handle was not retained by the test seam");
		}
		auto heldLoser = std::async(std::launch::async, [&] {
			return PublishStagedManifest(heldB);
		});
		bool collisionRetried = false;
		for (unsigned attempt = 0;
			 attempt < 200 && !collisionRetried;
			 ++attempt) {
			collisionRetried = PublicationCollisionRetriedForTesting();
			if (!collisionRetried)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		ReleasePublishedWinnerForTesting();
		const auto heldWinnerResult = heldWinner.get();
		const auto heldLoserResult = heldLoser.get();
		Check(collisionRetried, "retained winner did not exercise collision retry");
		Check(heldWinnerResult.success, "retained winner failed publication");
		Check(
			heldLoserResult.success && heldLoserResult.alreadyExisted,
			"retained identical winner was not verified after retry");

		auto lateWinnerStage = StageManifest(
			root, "late-held-winner", first);
		Check(
			lateWinnerStage.result.success,
			"late-stager winner did not stage");
		HoldNextPublishedWinnerForTesting();
		auto lateWinner = std::async(std::launch::async, [&] {
			return PublishStagedManifest(lateWinnerStage);
		});
		winnerHeld = false;
		for (unsigned attempt = 0; attempt < 200 && !winnerHeld; ++attempt) {
			winnerHeld = PublishedWinnerHeldForTesting();
			if (!winnerHeld)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (!winnerHeld) {
			ReleasePublishedWinnerForTesting();
			(void)lateWinner.get();
			Check(false, "late-stager winner handle was not retained");
		}
		auto lateStage = std::async(std::launch::async, [&] {
			return StageManifest(root, "late-held-winner", first);
		});
		collisionRetried = false;
		for (unsigned attempt = 0;
			 attempt < 200 && !collisionRetried;
			 ++attempt) {
			collisionRetried = PublicationCollisionRetriedForTesting();
			if (!collisionRetried)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		ReleasePublishedWinnerForTesting();
		const auto lateWinnerResult = lateWinner.get();
		auto lateStaged = lateStage.get();
		Check(
			collisionRetried,
			"late stager did not retry the retained existing winner");
		Check(lateWinnerResult.success, "late-stager winner failed publication");
		Check(
			lateStaged.result.success && lateStaged.result.alreadyExisted,
			"late stager did not verify the retained existing winner");
		Check(
			PublishStagedManifest(lateStaged).success,
			"late-staged existing winner failed reverification");

		auto differentA = StageManifest(root, "collision", first);
		auto differentB = StageManifest(root, "collision", second);
		Check(
			differentA.result.success && differentB.result.success,
			"collision manifests did not stage");
		Check(
			PublishStagedManifest(differentA).success,
			"collision winner failed to publish");
		Check(
			!PublishStagedManifest(differentB).success,
			"different collision loser accepted winner bytes");

		auto existing = StageManifest(root, "same-winner", first);
		Check(
			existing.result.success && existing.result.alreadyExisted,
			"existing manifest did not retain verification state");
		const auto target =
			root / "runs" / "same-winner" / "manifest.v1.json";
		const HANDLE writer = CreateFileW(
			target.c_str(), GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Check(
			writer == INVALID_HANDLE_VALUE,
			"existing manifest allowed mutation while staged");
		if (writer != INVALID_HANDLE_VALUE)
			CloseHandle(writer);
		Check(
			PublishStagedManifest(existing).success,
			"existing manifest failed publish-time reverification");
	}

	void TestBrokerClassification()
	{
		auto* stock = reinterpret_cast<ID3D11PixelShader*>(0x1000);
		auto* replacement = reinterpret_cast<ID3D11PixelShader*>(0x2000);
		const auto passthrough =
			cs::engine::ClassifyPixelShaderSwapCompletion(
				S_OK, true, stock, true, false, stock);
		Check(passthrough.finalIsStock, "passthrough was not classified as stock");
		Check(
			!passthrough.finalIsReplacement,
			"passthrough was misclassified as replacement");
		Check(
			passthrough.resolverInvoked
				&& !passthrough.resolverReportedReplacement,
			"resolver reporting was not preserved");

		const auto replaced =
			cs::engine::ClassifyPixelShaderSwapCompletion(
				S_OK, true, stock, true, true, replacement);
		Check(replaced.finalIsReplacement, "replacement was not classified");
		Check(!replaced.finalIsStock, "replacement was classified as stock");

		const auto failed =
			cs::engine::ClassifyPixelShaderSwapCompletion(
				E_FAIL, true, nullptr, false, false, nullptr);
		Check(failed.finalIsNull, "failure did not complete as null output");
		Check(!failed.resolverInvoked, "resolver ran for failed original");

		const auto stale =
			cs::engine::ClassifyPixelShaderSwapCompletion(
				E_FAIL, true, stock, false, false, stock);
		Check(
			!stale.finalIsStock && !stale.finalIsReplacement,
			"failed HRESULT with stale output was classified as usable");
		const auto noOutput =
			cs::engine::ClassifyPixelShaderSwapCompletion(
				S_OK, false, stock, false, false, stock);
		Check(
			!noOutput.finalIsStock && !noOutput.finalIsNull,
			"unrequested output was classified as stock or null");
		const auto falseNull =
			cs::engine::ClassifyPixelShaderSwapCompletion(
				S_FALSE, true, nullptr, false, false, nullptr);
		Check(
			falseNull.finalIsNull && !falseNull.finalIsStock,
			"S_FALSE null output classification was wrong");
	}

	void TestCreationAttributionPolicy()
	{
		Check(
			CreationAttributionObjectKind(true, false)
				== AttributionObjectKind::kStock,
			"stock completion lost object kind");
		Check(
			CreationAttributionObjectKind(false, true)
				== AttributionObjectKind::kOriginatingStock,
			"replacement completion lost stock origin");
		Check(
			CreationAttributionObjectKind(false, false)
				== AttributionObjectKind::kSubmissionNoObject,
			"failed/null submission claimed a stock object");
	}

	void TestRunAggregationAndLifecycle()
	{
		TempTree tree("aggregate");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		const auto bytes = SyntheticDxbc();
		auto& catalog = CatalogDB::Get();

		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"catalog run 1 failed to start");
		const auto generatedRunId = catalog.GetStats().generatedRunId;
		catalog.EnqueueObservation(MakeOutcome(bytes, 1, S_OK, true));
		catalog.EnqueueObservation(MakeOutcome(bytes, 2, E_FAIL, false));
		catalog.EnqueueObservation(MakeOutcome(bytes, 3, S_OK, false));
		Check(catalog.Stop(), "catalog run 1 failed to finalize");
		Check(
			SqlText(
				database,
				"SELECT config_snapshot_json FROM sessions LIMIT 1")
				== "{\"writer_flush_interval_ms\":10,"
				   "\"subclass_attribution_requested\":true,"
				   "\"subclass_attribution_enabled\":false}",
			"legacy config snapshot omitted effective catalog settings");
		Check(
			SqlInt(
				database,
				"SELECT attempts FROM catalog_run_observations LIMIT 1")
				== 3,
			"attempt multiplicity was not aggregated");
		Check(
			SqlInt(
				database,
				"SELECT successes FROM catalog_run_observations LIMIT 1")
				== 1,
			"success outcome was not aggregated");
		Check(
			SqlInt(
				database,
				"SELECT failures FROM catalog_run_observations LIMIT 1")
				== 2,
			"failure/null outcome was not aggregated");
		Check(
			SqlInt(
				database,
				"SELECT null_outputs FROM catalog_run_observations LIMIT 1")
				== 2,
			"null outputs were not aggregated");
		Check(
			SqlInt(
				database,
				"SELECT output_requests FROM catalog_run_observations LIMIT 1")
				== 3,
			"output request state was not aggregated");
		Check(
			SqlText(
				database,
				"SELECT lifecycle FROM catalog_runs LIMIT 1")
				== "running",
			"pending run exposed finalized lifecycle");
		Check(
			SqlInt(
				database,
				"SELECT writer_drained FROM catalog_runs LIMIT 1")
				== 1,
			"run finalized before writer drain");
		Check(
			SqlInt(
				database,
				"SELECT orderly_finalizer_ready FROM catalog_runs LIMIT 1")
				== 1,
			"test orderly finalizer readiness was not persisted");
		Check(
			SqlInt(
				database,
				"SELECT authoritative FROM catalog_runs LIMIT 1")
				== 0,
			"pending run exposed database authority");
		Check(
			SqlInt(
				database,
				"SELECT manifest_published FROM catalog_runs LIMIT 1")
				== 0,
			"pending run exposed database publication");
		Check(
			catalog.GetStats().authoritative,
			"published run did not expose cached authority");
		Check(
			SqlInt(
				database,
				"SELECT publication_pending FROM catalog_runs LIMIT 1")
				== 1,
			"final publication crash window was not marked");
		const auto manifestPath = artifacts / "runs" / generatedRunId
			/ "manifest.v1.json";
		Check(std::filesystem::exists(manifestPath), "run manifest was not published");
		std::ifstream manifestFile(manifestPath, std::ios::binary);
		const std::string manifest{
			std::istreambuf_iterator<char>(manifestFile),
			std::istreambuf_iterator<char>()
		};
		Check(manifest.ends_with('\n'), "published manifest has no final LF");
		Check(
			manifest.find("source_pointer") == std::string::npos,
			"published manifest contains a pointer field");
		Check(
			manifest.find(tree.path.string()) == std::string::npos,
			"published manifest contains an absolute machine path");
		Check(
			manifest.find("DXBC synthetic host fixture") == std::string::npos,
			"published manifest contains shader bytes");

		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"catalog run 2 failed to start");
		Check(
			SqlInt(
				database,
				("SELECT publication_pending FROM catalog_runs "
				 "WHERE generated_run_id='" + generatedRunId + "'").c_str())
				== 0,
			"published manifest was not reconciled on restart");
		Check(
			SqlInt(
				database,
				("SELECT authoritative FROM catalog_runs "
				 "WHERE generated_run_id='" + generatedRunId + "'").c_str())
				== 1,
			"reconciled manifest did not promote database authority");
		catalog.EnqueueObservation(MakeOutcome(bytes, 4, S_OK, true));
		Check(catalog.Stop(), "catalog run 2 failed to finalize");
		Check(
			SqlInt(database, "SELECT COUNT(*) FROM catalog_runs") == 2,
			"two runs were not retained");
		Check(
			SqlInt(
				database,
				"SELECT COUNT(DISTINCT generated_run_id) "
				"FROM catalog_run_observations")
				== 2,
			"same digest was not separated by run");
	}

	void TestLegacyMigrationUnscoped()
	{
		TempTree tree("legacy");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		SqlExec(
			database,
			"PRAGMA foreign_keys=ON;"
			"CREATE TABLE sessions("
			"session_id TEXT PRIMARY KEY,started_at TEXT NOT NULL,ended_at TEXT,"
			"engine_runtime TEXT NOT NULL,engine_build_hash TEXT,"
			"plugin_version TEXT NOT NULL,plugin_git_sha TEXT,"
			"shaders_added_this_session INTEGER NOT NULL DEFAULT 0,"
			"compiles_observed_this_session INTEGER NOT NULL DEFAULT 0,"
			"config_snapshot_json TEXT);"
			"CREATE TABLE shader_catalog("
			"sha1 TEXT PRIMARY KEY,size_bytes INTEGER NOT NULL,"
			"stage TEXT NOT NULL,profile TEXT,cb_count INTEGER,srv_count INTEGER,"
			"uav_count INTEGER,sampler_count INTEGER,output_count INTEGER,"
			"input_count INTEGER,input_has_position_only INTEGER,"
			"instruction_count INTEGER,sample_call_count INTEGER,"
			"input_signature_summary TEXT,output_signature_summary TEXT,"
			"resource_summary TEXT,first_seen_timestamp TEXT NOT NULL,"
			"first_seen_session_id TEXT NOT NULL REFERENCES sessions(session_id),"
			"last_seen_timestamp TEXT NOT NULL,seen_count INTEGER NOT NULL DEFAULT 1,"
			"source_pointer_va TEXT,source_module TEXT,creation_stack_top4 TEXT,"
			"creation_thread_id INTEGER,engine_runtime TEXT NOT NULL,"
			"bsshader_subclass TEXT,bsshader_technique_bits INTEGER);"
			"CREATE TABLE compile_events("
			"rowid INTEGER PRIMARY KEY AUTOINCREMENT,result_sha1 TEXT,"
			"hlsl_source_path TEXT,hlsl_source_sha1 TEXT,defines_json TEXT,"
			"entry_point TEXT,target TEXT,compile_flags INTEGER,effect_flags INTEGER,"
			"source_name TEXT,timestamp TEXT NOT NULL,"
			"session_id TEXT NOT NULL REFERENCES sessions(session_id),"
			"source_module TEXT,creation_stack_top4 TEXT,creation_thread_id INTEGER);"
			"CREATE TABLE corpus_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
			"INSERT INTO corpus_meta VALUES('schema_version','2');"
			"INSERT INTO sessions(session_id,started_at,engine_runtime,plugin_version)"
			" VALUES('frozen-v2','2020-01-01T00:00:00Z','OG','0.0.0');"
			"INSERT INTO shader_catalog("
			"sha1,size_bytes,stage,first_seen_timestamp,first_seen_session_id,"
			"last_seen_timestamp,engine_runtime) VALUES("
			"'0123456789abcdef0123456789abcdef01234567',4,'ps',"
			"'2020-01-01T00:00:00Z','frozen-v2',"
			"'2020-01-01T00:00:00Z','OG');"
			"INSERT INTO compile_events(timestamp,session_id) "
			"VALUES('2020-01-01T00:00:00Z','frozen-v2');");

		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"v2 migration failed");
		Check(catalog.Stop(), "migrated catalog failed to stop");
		Check(
			SqlInt(database, "SELECT COUNT(*) FROM shader_catalog") == 1,
			"legacy row was not preserved");
		Check(
			SqlInt(database, "SELECT COUNT(*) FROM catalog_run_observations") == 0,
			"legacy row was assigned to a v3 run");
		Check(
			SqlText(
				database,
				"SELECT value FROM corpus_meta WHERE key='schema_version'")
				== "3",
			"migration did not reach schema v3");
		Check(
			SqlInt(database, "SELECT COUNT(*) FROM sessions") == 2
				&& SqlInt(database, "SELECT COUNT(*) FROM compile_events") == 1,
			"frozen v2 rows were not preserved");
	}

	void TestRawExportAssociationCompleteness()
	{
		TempTree tree("raw-association-complete");
		const auto database = tree.path / "catalog.sqlite";
		const auto root = tree.path / "root";
		std::filesystem::create_directory(root);
		const auto bytes = SyntheticDxbc();
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, RawExportConfig(database, root)),
			"raw association run failed to start");
		const auto runId = catalog.GetStats().generatedRunId;
		catalog.EnqueueObservation(MakeOutcome(bytes, 1, S_OK, true));
		bool observed = false;
		for (int attempt = 0; attempt < 100 && !observed; ++attempt) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			observed = SqlInt(
				database,
				("SELECT COUNT(*) FROM catalog_run_blobs WHERE "
				 "generated_run_id='" + runId + "'").c_str()) == 1;
		}
		Check(observed, "raw association was not persisted");
		SqlExec(
			database,
			("DELETE FROM catalog_run_blobs WHERE generated_run_id='"
				+ runId + "'").c_str());
		Check(
			!catalog.Stop(),
			"missing expected raw association remained authoritative");
		Check(
			SqlInt(
				database,
				("SELECT raw_export_complete FROM catalog_runs WHERE "
				 "generated_run_id='" + runId + "'").c_str())
				== 0,
			"missing raw association reported complete");
	}

	void TestAbandonedRecovery()
	{
		TempTree tree("abandoned");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"abandoned seed failed to start");
		Check(catalog.Stop(), "abandoned seed failed to stop");
		SqlExec(
			database,
			"UPDATE catalog_runs SET lifecycle='running',ended_at=NULL,"
			"authoritative=0,manifest_published=0,publication_pending=0,"
			"pending_authoritative=0;");

		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"recovery run failed to start");
		Check(catalog.Stop(), "recovery run failed to stop");
		Check(
			SqlInt(
				database,
				"SELECT COUNT(*) FROM catalog_runs WHERE lifecycle='abandoned'")
				== 1,
			"prior running run was not abandoned");
		Check(
			SqlInt(
				database,
				"SELECT lifecycle_failure FROM catalog_run_quality q "
				"JOIN catalog_runs r USING(generated_run_id) "
				"WHERE r.lifecycle='abandoned'")
				== 1,
			"abandonment quality was not durable");
	}

	void TestSchemaIncompatibility()
	{
		TempTree tree("schema");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"schema seed failed to start");
		Check(catalog.Stop(), "schema seed failed to stop");
		SqlExec(
			database,
			"UPDATE corpus_meta SET value='99' WHERE key='schema_version';");
		Check(
			!catalog.Start(TestConfig(database, artifacts), TestIdentity()),
			"newer schema was accepted");
		int version = 0;
		std::string error;
		Check(
			CatalogDB::InspectSchemaVersion(database, version, error)
				&& version == 99,
			"newer schema was destructively reset");
		SqlExec(
			database,
			"UPDATE corpus_meta SET value='invalid' WHERE key='schema_version';");
		Check(
			!catalog.Start(TestConfig(database, artifacts), TestIdentity()),
			"malformed schema version was accepted");
		Check(
			!CatalogDB::InspectSchemaVersion(database, version, error),
			"malformed schema version was not reported");
	}

	void TestFailureAuthorityGates()
	{
		{
			TempTree tree("evidence-failure");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			auto config = TestConfig(database, artifacts);
			RunPolicy policy;
			policy.evidenceMode = true;
			policy.environmentValid = true;
			policy.evidenceIdsSatisfied = false;
			config.policyOverride = policy;
			auto& catalog = CatalogDB::Get();
			Check(
				StartReady(catalog, config),
				"evidence failure run failed to start");
			Check(!catalog.Stop(), "missing evidence IDs reported complete");
			Check(
				SqlInt(
					database,
					"SELECT authoritative FROM catalog_runs LIMIT 1")
					== 0,
				"missing evidence IDs remained authoritative");
			Check(
				SqlInt(
					database,
					"SELECT external_run_id IS NULL AND scenario_id IS NULL "
					"FROM catalog_runs LIMIT 1")
					== 1,
				"missing evidence identity was invented");
		}
		{
			TempTree tree("raw-failure");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			auto config = TestConfig(database, artifacts);
			RunPolicy policy;
			policy.environmentValid = false;
			policy.evidenceIdsSatisfied = true;
			policy.rawExportRequested = true;
			policy.exportRootValid = false;
			config.policyOverride = policy;
			auto& catalog = CatalogDB::Get();
			Check(
				StartReady(catalog, config),
				"raw failure run failed to start");
			Check(!catalog.Stop(), "raw failure run reported complete");
			Check(
				SqlInt(
					database,
					"SELECT authoritative FROM catalog_runs LIMIT 1")
					== 0,
				"raw export failure remained authoritative");
			Check(
				SqlInt(
					database,
					"SELECT raw_export_failure FROM catalog_run_quality LIMIT 1")
					> 0,
				"raw export failure was not durable");
		}
		{
			TempTree tree("manifest-failure");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			auto config = TestConfig(database, artifacts);
			config.finalizationFaults.publication = true;
			auto& catalog = CatalogDB::Get();
			Check(
				StartReady(catalog, config),
				"manifest failure run failed to start");
			Check(!catalog.Stop(), "manifest failure run reported complete");
			Check(
				SqlInt(
					database,
					"SELECT authoritative FROM catalog_runs LIMIT 1")
					== 0,
				"manifest failure remained authoritative");
			Check(
				SqlInt(
					database,
					"SELECT manifest_failure FROM catalog_run_quality LIMIT 1")
					> 0,
				"manifest failure was not durable");
		}
	}

	void TestBoundsAndQueueQuality()
	{
		const std::byte one{ 0x42 };
		const auto huge = PrepareObservation(
			'v', &one, kMaxShaderBytecodeBytes + 1, 1);
		Check(
			huge.bytecodeState == BytecodeState::kUnsupportedSize,
			"huge bytecode was not rejected before copy");

		TempTree tree("queue");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"queue run failed to start");
		std::vector<std::thread> producers;
		for (unsigned thread = 0; thread < 8; ++thread) {
			producers.emplace_back([&catalog, thread] {
				for (std::uint64_t i = 0; i < 10000; ++i) {
					ObservationOutcome outcome;
					outcome.prepared = PrepareObservation(
						'v', nullptr, 0,
						(static_cast<std::uint64_t>(thread) << 32) | i,
						0);
					outcome.hresult = static_cast<std::int32_t>(E_INVALIDARG);
					outcome.outputRequested = true;
					outcome.finalIsNull = true;
					catalog.EnqueueObservation(std::move(outcome));
				}
			});
		}
		for (auto& producer : producers)
			producer.join();
		Check(!catalog.Stop(), "lossy queue run reported complete");
		Check(
			SqlInt(
				database,
				"SELECT queue_overflow FROM catalog_run_quality LIMIT 1")
				> 0,
			"queue overflow was not durably counted");
		Check(
			SqlInt(
				database,
				"SELECT authoritative FROM catalog_runs LIMIT 1")
				== 0,
			"lossy queue run remained authoritative");
	}

	void TestProducerLeaseStopRace()
	{
		using namespace std::chrono_literals;
		TempTree tree("lease");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"lease run failed to start");
		auto lease = catalog.TryAcquireProducerLease();
		Check(static_cast<bool>(lease), "producer lease admission failed");
		auto stopped = std::async(std::launch::async, [&catalog] {
			return catalog.Stop();
		});
		std::this_thread::sleep_for(50ms);
		Check(
			stopped.wait_for(0ms) != std::future_status::ready,
			"Stop finalized while a producer lease was active");
		Check(
			!catalog.TryAcquireProducerLease(),
			"producer admission remained open after Stop");
		lease.Reset();
		Check(
			stopped.wait_for(2s) == std::future_status::ready,
			"Stop did not resume after producer completion");
		Check(stopped.get(), "lease run did not finalize authoritatively");
	}

	void TestPixelAdmissionAllocationFailure()
	{
		TempTree tree("pixel-admission-allocation");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"pixel admission run failed to start");
		const cs::engine::PixelShaderSwapObserver observer{
			&BeginTestPixelAdmission,
			&EndTestPixelAdmission,
			&FailTestPixelTokenAllocation,
			nullptr,
			&CompleteTestPixelObserver
		};
		auto invocation = cs::engine::BeginPixelShaderSwapObserver(
			observer, nullptr, 0);
		Check(
			invocation.active && invocation.admitted
				&& invocation.token == nullptr,
			"forced token allocation failure lost admission");
		auto stopped = std::async(std::launch::async, [&catalog] {
			return catalog.Stop();
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		Check(
			stopped.wait_for(std::chrono::milliseconds(0))
				!= std::future_status::ready,
			"Stop crossed failed-token admission");
		cs::engine::CompletePixelShaderSwapObserver(
			invocation, {});
		Check(
			stopped.wait_for(std::chrono::seconds(2))
				== std::future_status::ready,
			"Stop did not observe failed-token admission release");
		Check(
			!stopped.get(),
			"allocation failure remained authoritative");
		Check(
			SqlInt(
				database,
				"SELECT allocation_failure FROM catalog_run_quality")
				== 1,
			"token allocation failure was not durable");
	}

	void TestFinalizationFaultSeams()
	{
		const std::pair<const char*, int> faults[] = {
			{ "persistence", 0 },
			{ "checkpoint", 1 },
			{ "publication", 2 }
		};
		for (const auto& [name, kind] : faults) {
			TempTree tree(std::string("fault-") + name);
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			auto config = TestConfig(database, artifacts);
			config.finalizationFaults.persistence = kind == 0;
			config.finalizationFaults.checkpoint = kind == 1;
			config.finalizationFaults.publication = kind == 2;
			auto& catalog = CatalogDB::Get();
			Check(StartReady(catalog, config), "fault run failed to start");
			const auto runId = catalog.GetStats().generatedRunId;
			Check(!catalog.Stop(), "fault run reported authoritative");
			Check(
				!std::filesystem::exists(
					artifacts / "runs" / runId / "manifest.v1.json"),
				"fault run exposed a contract manifest");
			Check(
				SqlText(
					database,
					"SELECT lifecycle FROM catalog_runs LIMIT 1")
					== "abandoned",
				"fault run was not conservatively abandoned");
			Check(
				SqlInt(
					database,
					"SELECT authoritative FROM catalog_runs LIMIT 1")
					== 0,
				"fault run remained authoritative");
			Check(
				SqlInt(
					database,
					"SELECT COUNT(*) FROM sessions WHERE ended_at IS NOT NULL")
					== 1,
				"fault run left its legacy session open");
			Check(
				SqlInt(
					database,
					"SELECT lifecycle_failure FROM catalog_run_quality")
					== 1,
				"fault run lifecycle failure count was not exact");
			Check(
				SqlInt(
					database,
					"SELECT manifest_failure FROM catalog_run_quality")
					== (kind == 2 ? 1 : 0),
				"fault run manifest failure count was not exact");
		}
	}

	void TestPublicationCrashRecovery()
	{
		auto& catalog = CatalogDB::Get();
		{
			TempTree tree("publication-promote");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"publication promotion seed failed to start");
			const auto runId = catalog.GetStats().generatedRunId;
			Check(catalog.Stop(), "publication promotion seed failed");
			Check(
				SqlInt(
					database,
					"SELECT COUNT(*) FROM catalog_runs WHERE "
					"publication_pending=1 AND lifecycle='running' "
					"AND authoritative=0 AND manifest_published=0")
					== 1,
				"pending row exposed finalized authority");
			Check(
				SqlExecFails(
					database,
					"UPDATE catalog_runs SET authoritative=1 "
					"WHERE publication_pending=1"),
				"database authority invariant accepted a pending row");
			auto equivalentRootText = artifacts.native();
			if (equivalentRootText.size() > 1
				&& equivalentRootText[1] == L':'
				&& equivalentRootText[0] >= L'A'
				&& equivalentRootText[0] <= L'Z') {
				equivalentRootText[0] =
					static_cast<wchar_t>(equivalentRootText[0] - L'A' + L'a');
			}
			Check(
				StartReady(
					catalog,
					TestConfig(database, std::filesystem::path(equivalentRootText))),
				"same-root recovery failed to start");
			Check(
				SqlInt(
					database,
					("SELECT COUNT(*) FROM catalog_runs WHERE "
					 "generated_run_id='" + runId + "' AND "
					 "lifecycle='finalized' AND authoritative=1 "
					 "AND manifest_published=1 AND publication_pending=0")
						.c_str())
					== 1,
				"verified pending manifest was not promoted");
			Check(catalog.Stop(), "same-root recovery failed to stop");
		}
		{
			TempTree tree("publication-missing");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"missing publication seed failed to start");
			const auto runId = catalog.GetStats().generatedRunId;
			Check(catalog.Stop(), "missing publication seed failed");
			std::filesystem::remove(
				artifacts / "runs" / runId / "manifest.v1.json");
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"missing publication recovery failed to start");
			Check(
				SqlInt(
					database,
					("SELECT COUNT(*) FROM catalog_runs WHERE "
					 "generated_run_id='" + runId + "' AND "
					 "lifecycle='abandoned' AND authoritative=0 "
					 "AND manifest_published=0 AND publication_pending=0")
						.c_str())
					== 1,
				"missing pending manifest was not abandoned");
			Check(catalog.Stop(), "missing recovery failed to stop");
		}
		{
			TempTree tree("publication-different-root");
			const auto database = tree.path / "catalog.sqlite";
			const auto rootA = tree.path / "root-a";
			const auto rootB = tree.path / "root-b";
			std::filesystem::create_directory(rootA);
			std::filesystem::create_directory(rootB);
			Check(
				StartReady(catalog, TestConfig(database, rootA)),
				"different-root seed failed to start");
			const auto runId = catalog.GetStats().generatedRunId;
			Check(catalog.Stop(), "different-root seed failed");
			Check(
				StartReady(catalog, TestConfig(database, rootB)),
				"different-root recovery failed to start");
			Check(
				SqlInt(
					database,
					("SELECT COUNT(*) FROM catalog_runs WHERE "
					 "generated_run_id='" + runId + "' AND "
					 "lifecycle='abandoned' AND authoritative=0 "
					 "AND publication_pending=0").c_str())
					== 1,
				"different-root pending row was ignored");
			Check(catalog.Stop(), "different-root recovery failed to stop");
		}
	}

	void TestHookCoverageAuthority()
	{
		{
			TempTree tree("no-d3d");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			auto& catalog = CatalogDB::Get();
			Check(
				catalog.Start(
					TestConfig(database, artifacts), TestIdentity()),
				"no-D3D run failed to start");
			const auto runId = catalog.GetStats().generatedRunId;
			Check(!catalog.Stop(), "no-D3D run became authoritative");
			Check(
				SqlInt(
					database,
					"SELECT hook_observer_gap FROM catalog_run_quality")
					== 1,
				"no-D3D run did not persist its coverage gap");
			Check(
				SqlInt(
					database,
					"SELECT hook_coverage_ready FROM catalog_runs")
					== 0,
				"no-D3D run claimed hook readiness");
			std::ifstream input(
				artifacts / "runs" / runId / "manifest.v1.json",
				std::ios::binary);
			const std::string manifest{
				std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>()
			};
			Check(
				manifest.find("\"authoritative\":false")
					!= std::string::npos,
				"incomplete hook manifest claimed authority");
		}

		{
			TempTree tree("observer-failure");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			auto& catalog = CatalogDB::Get();
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"observer-failure run failed to start");
			catalog.RecordHookObserverGap();
			Check(
				!catalog.Stop(),
				"observer registration failure remained authoritative");
		}
	}

	void TestOrderlyFinalizerGate()
	{
		TempTree tree("orderly-finalizer-missing");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto config = TestConfig(database, artifacts);
		config.orderlyFinalizerReadyForTesting = false;
		auto& catalogDb = CatalogDB::Get();
		Check(
			catalogDb.Start(config, TestIdentity()),
			"missing-finalizer run failed to start");
		catalogDb.MarkHookCoverageReady();
		const auto runId = catalogDb.GetStats().generatedRunId;
		Check(
			!catalogDb.Stop(),
			"run without orderly finalizer became authoritative");
		Check(
			SqlInt(
				database,
				"SELECT orderly_finalizer_ready FROM catalog_runs")
				== 0,
			"missing orderly finalizer was persisted as ready");
		std::ifstream input(
			artifacts / "runs" / runId / "manifest.v1.json",
			std::ios::binary);
		const std::string manifest{
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
		Check(
			manifest.find("\"orderly_finalizer_ready\":false")
				!= std::string::npos,
			"manifest omitted missing orderly finalizer state");
		Check(
			StartReady(
				catalogDb,
				TestConfig(database, artifacts)),
			"orderly finalizer invariant restart failed");
		Check(
			SqlExecFails(
				database,
				("UPDATE catalog_runs SET authoritative=1 "
				 "WHERE generated_run_id='" + runId + "'").c_str()),
			"database accepted authority without an orderly finalizer");
		Check(
			catalogDb.Stop(),
			"orderly finalizer invariant cleanup failed");
	}

	void TestHookCoverageReduction()
	{
		cs::features::catalog::hooks::HookCoverage coverage{
			true, true, true, true, true, true, true, true, true
		};
		Check(coverage.Complete(), "complete hook surface was rejected");
		bool* fields[] = {
			&coverage.vertex,
			&coverage.geometry,
			&coverage.geometryStreamOutput,
			&coverage.pixel,
			&coverage.hull,
			&coverage.domain,
			&coverage.compute,
			&coverage.pixelBinding,
			&coverage.observer
		};
		for (auto* field : fields) {
			*field = false;
			Check(
				!coverage.Complete(),
				"missing hook slot did not reduce coverage");
			*field = true;
		}
	}

	void TestOutcomeSemanticsAndOrdering()
	{
		TempTree tree("outcomes");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		const auto bytes = SyntheticDxbc();
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"outcome run failed to start");

		auto stale = MakeOutcome(bytes, 50, E_FAIL, true);
		stale.prepared.qpc = 500;
		stale.finalIsStock = false;
		stale.finalIsNull = false;
		catalog.EnqueueObservation(std::move(stale));

		auto okNull = MakeOutcome(bytes, 10, S_OK, false);
		okNull.prepared.qpc = 100;
		catalog.EnqueueObservation(std::move(okNull));

		auto falseNull = MakeOutcome(bytes, 30, S_FALSE, false);
		falseNull.prepared.qpc = 300;
		catalog.EnqueueObservation(std::move(falseNull));

		auto noOutput = MakeOutcome(bytes, 20, S_OK, false);
		noOutput.prepared.qpc = 200;
		noOutput.outputRequested = false;
		noOutput.finalIsNull = false;
		catalog.EnqueueObservation(std::move(noOutput));

		Check(catalog.Stop(), "outcome run failed to finalize");
		Check(
			SqlInt(database, "SELECT successes FROM catalog_run_observations")
				== 0,
			"unusable outcomes counted as successful");
		Check(
			SqlInt(database, "SELECT failures FROM catalog_run_observations")
				== 4,
			"unusable outcomes were not counted as failures");
		Check(
			SqlInt(database, "SELECT null_outputs FROM catalog_run_observations")
				== 2,
			"null output count included stale or unrequested outputs");
		Check(
			SqlInt(
				database,
				"SELECT raw_output_nonnull FROM catalog_run_observations")
				== 1,
			"raw stale output state was not preserved");
		Check(
			SqlInt(
				database,
				"SELECT COUNT(*) FROM catalog_run_hresult_details")
				== 3,
			"failed outcomes did not retain HRESULT details");
		Check(
			SqlInt(
				database,
				"SELECT occurrence_count FROM catalog_run_hresult_details "
				"WHERE hresult=0")
				== 2,
			"S_OK null/no-output failures were not retained");
		Check(
			SqlInt(database, "SELECT first_sequence FROM catalog_run_observations")
				== 10,
			"first sequence did not use MIN");
		Check(
			SqlInt(database, "SELECT last_sequence FROM catalog_run_observations")
				== 50,
			"last sequence did not use MAX");
		Check(
			SqlInt(database, "SELECT first_qpc FROM catalog_run_observations")
				== 100,
			"first QPC did not use MIN");
		Check(
			SqlInt(database, "SELECT last_qpc FROM catalog_run_observations")
				== 500,
			"last QPC did not use MAX");
	}

	void TestEnvironmentEmptyValues()
	{
		EnvironmentValues values;
		values.evidenceMode = "";
		Check(
			!ParseRunPolicy(values).environmentValid,
			"present-empty evidence boolean was treated as absent");

		values = {};
		values.externalRunId = "";
		Check(
			!ParseRunPolicy(values).environmentValid,
			"present-empty run identifier was treated as absent");

		values = {};
		values.scenarioId = "";
		Check(
			!ParseRunPolicy(values).environmentValid,
			"present-empty scenario identifier was treated as absent");

		values = {};
		values.configId = "";
		Check(
			!ParseRunPolicy(values).environmentValid,
			"present-empty config identifier was treated as absent");

		values = {};
		values.sourceId = "";
		Check(
			!ParseRunPolicy(values).environmentValid,
			"present-empty source identifier was treated as absent");

		values = {};
		values.corpusRoot = "";
		const auto corpus = ParseRunPolicy(values);
		Check(
			!corpus.environmentValid && corpus.rawExportRequested,
			"present-empty corpus root disabled export silently");
	}

	void TestGuardedInputCopies()
	{
		const auto pageSize = [] {
			SYSTEM_INFO info{};
			GetSystemInfo(&info);
			return static_cast<std::size_t>(info.dwPageSize);
		}();
		void* protectedPage = VirtualAlloc(
			nullptr, pageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		Check(protectedPage != nullptr, "PAGE_NOACCESS fixture allocation failed");
		std::memset(protectedPage, 0x42, pageSize);
		DWORD oldProtection = 0;
		Check(
			VirtualProtect(
				protectedPage, pageSize, PAGE_NOACCESS,
				&oldProtection) != FALSE,
			"PAGE_NOACCESS fixture protection failed");

		const auto bytecode = PrepareObservation(
			'v', protectedPage, 32, 1);
		Check(
			bytecode.bytecodeState == BytecodeState::kCopyFailure,
			"inaccessible bytecode was not classified as copy failure");

		const auto declaration = PrepareStreamOutputIdentity(
			static_cast<const D3D11_SO_DECLARATION_ENTRY*>(protectedPage),
			1, nullptr, 0, 0);
		Check(
			declaration.copyFailure && !declaration.valid,
			"inaccessible SO declaration was not a copy failure");

		const auto strides = PrepareStreamOutputIdentity(
			nullptr, 0, static_cast<const UINT*>(protectedPage), 1, 0);
		Check(
			strides.copyFailure && !strides.valid,
			"inaccessible SO stride was not a copy failure");

		DWORD writable = 0;
		Check(
			VirtualProtect(
				protectedPage, pageSize, PAGE_READWRITE,
				&writable) != FALSE,
			"PAGE_NOACCESS fixture restore failed");
		D3D11_SO_DECLARATION_ENTRY entry{};
		entry.SemanticName = static_cast<const char*>(protectedPage);
		Check(
			VirtualProtect(
				protectedPage, pageSize, PAGE_NOACCESS,
				&writable) != FALSE,
			"semantic fixture protection failed");
		const auto semantic = PrepareStreamOutputIdentity(
			&entry, 1, nullptr, 0, 0);
		Check(
			semantic.copyFailure && !semantic.valid,
			"inaccessible SO semantic was not a copy failure");
		VirtualFree(protectedPage, 0, MEM_RELEASE);

		TempTree tree("copy-quality");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		const auto bytes = SyntheticDxbc();
		auto outcome = MakeOutcome(bytes, 1, S_OK, true);
		outcome.prepared.streamOutput = semantic;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"copy-failure quality run failed to start");
		catalog.EnqueueObservation(std::move(outcome));
		Check(
			!catalog.Stop(),
			"SO copy failure remained authoritative");
		Check(
			SqlInt(database, "SELECT copy_failure FROM catalog_run_quality")
				== 1,
			"SO copy failure was not durable");
	}

	void TestStreamOutputQualityClassification()
	{
		std::string longSemantic(kMaxSemanticBytes + 8, 'X');
		D3D11_SO_DECLARATION_ENTRY entry{
			0, longSemantic.c_str(), 0, 0, 4, 0
		};
		const struct Case
		{
			const char* name;
			StreamOutputIdentity identity;
			const char* qualityColumn;
		} cases[] = {
			{
				"unsupported",
				PrepareStreamOutputIdentity(
					nullptr, kMaxStreamOutputEntries + 1,
					nullptr, 0, 0),
				"unsupported_size"
			},
			{
				"allocation",
				StreamOutputIdentity{
					true, false, false, false,
					StreamOutputState::kAllocationFailure
				},
				"allocation_failure"
			},
			{
				"hash",
				StreamOutputIdentity{
					true, false, false, false,
					StreamOutputState::kHashFailure
				},
				"hash_failure"
			},
			{
				"truncated",
				PrepareStreamOutputIdentity(&entry, 1, nullptr, 0, 0),
				"metadata_truncated"
			}
		};
		const auto bytes = SyntheticDxbc();
		for (const auto& test : cases) {
			TempTree tree(std::string("so-quality-") + test.name);
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			auto outcome = MakeOutcome(bytes, 1, S_OK, true);
			outcome.prepared.streamOutput = test.identity;
			auto& catalog = CatalogDB::Get();
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"SO quality run failed to start");
			const auto runId = catalog.GetStats().generatedRunId;
			catalog.EnqueueObservation(std::move(outcome));
			Check(
				!catalog.Stop(),
				"SO identity failure remained authoritative");
			const std::string query =
				"SELECT " + std::string(test.qualityColumn)
				+ " FROM catalog_run_quality";
			Check(
				SqlInt(database, query.c_str()) == 1,
				"SO failure did not reach its quality counter");
			std::ifstream input(
				artifacts / "runs" / runId / "manifest.v1.json",
				std::ios::binary);
			const std::string manifest{
				std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>()
			};
			Check(
				manifest.find("\"authoritative\":false")
					!= std::string::npos,
				"SO failure manifest claimed authority");
		}
	}

	void TestMalformedV2Rollback()
	{
		TempTree tree("malformed-v2");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		SqlExec(
			database,
			"CREATE TABLE corpus_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
			"INSERT INTO corpus_meta VALUES('schema_version','2');"
			"CREATE TABLE sessions(session_id TEXT PRIMARY KEY);"
			"INSERT INTO sessions VALUES('legacy-row');"
			"CREATE TABLE shader_catalog(sha1 TEXT PRIMARY KEY);"
			"INSERT INTO shader_catalog VALUES('legacy-shader');"
			"CREATE TABLE compile_events(rowid INTEGER PRIMARY KEY);"
			"INSERT INTO compile_events VALUES(1);");
		auto& catalog = CatalogDB::Get();
		Check(
			!catalog.Start(
				TestConfig(database, artifacts), TestIdentity()),
			"malformed v2 schema was migrated");
		int version = 0;
		std::string error;
		Check(
			CatalogDB::InspectSchemaVersion(database, version, error)
				&& version == 2,
			"failed migration changed the schema version");
		Check(
			SqlInt(database, "SELECT COUNT(*) FROM sessions") == 1,
			"failed migration destroyed legacy rows");
		Check(
			SqlInt(
				database,
				"SELECT COUNT(*) FROM sqlite_master "
				"WHERE type='table' AND name='catalog_runs'")
				== 0,
			"failed migration committed partial v3 structure");
	}

	void TestLegacyV3ObservationUpgrade()
	{
		TempTree tree("legacy-v3-observation");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		const auto bytes = SyntheticDxbc();
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"legacy v3 seed failed to start");
		catalog.EnqueueObservation(MakeOutcome(bytes, 1, E_FAIL, false));
		Check(catalog.Stop(), "legacy v3 seed failed to stop");

		SqlExec(
			database,
			"UPDATE catalog_run_observations SET "
			"raw_output_nonnull=7,other_hresult_count=9,"
			"hresult_details_truncated=1;");
		SqlExec(
			database,
			"CREATE TABLE catalog_run_observations_reduced AS SELECT "
			"generated_run_id,observation_key,stage,content_sha256,"
			"bytecode_state,submitted_size,stream_output_digest,"
			"stream_output_declaration_state,stream_output_declaration_count,"
			"stream_output_strides_state,stream_output_stride_count,"
			"stream_output_rasterized_stream,stream_output_metadata_truncated,"
			"attempts,successes,failures,null_outputs,resolver_invocations,"
			"resolver_reported_replacements,final_stock,final_replacement,"
			"final_null,replacement_sha256,first_sequence,last_sequence,"
			"first_qpc,last_qpc,first_thread_id,first_module,first_stack,"
			"raw_output_nonnull,other_hresult_count,hresult_details_truncated,"
			"stream_output_state FROM catalog_run_observations;"
			"DROP TABLE catalog_run_observations;"
			"ALTER TABLE catalog_run_observations_reduced "
			"RENAME TO catalog_run_observations;");
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"legacy v3 observation schema failed to upgrade");
		auto copyFailure = MakeOutcome(bytes, 2, E_FAIL, false);
		copyFailure.prepared.bytecodeState = BytecodeState::kCopyFailure;
		copyFailure.prepared.digest.reset();
		copyFailure.prepared.bytecode.reset();
		catalog.EnqueueObservation(std::move(copyFailure));
		Check(
			!catalog.Stop(),
			"copy failure in upgraded v3 run remained authoritative");
		Check(
			SqlInt(
				database,
				"SELECT COUNT(*) FROM catalog_run_observations "
				"WHERE bytecode_state='copy_failure'")
				== 1,
			"upgraded v3 schema rejected copy failures");
		Check(
			SqlInt(
				database,
				"SELECT COUNT(*) FROM catalog_run_observations "
				"WHERE bytecode_state='exact'")
				== 1,
			"v3 observation upgrade lost prior rows");
		Check(
			SqlInt(
				database,
				"SELECT raw_output_nonnull FROM catalog_run_observations "
				"WHERE bytecode_state='exact'")
					== 7
				&& SqlInt(
					database,
					"SELECT other_hresult_count "
					"FROM catalog_run_observations "
					"WHERE bytecode_state='exact'")
					== 9
				&& SqlInt(
					database,
					"SELECT hresult_details_truncated "
					"FROM catalog_run_observations "
					"WHERE bytecode_state='exact'")
					== 1,
			"v3 observation upgrade lost aggregate detail fields");
		Check(
			SqlInt(
				database,
				"SELECT output_requests FROM catalog_run_observations "
				"WHERE bytecode_state='exact'")
				== 0,
			"v3 observation upgrade did not default absent output requests");
	}

	void TestHresultOverflow()
	{
		TempTree tree("hresult-overflow");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		const auto bytes = SyntheticDxbc();
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"HRESULT overflow run failed to start");
		for (std::uint64_t i = 0; i < 10; ++i) {
			const auto result = static_cast<HRESULT>(
				0x80040000u + static_cast<std::uint32_t>(i));
			catalog.EnqueueObservation(
				MakeOutcome(bytes, i + 1, result, false));
		}
		const auto runId = catalog.GetStats().generatedRunId;
		Check(
			!catalog.Stop(),
			"truncated HRESULT detail remained authoritative");
		Check(
			SqlInt(
				database,
				"SELECT COUNT(*) FROM catalog_run_hresult_details")
				== 8,
			"HRESULT detail bound was not enforced");
		Check(
			SqlInt(
				database,
				"SELECT other_hresult_count FROM catalog_run_observations")
				== 2,
			"HRESULT overflow count was not explicit");
		Check(
			SqlInt(
				database,
				"SELECT hresult_details_truncated "
				"FROM catalog_run_observations")
				== 1,
			"HRESULT truncation flag was not set");
		Check(
			SqlInt(
				database,
				"SELECT metadata_truncated FROM catalog_run_quality")
				>= 2,
			"HRESULT truncation did not quality-gate authority");
		std::ifstream input(
			artifacts / "runs" / runId / "manifest.v1.json",
			std::ios::binary);
		const std::string manifest{
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
		Check(
			manifest.find("\"other_hresult_count\":2") != std::string::npos
				&& manifest.find("\"hresult_details_truncated\":true")
					!= std::string::npos,
			"manifest omitted HRESULT overflow");
	}

	void TestAttributionIdentityKinds()
	{
		TempTree tree("attribution-kinds");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		const auto bytes = SyntheticDxbc();
		ContentDigest digest{};
		Check(
			ComputeDigests(bytes.data(), bytes.size(), digest),
			"attribution digest failed");
		Sha1Result sha{};
		sha.bytes = digest.sha1;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"attribution run failed to start");
		const auto runId = catalog.GetStats().generatedRunId;
		catalog.EnqueueObservation(MakeOutcome(bytes, 1, S_OK, true));
		catalog.EnqueueAttribution(
			sha, "BSLightingShader", 0,
			AttributionKind::kObservedBinding,
			AttributionObjectKind::kStock);
		catalog.EnqueueAttribution(
			sha, "BSLightingShader", 0,
			AttributionKind::kObservedBinding,
			AttributionObjectKind::kReplacementUnknown);
		catalog.EnqueueAttribution(
			sha, "BSLightingShader", 0,
			AttributionKind::kCreationContext,
			AttributionObjectKind::kOriginatingStock);
		catalog.EnqueueAttribution(
			sha, "BSLightingShader", 0,
			AttributionKind::kTechniqueMapAssociation,
			AttributionObjectKind::kStock);
		catalog.EnqueueAttribution(
			sha, "BSLightingShader", 0,
			AttributionKind::kCreationContext,
			AttributionObjectKind::kSubmissionNoObject);
		Check(catalog.Stop(), "attribution run failed to finalize");
		Check(
			SqlInt(
				database,
				"SELECT COUNT(*) FROM catalog_run_attributions "
				"WHERE technique_bits=0")
				== 5,
			"technique value zero was treated as unknown");
		std::ifstream input(
			artifacts / "runs" / runId / "manifest.v1.json",
			std::ios::binary);
		const std::string manifest{
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
		Check(
			manifest.find("\"technique_bits\":0")
				!= std::string::npos,
			"manifest lost technique value zero");
		Check(
			manifest.find(
				"\"sha1\":null,\"originating_stock_sha1\":")
				!= std::string::npos
				&& manifest.find(
					"\"object_kind\":\"replacement_unknown\"")
					!= std::string::npos,
			"replacement binding was emitted as stock identity");
		Check(
			manifest.find("\"attribution_kind\":\"creation_context\"")
					!= std::string::npos
				&& manifest.find("\"attribution_kind\":\"observed_binding\"")
					!= std::string::npos
				&& manifest.find(
					"\"attribution_kind\":\"technique_map_association\"")
					!= std::string::npos
				&& manifest.find(
					"\"object_kind\":\"submission_no_object\"")
					!= std::string::npos,
			"manifest omitted attribution kinds");
	}

	void TestPixelTrackerAliasLookup()
	{
		TempTree tree("tracker-conflict");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		const auto bytes = SyntheticDxbc();
		ContentDigest digest{};
		Check(
			ComputeDigests(bytes.data(), bytes.size(), digest),
			"tracker digest failed");
		Sha1Result sha{};
		sha.bytes = digest.sha1;
		Sha1Result otherSha = sha;
		otherSha.bytes[0] ^= 0xff;
		FakePixelShader shader;
		FakePixelShader allocationShader;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"tracker conflict run failed to start");
		shader_tracker::SetEnabled(true);
		Check(
			shader_tracker::TrackPixelShader(&shader, sha, true)
				== shader_tracker::TrackResult::kTracked,
			"pixel tracker did not register alias");
		shader_tracker::Lookup lookup;
		Check(
			shader_tracker::TryGetPixelShader(&shader, lookup),
			"tracked pixel shader lookup failed");
		Check(lookup.alias, "pixel tracker discarded alias state");
		Check(lookup.sha.bytes == sha.bytes, "pixel tracker changed digest");
		Check(
			shader_tracker::TrackPixelShader(&shader, otherSha, true)
				== shader_tracker::TrackResult::kAmbiguousOrigin,
			"pixel tracker accepted conflicting stock origins");
		Check(
			shader_tracker::TryGetPixelShader(&shader, lookup)
				&& lookup.ambiguousOrigin
				&& Sha1IsZero(lookup.sha),
			"pixel tracker exposed a singular ambiguous origin");
		catalog.RecordHookObserverGap();
#ifdef FO4CS_SHADER_CATALOG_TESTING
		shader_tracker::FailNextAllocationForTesting();
#endif
		Check(
			shader_tracker::TrackPixelShader(
				&allocationShader, sha, false)
				== shader_tracker::TrackResult::kAllocationFailure,
			"pixel tracker allocation failure was swallowed");
		catalog.RecordAllocationFailure();
		shader_tracker::SetEnabled(false);
		Check(
			shader.references.load(std::memory_order_relaxed) == 1,
			"pixel tracker did not release retained shader");
		Check(
			allocationShader.references.load(std::memory_order_relaxed) == 1,
			"failed tracker allocation retained shader");
		Check(
			!catalog.Stop(),
			"tracker ambiguity/allocation remained authoritative");
		Check(
			SqlInt(
				database,
				"SELECT hook_observer_gap FROM catalog_run_quality")
					== 1
				&& SqlInt(
					database,
					"SELECT allocation_failure FROM catalog_run_quality")
					== 1
				&& SqlInt(
					database,
					"SELECT COUNT(*) FROM catalog_run_attributions")
					== 0,
			"tracker failures were not durable or emitted attribution");
	}

	void TestPerRunBlobAssociations()
	{
		TempTree tree("run-blobs");
		const auto database = tree.path / "catalog.sqlite";
		const auto rootA = tree.path / "root-a";
		const auto disabledRoot = tree.path / "disabled";
		const auto rootB = tree.path / "root-b";
		std::filesystem::create_directory(rootA);
		std::filesystem::create_directory(disabledRoot);
		std::filesystem::create_directory(rootB);
		const auto bytes = SyntheticDxbc();
		ContentDigest digest{};
		Check(
			ComputeDigests(bytes.data(), bytes.size(), digest),
			"run blob digest failed");
		const auto sha256 = HexLower(
			digest.sha256.data(), digest.sha256.size());
		const auto relative = BlobRelativePath(sha256);
		auto& catalog = CatalogDB::Get();

		Check(
			StartReady(catalog, RawExportConfig(database, rootA)),
			"root-A run failed to start");
		const auto rootARun = catalog.GetStats().generatedRunId;
		catalog.EnqueueObservation(MakeOutcome(bytes, 1, S_OK, true));
		Check(catalog.Stop(), "root-A run failed to finalize");
		Check(
			std::filesystem::exists(rootA / relative),
			"root-A blob was not exported");

		Check(
			StartReady(catalog, TestConfig(database, disabledRoot)),
			"export-disabled run failed to start");
		const auto disabledRun = catalog.GetStats().generatedRunId;
		catalog.EnqueueObservation(MakeOutcome(bytes, 2, S_OK, true));
		Check(
			catalog.Stop(),
			"export-disabled run failed to finalize");
		std::ifstream disabledInput(
			disabledRoot / "runs" / disabledRun / "manifest.v1.json",
			std::ios::binary);
		const std::string disabledManifest{
			std::istreambuf_iterator<char>(disabledInput),
			std::istreambuf_iterator<char>()
		};
		Check(
			disabledManifest.find("\"relative_path\":null")
				!= std::string::npos,
			"export-disabled run inherited root-A blob path");
		Check(
			SqlInt(database, "SELECT COUNT(*) FROM catalog_run_blobs") == 1,
			"export-disabled run inherited a blob association");
		Check(
			SqlText(
				database,
				("SELECT lifecycle FROM catalog_runs "
				 "WHERE generated_run_id='" + rootARun + "'").c_str())
				== "abandoned",
			"root-A pending run was not abandoned under a different root");

		Check(
			StartReady(catalog, RawExportConfig(database, rootB)),
			"root-B run failed to start");
		const auto rootBRun = catalog.GetStats().generatedRunId;
		catalog.EnqueueObservation(MakeOutcome(bytes, 3, S_OK, true));
		Check(catalog.Stop(), "root-B run failed to finalize");
		Check(
			std::filesystem::exists(rootB / relative),
			"root-B blob was not exported");
		Check(
			SqlInt(database, "SELECT COUNT(*) FROM catalog_run_blobs") == 2,
			"per-run blob associations were not isolated");
		std::ifstream rootBInput(
			rootB / "runs" / rootBRun / "manifest.v1.json",
			std::ios::binary);
		const std::string rootBManifest{
			std::istreambuf_iterator<char>(rootBInput),
			std::istreambuf_iterator<char>()
		};
		Check(
			rootBManifest.find(
				"\"relative_path\":\"" + relative + "\"")
				!= std::string::npos,
			"root-B manifest omitted its canonical blob path");
	}

	void TestInjectedBlobPathRejected()
	{
		using namespace std::chrono_literals;
		TempTree tree("blob-injection");
		const auto database = tree.path / "catalog.sqlite";
		const auto root = tree.path / "artifacts";
		std::filesystem::create_directory(root);
		const auto bytes = SyntheticDxbc();
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, RawExportConfig(database, root)),
			"path injection run failed to start");
		const auto runId = catalog.GetStats().generatedRunId;
		catalog.EnqueueObservation(MakeOutcome(bytes, 1, S_OK, true));
		bool associated = false;
		for (int attempt = 0; attempt < 100 && !associated; ++attempt) {
			std::this_thread::sleep_for(10ms);
			associated =
				SqlInt(database, "SELECT COUNT(*) FROM catalog_run_blobs")
				== 1;
		}
		Check(associated, "blob association was not persisted in time");
		SqlExec(
			database,
			"UPDATE catalog_run_blobs "
			"SET relative_path='C:/absolute/injected.dxbc'");
		Check(
			!catalog.Stop(),
			"injected blob path remained authoritative");
		Check(
			!std::filesystem::exists(
				root / "runs" / runId / "manifest.v1.json"),
			"injected blob path reached a contract manifest");
	}

	constexpr std::string_view kEmptyRouteRegistrySha256 =
		"f145a5ce82643093352f6c02a3d4a177"
		"06697e07538f7a47529081cb4997bdf0";

	RouteResolverRegistrySnapshot g_routeRegistrySnapshot{
		true,
		0,
		true,
		std::string(kEmptyRouteRegistrySha256)
	};
	bool g_routeHooksReady = true;

	RouteResolverRegistrySnapshot RouteRegistrySnapshotForTesting() noexcept
	{
		return g_routeRegistrySnapshot;
	}

	bool RouteHooksReadyForTesting() noexcept
	{
		return g_routeHooksReady;
	}

	// Restores the route registry and hook-readiness seams even when a Check throws.
	class RouteCaptureSeamScope
	{
	public:
		RouteCaptureSeamScope() { Reset(); }

		~RouteCaptureSeamScope()
		{
			try {
				Reset();
			} catch (...) {
			}
		}

		RouteCaptureSeamScope(const RouteCaptureSeamScope&) = delete;
		RouteCaptureSeamScope& operator=(
			const RouteCaptureSeamScope&) = delete;

	private:
		static void Reset()
		{
			g_routeRegistrySnapshot = {
				true, 0, true, std::string(kEmptyRouteRegistrySha256)
			};
			g_routeHooksReady = true;
		}
	};

	RouteCodeIdentity TestRouteCodeIdentity(
		std::string a_symbol,
		std::uint32_t a_rva,
		char a_digest)
	{
		return {
			"FO4CommunityShaders.dll",
			std::move(a_symbol),
			a_rva,
			{ a_rva, 16 },
			std::string(64, a_digest)
		};
	}

	RouteCaptureScope TestRouteCaptureScope()
	{
		RouteCaptureScope scope;
		scope.createHook = TestRouteCodeIdentity(
			"PixelShaderSwapBroker::CreatePixelShaderHook::thunk",
			0x1000,
			'b');
		scope.bindHook = TestRouteCodeIdentity(
			"ShaderCatalog::PSSetShaderHook::thunk",
			0x1100,
			'c');
		scope.pluginRuntimeResolver = {
			"ShaderVariantRuntimeResolver",
			"1",
			"fo4cs.pixel-shader-runtime-route.v1",
			std::string(64, 'd'),
			{
				{
					{ "formula" },
					"ResolvePixelShaderVariantFormula",
					0x1200,
					{ 0x1200, 16 },
					std::string(64, 'e')
				},
				{
					{ "runtime-gate", "tiled-state" },
					"ResolvePixelShaderRuntimeRoute",
					0x1300,
					{ 0x1300, 16 },
					std::string(64, 'f')
				}
			}
		};
		scope.resolverRegistryOpen = g_routeRegistrySnapshot;
		scope.resolverRegistrySnapshot =
			&RouteRegistrySnapshotForTesting;
		scope.hookCoverageReady = &RouteHooksReadyForTesting;
		return scope;
	}

	std::unique_ptr<StockRuntimeRoutePublisher> OpenTestRoutePublisher(
		const std::filesystem::path& a_root,
		RoutePublicationError& a_error)
	{
		return StockRuntimeRoutePublisher::Open(
			a_root,
			{
				"FO4CommunityShaders",
				"1.2.3",
				std::string(64, '1')
			},
			{
				"AE",
				"1.11.221",
				std::string(64, '2')
			},
			{
				"11111111-2222-4333-8444-555555555555"
			},
			TestRouteCaptureScope(),
			a_error);
	}

	RouteCreateInput TestRouteCreateInput(bool a_linkage = false)
	{
		return {
			.routePresent = true,
			.subclass = "BSDFLightShader",
			.stage = "ps",
			.rawTechnique = 0x01200202,
			.pluginResolvedPsid = 0x01200202,
			.tiledLighting = std::nullopt,
			.classLinkagePresent = a_linkage
		};
	}

	RouteCreateOutcome TestRouteCreateOutcome()
	{
		return {
			.byteLength = 24416,
			.sha1 = std::string(40, '3'),
			.sha256 = std::string(64, '4'),
			.hresult = 0,
			.creationSucceeded = true,
			.outputNonNull = true,
			.originalInputUnchanged = true,
			.finalObjectStock = true,
			.resolverInvoked = false
		};
	}

	// Opens a publisher under a_root and seeds one complete create plus its matching bind.
	std::unique_ptr<StockRuntimeRoutePublisher> SeedRoutePublisher(
		const std::filesystem::path& a_root)
	{
		auto error = RoutePublicationError::kNone;
		auto publisher = OpenTestRoutePublisher(a_root, error);
		Check(
			publisher && error == RoutePublicationError::kNone,
			"seeded route publisher did not open");
		auto admission =
			publisher->BeginCreate(TestRouteCreateInput(true));
		Check(
			static_cast<bool>(admission),
			"seeded route create admission failed");
		auto created = publisher->CompleteCreate(
			std::move(admission), TestRouteCreateOutcome());
		Check(
			created.enqueued && created.record,
			"seeded route create was not queued");
		{
			std::scoped_lock lock(created.record->mutex);
			created.record->bindReserved = true;
		}
		auto bind = publisher->BeginBind();
		Check(
			static_cast<bool>(bind),
			"seeded route bind admission failed");
		Check(
			publisher->RecordBind(
				std::move(bind),
				created.record,
				RouteBindSnapshot{
					"BSDFLightShader",
					"ps",
					0x01200202,
					std::nullopt
				}),
			"seeded route bind was not recorded");
		return publisher;
	}

	std::string ReadTextFile(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		Check(input.is_open(), "unable to read route receipt file");
		return {
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
	}

	void TestRouteReceiptPublication()
	{
		TempTree tree("route-receipt");
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		g_routeHooksReady = true;
		RoutePublicationError error = RoutePublicationError::kNone;
		auto publisher = OpenTestRoutePublisher(tree.path, error);
		Check(
			publisher && error == RoutePublicationError::kNone,
			"route publisher did not open");
		auto admission =
			publisher->BeginCreate(TestRouteCreateInput(true));
		Check(
			static_cast<bool>(admission),
			"route create admission failed");
		auto created = publisher->CompleteCreate(
			std::move(admission), TestRouteCreateOutcome());
		Check(
			created.enqueued
				&& created.usableStockObject
				&& created.record,
			"route create was not queued");
		{
			std::scoped_lock lock(created.record->mutex);
			created.record->bindReserved = true;
		}
		auto bindAdmission = publisher->BeginBind();
		Check(
			static_cast<bool>(bindAdmission),
			"route bind admission failed");
		Check(
			publisher->RecordBind(
				std::move(bindAdmission),
				created.record,
				RouteBindSnapshot{
					"BSDFLightShader",
					"ps",
					0x01200202,
					std::nullopt
				}),
			"route bind was not recorded");

		FrozenRouteSnapshot snapshot;
		Check(
			publisher->CloseCaptureAdmissionAndFreeze(
				snapshot, g_routeHooksReady, error)
				&& snapshot.records.size() == 1,
			"route capture did not freeze");
		const auto canonicalA =
			BuildCanonicalRouteObservation(
				snapshot.records.front().observation);
		const auto canonicalB =
			BuildCanonicalRouteObservation(
				snapshot.records.front().observation);
		Check(
			canonicalA == canonicalB
				&& canonicalA.starts_with(
					"{\"authority_reasons\":[]")
				&& canonicalA.ends_with('\n'),
			"route observation is not deterministic canonical JSON");
		Check(
			canonicalA.find("\"observed_lookup_psid\":null")
					!= std::string::npos
				&& canonicalA.find(
					"\"engine_lookup_observed\":false")
					!= std::string::npos
				&& canonicalA.find(
					"\"plugin_resolved_psid\":18874882")
					!= std::string::npos
				&& canonicalA.find(
					"\"gpu_execution_observed\":false")
					!= std::string::npos
				&& canonicalA.find(
					"\"class_linkage_state\":\"present\"")
					!= std::string::npos,
			"route observation lost domain-separated provenance");
		Check(
			canonicalA.find("fxp") == std::string::npos
				&& canonicalA.find("archive") == std::string::npos
				&& canonicalA.find("admission") == std::string::npos
				&& canonicalA.find("fidelity") == std::string::npos,
			"route observation leaked a forbidden claim");

		const auto observation =
			publisher->PublishObservation(snapshot.records.front());
		Check(
			observation.success
				&& std::filesystem::exists(
					observation.document.path),
			"route observation publication failed");
		const auto manifest = publisher->FinalizeRun();
		Check(
			manifest.success
				&& std::filesystem::exists(manifest.document.path),
			"route manifest publication failed");
		const auto observationJson =
			ReadTextFile(observation.document.path);
		const auto manifestJson =
			ReadTextFile(manifest.document.path);
		Check(
			observationJson == canonicalA,
			"published route observation bytes changed");
		Check(
			manifestJson.find("\"capture_authoritative\":true")
					!= std::string::npos
				&& manifestJson.find(
					"\"scenario_id\":\"stock-pixel-shader-routes-v1\"")
					!= std::string::npos
				&& manifestJson.find("\"create_attempts\":1")
					!= std::string::npos
				&& manifestJson.find(
					"\"uncommitted_sequences\":0")
					!= std::string::npos,
			"route manifest omitted authority or accounting");
	}

	void TestRouteCaptureDisabledByDefault()
	{
		TempTree tree("route-disabled");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"disabled route capture run did not start");
		const auto stats = catalog.GetStats();
		Check(
			!stats.routeCaptureRequested
				&& !stats.routeCaptureActive,
			"route capture was active by default");
		Check(
			!catalog.RoutePublisherPresentForTesting(),
			"a disabled route capture built a publisher");
		Check(catalog.Stop(), "disabled route capture run did not stop");
		Check(
			!catalog.RoutePublisherPresentForTesting(),
			"a disabled route capture retained a publisher");
		Check(
			!std::filesystem::exists(artifacts / "observations")
				&& !std::filesystem::exists(artifacts / "manifests"),
			"disabled route capture published route documents");
	}

	void TestRouteCaptureLossAndRegistryViolation()
	{
		TempTree tree("route-loss");
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		g_routeHooksReady = true;
		RoutePublicationError error = RoutePublicationError::kNone;
		auto publisher = OpenTestRoutePublisher(tree.path, error);
		Check(
			static_cast<bool>(publisher),
			"route loss publisher did not open");
		auto admission =
			publisher->BeginCreate(TestRouteCreateInput());
		auto invalid = TestRouteCreateOutcome();
		invalid.sha1.clear();
		Check(
			!publisher->CompleteCreate(
				std::move(admission), invalid).enqueued,
			"invalid route create was queued");
		FrozenRouteSnapshot snapshot;
		Check(
			publisher->CloseCaptureAdmissionAndFreeze(
				snapshot, g_routeHooksReady, error)
				&& snapshot.records.empty(),
			"lossy route capture did not freeze");
		const auto manifest = publisher->FinalizeRun();
		Check(manifest.success, "lossy route manifest was not published");
		const auto manifestJson =
			ReadTextFile(manifest.document.path);
		Check(
			manifestJson.find("\"capture_authoritative\":false")
					!= std::string::npos
				&& manifestJson.find("\"queue-loss\"")
					!= std::string::npos
				&& manifestJson.find("\"sequence-loss\"")
					!= std::string::npos,
			"route queue/sequence loss was not fail-closed");

		TempTree registryTree("route-registry");
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		auto registryPublisher =
			OpenTestRoutePublisher(registryTree.path, error);
		auto registryAdmission =
			registryPublisher->BeginCreate(TestRouteCreateInput());
		auto created = registryPublisher->CompleteCreate(
			std::move(registryAdmission), TestRouteCreateOutcome());
		{
			std::scoped_lock lock(created.record->mutex);
			created.record->bindReserved = true;
		}
		auto registryBind = registryPublisher->BeginBind();
		Check(
			registryPublisher->RecordBind(
				std::move(registryBind),
				created.record,
				RouteBindSnapshot{
					"BSDFLightShader",
					"ps",
					0x01200202,
					std::nullopt
				}),
			"registry fixture bind failed");
		g_routeRegistrySnapshot = {
			true, 1, false, std::string(64, 'b')
		};
		Check(
			registryPublisher->CloseCaptureAdmissionAndFreeze(
				snapshot, g_routeHooksReady, error),
			"registry-violation capture did not freeze");
		const auto recordJson =
			BuildCanonicalRouteObservation(
				snapshot.records.front().observation);
		Check(
			recordJson.find("\"run-stock-only-violation\"")
					!= std::string::npos
				&& recordJson.find(
					"\"capture_authoritative\":false")
					!= std::string::npos,
			"resolver registry change did not invalidate record");
		Check(
			registryPublisher->PublishObservation(
				snapshot.records.front()).success,
			"registry-violation observation did not publish");
		const auto registryManifest =
			registryPublisher->FinalizeRun();
		Check(
			registryManifest.success
				&& ReadTextFile(registryManifest.document.path)
					.find("\"stock-only-violation\"")
					!= std::string::npos,
			"resolver registry change did not invalidate run");
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};

		TempTree mutationTree("route-input-change");
		auto mutationPublisher =
			OpenTestRoutePublisher(mutationTree.path, error);
		auto mutationAdmission =
			mutationPublisher->BeginCreate(TestRouteCreateInput());
		auto mutation = TestRouteCreateOutcome();
		mutation.originalInputUnchanged = false;
		auto mutationRecord = mutationPublisher->CompleteCreate(
			std::move(mutationAdmission), mutation);
		Check(
			mutationRecord.enqueued,
			"changed-input diagnostic was not queued");
		Check(
			mutationPublisher->CloseCaptureAdmissionAndFreeze(
				snapshot, g_routeHooksReady, error),
			"changed-input capture did not freeze");
		const auto mutationJson =
			BuildCanonicalRouteObservation(
				snapshot.records.front().observation);
		Check(
			mutationJson.find("\"original-input-changed\"")
					!= std::string::npos
				&& mutationJson.find(
					"\"capture_authoritative\":false")
					!= std::string::npos,
			"changed original input remained authoritative");

		TempTree excludedTree("route-excluded-input");
		auto excludedPublisher =
			OpenTestRoutePublisher(excludedTree.path, error);
		auto excludedInput = TestRouteCreateInput();
		excludedInput.routePresent = false;
		auto excludedAdmission =
			excludedPublisher->BeginCreate(excludedInput);
		Check(
			static_cast<bool>(excludedAdmission),
			"excluded callback did not retain its capture lease");
		auto excludedOutcome = TestRouteCreateOutcome();
		excludedOutcome.originalInputUnchanged = false;
		Check(
			!excludedPublisher->CompleteCreate(
				std::move(excludedAdmission),
				excludedOutcome).enqueued,
			"excluded callback produced an observation");
		Check(
			excludedPublisher->CloseCaptureAdmissionAndFreeze(
				snapshot, g_routeHooksReady, error)
				&& snapshot.records.empty(),
			"excluded callback capture did not freeze");
		const auto excludedManifest =
			excludedPublisher->FinalizeRun();
		const auto excludedJson =
			ReadTextFile(excludedManifest.document.path);
		Check(
			excludedManifest.success,
			"excluded input manifest did not publish");
		Check(
			excludedJson.find(
				"\"excluded_missing_route_context\":1")
				!= std::string::npos,
			"excluded callback was not counted");
		Check(
			excludedJson.find(
				"\"original_input_changes\":1")
				!= std::string::npos,
			"excluded input mutation was not counted");
		Check(
			excludedJson.find("\"stock-only-violation\"")
				!= std::string::npos,
			"excluded input mutation did not invalidate stock-only run");

		TempTree hookTree("route-hook-coverage");
		auto hookPublisher =
			OpenTestRoutePublisher(hookTree.path, error);
		g_routeHooksReady = false;
		Check(
			hookPublisher->CloseCaptureAdmissionAndFreeze(
				snapshot, g_routeHooksReady, error),
			"hook-coverage capture did not freeze");
		const auto hookManifest = hookPublisher->FinalizeRun();
		const auto hookJson =
			ReadTextFile(hookManifest.document.path);
		Check(
			hookManifest.success
				&& hookJson.find(
					"\"capture_authoritative\":false")
					!= std::string::npos
				&& hookJson.find("\"producer-declined\"")
					!= std::string::npos,
			"missing hook coverage produced authoritative manifest");
		g_routeHooksReady = true;

		TempTree excludedLossTree("route-excluded-loss");
		auto excludedLossPublisher =
			OpenTestRoutePublisher(excludedLossTree.path, error);
		auto excludedLossInput = TestRouteCreateInput();
		excludedLossInput.routePresent = false;
		{
			auto abandoned =
				excludedLossPublisher->BeginCreate(excludedLossInput);
			Check(
				static_cast<bool>(abandoned),
				"excluded admission was not retained");
		}
		Check(
			excludedLossPublisher->CloseCaptureAdmissionAndFreeze(
				snapshot, g_routeHooksReady, error),
			"abandoned excluded capture did not freeze");
		const auto excludedLossManifest =
			excludedLossPublisher->FinalizeRun();
		const auto excludedLossJson =
			ReadTextFile(excludedLossManifest.document.path);
		Check(
			excludedLossManifest.success
				&& excludedLossJson.find("\"counter-mismatch\"")
					!= std::string::npos
				&& excludedLossJson.find(
					"\"capture_authoritative\":false")
					!= std::string::npos,
			"abandoned excluded admission remained authoritative");

		TempTree registryDigestTree("route-empty-registry-digest");
		auto invalidScope = TestRouteCaptureScope();
		invalidScope.resolverRegistryOpen.sha256 =
			std::string(64, 'a');
		auto invalidRegistryPublisher =
			StockRuntimeRoutePublisher::Open(
				registryDigestTree.path,
				{
					"FO4CommunityShaders",
					"1.2.3",
					std::string(64, '1')
				},
				{
					"AE",
					"1.11.221",
					std::string(64, '2')
				},
				{
					"11111111-2222-4333-8444-555555555555"
				},
				invalidScope,
				error);
		Check(
			!invalidRegistryPublisher
				&& error
					== RoutePublicationError::kStockOnlyViolation,
			"arbitrary empty registry digest was accepted");
	}

	void TestRouteCaptureLeaseRace()
	{
		using namespace std::chrono_literals;
		TempTree tree("route-lease");
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		RoutePublicationError error = RoutePublicationError::kNone;
		auto publisher = OpenTestRoutePublisher(tree.path, error);
		auto admission =
			publisher->BeginCreate(TestRouteCreateInput());
		Check(
			static_cast<bool>(admission),
			"route lease was not acquired");
		FrozenRouteSnapshot snapshot;
		auto frozen = std::async(std::launch::async, [&] {
			RoutePublicationError freezeError =
				RoutePublicationError::kNone;
			return publisher->CloseCaptureAdmissionAndFreeze(
				snapshot, g_routeHooksReady, freezeError);
		});
		std::this_thread::sleep_for(50ms);
		Check(
			frozen.wait_for(0ms) != std::future_status::ready,
			"route capture froze with an active lease");
		admission = {};
		Check(
			frozen.wait_for(2s) == std::future_status::ready
				&& frozen.get(),
			"route capture did not resume after lease release");
	}

	void TestRouteLineageConflictAndStaleOutput()
	{
		FakePixelShader shader;
		const auto bytes = SyntheticDxbc();
		ContentDigest digest{};
		Check(
			ComputeDigests(bytes.data(), bytes.size(), digest),
			"route lineage digest failed");
		Sha1Result sha{};
		sha.bytes = digest.sha1;
		auto first =
			std::make_shared<RouteCaptureRecordState>();
		auto second =
			std::make_shared<RouteCaptureRecordState>();
		first->observation.lineage.status =
			RouteLineageStatus::kPendingBind;
		second->observation.lineage.status =
			RouteLineageStatus::kPendingBind;
		shader_tracker::SetEnabled(true);
		Check(
			shader_tracker::TrackRouteLineage(
				&shader, sha, first)
				== shader_tracker::RouteTrackResult::kTracked,
			"route lineage was not tracked");
		Check(
			shader_tracker::TryReserveRouteBind(&shader) == first
				&& !shader_tracker::TryReserveRouteBind(&shader),
			"route bind reservation was not unique");
		{
			std::scoped_lock lock(first->mutex);
			first->bindReserved = false;
		}
		RouteBindEvent priorBind{
			.eventId = "prior-bind",
			.sequence = 3,
			.threadId = 7
		};
		first->observation.bind = priorBind;
		first->observation.facts.bindObserved = true;
		first->observation.lineage.pointerLineageEventId =
			"prior-bind";
		first->observation.lineage.bindRouteContextMatch = true;
		Check(
			shader_tracker::TrackRouteLineage(
				&shader, sha, second)
				== shader_tracker::RouteTrackResult::kDuplicate,
			"late duplicate route lineage was not rejected");
		Check(
			!shader_tracker::TryReserveRouteBind(&shader),
			"duplicate route lineage remained bindable");
		{
			std::scoped_lock firstLock(first->mutex);
			std::scoped_lock secondLock(second->mutex);
			Check(
				first->observation.lineage.status
						== RouteLineageStatus::kDuplicate
					&& second->observation.lineage.status
						== RouteLineageStatus::kDuplicate
					&& first->observation.bind
					&& first->observation.bind->eventId
						== "prior-bind",
				"duplicate status was not applied to both records");
		}
		shader_tracker::SetEnabled(false);
		Check(
			shader.references.load(std::memory_order_relaxed) == 1,
			"route tracker leaked a shader reference");

		TempTree tree("route-stale");
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		RoutePublicationError error = RoutePublicationError::kNone;
		auto publisher = OpenTestRoutePublisher(tree.path, error);
		auto admission =
			publisher->BeginCreate(TestRouteCreateInput());
		auto stale = TestRouteCreateOutcome();
		stale.creationSucceeded = false;
		stale.outputNonNull = true;
		stale.finalObjectStock.reset();
		const auto created = publisher->CompleteCreate(
			std::move(admission), stale);
		Check(
			created.enqueued
				&& !created.usableStockObject
				&& !created.record->observation.lineage.shaderObjectId
				&& created.record->observation.lineage.status
					== RouteLineageStatus::kNotCreated,
			"failed stale output received route object identity");
	}

	class ManualTime final : public route_capture::TimeSource
	{
	public:
		route_capture::Clock::time_point Now() const noexcept override
		{
			return route_capture::Clock::time_point(
				route_capture::Clock::duration(
					_now.load(std::memory_order_acquire)));
		}

		void Attach(
			std::mutex& a_mutex,
			std::condition_variable& a_signal) noexcept override
		{
			_mutex = &a_mutex;
			_signal = &a_signal;
		}

		void WaitUntil(
			std::unique_lock<std::mutex>& a_lock,
			std::condition_variable& a_signal,
			route_capture::Clock::time_point a_deadline) override
		{
			if (Now() >= a_deadline)
				return;
			a_signal.wait(a_lock);
		}

		void Advance(route_capture::Clock::duration a_delta)
		{
			Check(_mutex != nullptr, "manual time source was not attached");
			{
				std::scoped_lock lock(*_mutex);
				_now.fetch_add(
					a_delta.count(), std::memory_order_acq_rel);
			}
			_signal->notify_all();
		}

	private:
		std::atomic<route_capture::Clock::rep> _now{ 1 };
		std::mutex* _mutex = nullptr;
		std::condition_variable* _signal = nullptr;
	};

	constexpr auto kTestWaitBudget = std::chrono::hours(24);

	// Read and cleared from the coordinator worker while the test thread publishes it.
	std::atomic<route_capture::Coordinator*> g_sealTarget{ nullptr };

	bool SealFinalizationForTesting() noexcept
	{
		auto* target = g_sealTarget.load(std::memory_order_acquire);
		return target != nullptr && target->SealFinalizationDecision();
	}

	// Clears the finalization seal target even when a Check throws.
	class SealTargetScope
	{
	public:
		explicit SealTargetScope(
			route_capture::Coordinator& a_coordinator)
		{
			g_sealTarget.store(&a_coordinator, std::memory_order_release);
		}

		~SealTargetScope()
		{
			g_sealTarget.store(nullptr, std::memory_order_release);
		}

		SealTargetScope(const SealTargetScope&) = delete;
		SealTargetScope& operator=(const SealTargetScope&) = delete;
	};

	std::atomic<std::promise<void>*> g_admissionClosedPromise{ nullptr };

	void NotifyAdmissionClosedForTesting() noexcept
	{
		if (auto* promise = g_admissionClosedPromise.exchange(
				nullptr, std::memory_order_acq_rel))
			promise->set_value();
	}

	// Proves Stop passed its producer cutoff without polling, and clears the seam on throw.
	class AdmissionClosedProbe
	{
	public:
		AdmissionClosedProbe()
		{
			g_admissionClosedPromise.store(
				&_closed, std::memory_order_release);
			CatalogDB::SetAdmissionClosedCallbackForTesting(
				&NotifyAdmissionClosedForTesting);
		}

		~AdmissionClosedProbe()
		{
			CatalogDB::SetAdmissionClosedCallbackForTesting(nullptr);
			g_admissionClosedPromise.store(
				nullptr, std::memory_order_release);
		}

		AdmissionClosedProbe(const AdmissionClosedProbe&) = delete;
		AdmissionClosedProbe& operator=(
			const AdmissionClosedProbe&) = delete;

		void Wait() { _reached.wait(); }

	private:
		std::promise<void> _closed;
		std::future<void> _reached{ _closed.get_future() };
	};

	// Observes the interval where the shared gate is shut but route state is live.
	struct SharedGateObservation
	{
		bool fired = false;
		bool createAdmitted = false;
		bool bindAdmitted = false;
		bool generationStillActive = false;
		bool genericStillOpen = false;
	};

	std::atomic<SharedGateObservation*> g_sharedGateObservation{ nullptr };

	void ObserveSharedGateForTesting() noexcept
	{
		auto* observation =
			g_sharedGateObservation.load(std::memory_order_acquire);
		if (!observation)
			return;
		auto& catalog = CatalogDB::Get();
		// Tokens are scoped so an unexpected admission releases before the drain.
		{
			auto create =
				catalog.BeginRouteCreate(TestRouteCreateInput(true));
			auto bind = catalog.BeginRouteBind();
			observation->createAdmitted = static_cast<bool>(create);
			observation->bindAdmitted = static_cast<bool>(bind);
		}
		observation->generationStillActive = catalog.RouteCaptureActive();
		observation->genericStillOpen = catalog.TryBeginProducerAdmission();
		if (observation->genericStillOpen)
			catalog.EndProducerAdmission();
		observation->fired = true;
	}

	// Clears the shared-gate seam even when a Check throws.
	class SharedGateProbe
	{
	public:
		explicit SharedGateProbe(SharedGateObservation& a_observation)
		{
			g_sharedGateObservation.store(
				&a_observation, std::memory_order_release);
			CatalogDB::SetSharedGateClosedCallbackForTesting(
				&ObserveSharedGateForTesting);
		}

		~SharedGateProbe()
		{
			CatalogDB::SetSharedGateClosedCallbackForTesting(nullptr);
			g_sharedGateObservation.store(
				nullptr, std::memory_order_release);
		}

		SharedGateProbe(const SharedGateProbe&) = delete;
		SharedGateProbe& operator=(const SharedGateProbe&) = delete;
	};

	DbConfig RouteCaptureRunConfig(		const std::filesystem::path& a_database,
		const std::filesystem::path& a_artifacts,
		const std::filesystem::path& a_routeRoot)
	{
		auto config = TestConfig(a_database, a_artifacts);
		config.routeCapture.requested = true;
		config.routeCapture.outputRoot = a_routeRoot;
		config.routeCapture.producer = {
			"FO4CommunityShaders",
			"1.2.3",
			std::string(64, '1')
		};
		config.routeCapture.runtime = {
			"AE",
			"1.11.221",
			std::string(64, '2')
		};
		config.routeCapture.scope = TestRouteCaptureScope();
		config.finalizationSeal = &SealFinalizationForTesting;
		return config;
	}

	void RecordRouteEvidence(CatalogDB& a_catalog)
	{
		auto admission =
			a_catalog.BeginRouteCreate(TestRouteCreateInput(true));
		Check(
			static_cast<bool>(admission),
			"route create admission failed");
		auto created = a_catalog.CompleteRouteCreate(
			std::move(admission), TestRouteCreateOutcome());
		Check(
			created.enqueued && created.record,
			"route create was not queued");
		{
			std::scoped_lock lock(created.record->mutex);
			created.record->bindReserved = true;
		}
		auto bind = a_catalog.BeginRouteBind();
		Check(static_cast<bool>(bind), "route bind admission failed");
		Check(
			a_catalog.RecordRouteBind(
				std::move(bind),
				created.record,
				RouteBindSnapshot{
					"BSDFLightShader",
					"ps",
					0x01200202,
					std::nullopt
				}),
			"route bind was not recorded");
	}

	std::map<std::filesystem::path, std::string> ReadRouteDocuments(
		const std::filesystem::path& a_root)
	{
		std::map<std::filesystem::path, std::string> documents;
		for (const auto* folder : { "observations", "manifests" }) {
			const auto directory = a_root / folder;
			if (!std::filesystem::exists(directory))
				continue;
			for (const auto& entry :
					std::filesystem::directory_iterator(directory)) {
				documents.emplace(
					entry.path(), ReadTextFile(entry.path()));
			}
		}
		return documents;
	}

	std::string ReadRouteManifest(const std::filesystem::path& a_root)
	{
		const auto directory = a_root / "manifests";
		Check(
			std::filesystem::exists(directory),
			"route manifest was not published");
		for (const auto& entry :
				std::filesystem::directory_iterator(directory))
			return ReadTextFile(entry.path());
		throw Failure("route manifest directory is empty");
	}

	std::string ReadRunManifest(
		const std::filesystem::path& a_artifacts,
		const std::string& a_runId)
	{
		return ReadTextFile(
			a_artifacts / "runs" / a_runId / "manifest.v1.json");
	}

	bool ReadCanonicalJsonBool(
		std::string_view a_json,
		std::string_view a_key,
		std::size_t a_from = 0)
	{
		const std::string needle =
			"\"" + std::string(a_key) + "\":";
		const auto at = a_json.find(needle, a_from);
		Check(
			at != std::string_view::npos,
			"canonical JSON key is missing: " + std::string(a_key));
		const auto value = a_json.substr(at + needle.size());
		if (value.starts_with("true"))
			return true;
		Check(
			value.starts_with("false"),
			"canonical JSON key is not a boolean: " + std::string(a_key));
		return false;
	}

	// The two route-local authority facts a published capture run states about one row.
	struct RouteAuthorityFacts
	{
		bool row = false;
		bool routeManifest = false;

		// Route membership authority; a false manifest never promotes a true row.
		[[nodiscard]] bool MembershipAuthoritative() const noexcept
		{
			return routeManifest && row;
		}
	};

	RouteAuthorityFacts ReadRouteAuthorityFacts(
		const std::filesystem::path& a_root)
	{
		const auto directory = a_root / "observations";
		Check(
			std::filesystem::exists(directory),
			"route observation was not published");
		std::optional<bool> row;
		for (const auto& entry :
				std::filesystem::directory_iterator(directory)) {
			Check(!row, "route capture published more than one observation");
			row = ReadCanonicalJsonBool(
				ReadTextFile(entry.path()), "capture_authoritative");
		}
		Check(row.has_value(), "route observation directory is empty");
		const auto manifest = ReadRouteManifest(a_root);
		const auto observations = manifest.find("\"observations\":[{");
		Check(
			observations != std::string::npos,
			"route manifest published no observation row");
		Check(
			ReadCanonicalJsonBool(
				manifest, "capture_authoritative", observations)
				== *row,
			"route manifest row disagreed with its published observation");
		return {
			*row,
			ReadCanonicalJsonBool(manifest, "capture_authoritative")
		};
	}

	// Returns the manifest's leading authority_reasons array text for exact-set comparison.
	std::string ReadRouteManifestReasons(std::string_view a_manifest)
	{
		constexpr std::string_view prefix = "{\"authority_reasons\":";
		constexpr std::string_view suffix = ",\"capture_authoritative\":";
		Check(
			a_manifest.starts_with(prefix),
			"route manifest lost its leading authority reasons");
		const auto end = a_manifest.find(suffix, prefix.size());
		Check(
			end != std::string_view::npos,
			"route manifest lost its capture authority key");
		return std::string(
			a_manifest.substr(prefix.size(), end - prefix.size()));
	}

	std::string FinalizeSeededRoute(
		const std::filesystem::path& a_root,
		const RouteFinalizationVeto& a_veto,
		bool a_breakStockOnly = false)
	{
		std::filesystem::create_directories(a_root);
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		auto publisher = SeedRoutePublisher(a_root);
		// Only a registry change after the open snapshot derives a stock-only reason.
		if (a_breakStockOnly)
			g_routeRegistrySnapshot = {
				true, 1, false, std::string(64, 'b')
			};
		FrozenRouteSnapshot snapshot;
		auto error = RoutePublicationError::kNone;
		Check(
			publisher->CloseCaptureAdmissionAndFreeze(snapshot, g_routeHooksReady, error)
				&& snapshot.records.size() == 1,
			"seeded route capture did not freeze");
		Check(
			publisher->PublishObservation(snapshot.records.front())
				.success,
			"seeded route observation did not publish");
		const auto manifest = publisher->FinalizeRun(a_veto);
		Check(manifest.success, "seeded route manifest did not publish");
		return ReadTextFile(manifest.document.path);
	}

	void TestRouteFinalizeRunVeto()
	{
		TempTree tree("route-veto");
		RouteCaptureSeamScope seams;
		const auto cleanControl = FinalizeSeededRoute(
			tree.path / "clean-control", {});
		const auto cleanVetoed = FinalizeSeededRoute(
			tree.path / "clean-vetoed", { .producerDeclined = true });
		Check(
			ReadRouteManifestReasons(cleanControl) == "[]"
				&& ReadCanonicalJsonBool(
					cleanControl, "capture_authoritative"),
			"the unvetoed clean control run was not authoritative");
		Check(
			ReadRouteManifestReasons(cleanVetoed)
					== "[\"producer-declined\"]"
				&& !ReadCanonicalJsonBool(
					cleanVetoed, "capture_authoritative"),
			"an external veto without a derived reason left the closed fallback");

		const auto derivedControl = FinalizeSeededRoute(
			tree.path / "derived-control", {}, true);
		const auto derivedVetoed = FinalizeSeededRoute(
			tree.path / "derived-vetoed",
			{ .producerDeclined = true },
			true);
		const auto derivedReasons =
			ReadRouteManifestReasons(derivedControl);
		Check(
			derivedReasons != "[]"
				&& derivedReasons.find("stock-only-violation")
					!= std::string::npos,
			"the derived-reason control run lost its fail-closed reason");
		Check(
			ReadRouteManifestReasons(derivedVetoed) == derivedReasons
				&& !ReadCanonicalJsonBool(
					derivedVetoed, "capture_authoritative"),
			"an external veto changed the derived reason set");
		Check(
			derivedVetoed.find("producer-declined") == std::string::npos
				&& derivedVetoed.find("finalization-timeout")
					== std::string::npos,
			"an external veto co-emitted a reason outside the derived set");
	}

	void TestRouteCaptureCoordinatorDeadline()
	{
		using namespace std::chrono_literals;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		std::atomic<unsigned> closes{ 0 };
		Check(
			!coordinator.ArmCaptureDeadline(30s),
			"capture timing armed before hook readiness began a run");
		Check(
			coordinator.Begin([&] {
				closes.fetch_add(1, std::memory_order_relaxed);
				return true;
			}),
			"capture coordinator did not begin");
		Check(
			!coordinator.Begin([] { return true; }),
			"a second close action replaced a live coordinator worker");
		auto status = coordinator.Snapshot();
		Check(
			status.state == route_capture::State::kCapturing
				&& !status.deadlineArmed,
			"capture began with an armed deadline");
		time.Advance(2h);
		Check(
			coordinator.Snapshot().state
					== route_capture::State::kCapturing
				&& closes.load(std::memory_order_relaxed) == 0,
			"unarmed capture closed on elapsed time alone");
		Check(
			coordinator.ArmCaptureDeadline(90s),
			"capture deadline was not armed after readiness");
		Check(
			!coordinator.ArmCaptureDeadline(90s),
			"capture deadline was armed twice");
		Check(
			coordinator.Snapshot().remainingCaptureSeconds == 90,
			"armed capture reported the wrong remaining window");
		time.Advance(89s);
		Check(
			coordinator.Snapshot().state
				== route_capture::State::kCapturing,
			"capture closed before its deadline");
		time.Advance(2s);
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"capture deadline did not finalize");
		status = coordinator.Snapshot();
		Check(
			status.state == route_capture::State::kFinalizedInert
				&& status.reason
					== route_capture::CloseReason::kCaptureDeadline
				&& status.authoritative
				&& !status.finalizationTimedOut
				&& closes.load(std::memory_order_relaxed) == 1,
			"capture deadline did not reach an authoritative inert state");
		coordinator.RequestClose(
			route_capture::CloseReason::kProcessExit);
		status = coordinator.Snapshot();
		Check(
			status.state == route_capture::State::kFinalizedInert
				&& status.reason
					== route_capture::CloseReason::kCaptureDeadline
				&& closes.load(std::memory_order_relaxed) == 1,
			"a late request reopened terminal capture state");
		Check(
			!coordinator.Begin([] { return true; })
				&& !coordinator.ArmCaptureDeadline(30s),
			"terminal capture state accepted a new run");
	}

	void TestRouteCaptureCoordinatorCoalescing()
	{
		using namespace std::chrono_literals;
		std::promise<void> release;
		const std::shared_future<void> gate =
			release.get_future().share();
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		// Declared after the coordinator so it releases the worker before the join.
		struct ReleaseGuard
		{
			std::promise<void>& promise;
			std::atomic<bool> fired{ false };
			void Fire() noexcept
			{
				if (!fired.exchange(true, std::memory_order_acq_rel)) {
					try {
						promise.set_value();
					} catch (...) {
					}
				}
			}
			~ReleaseGuard() { Fire(); }
		} releaseGuard{ release };
		std::atomic<unsigned> closes{ 0 };
		std::atomic<unsigned long> actionThread{ 0 };
		const auto requesterThread = GetCurrentThreadId();
		Check(
			coordinator.Begin([&, gate] {
				actionThread.store(
					GetCurrentThreadId(), std::memory_order_release);
				closes.fetch_add(1, std::memory_order_relaxed);
				coordinator.RequestClose(
					route_capture::CloseReason::kUserRequest);
				gate.wait();
				return true;
			}),
			"coalescing coordinator did not begin");
		Check(
			coordinator.ArmCaptureDeadline(30s),
			"coalescing deadline was not armed");

		std::vector<std::jthread> requesters;
		for (unsigned index = 0; index < 8; ++index) {
			requesters.emplace_back([&coordinator, index] {
				coordinator.RequestClose(
					index % 2 == 0
						? route_capture::CloseReason::kUserRequest
						: route_capture::CloseReason::kProcessExit);
			});
		}
		time.Advance(31s);
		for (auto& requester : requesters)
			requester.join();

		time.Advance(route_capture::kFinalizationBudget + 1s);
		auto bounded = std::async(std::launch::async, [&] {
			return coordinator.WaitForTerminal(
				route_capture::kFinalizationBudget);
		});
		while (bounded.wait_for(0s) != std::future_status::ready) {
			std::this_thread::yield();
			time.Advance(route_capture::kFinalizationBudget);
		}
		Check(
			!bounded.get(),
			"the exit fallback wait was not bounded");
		Check(
			coordinator.Snapshot().finalizationTimedOut,
			"an overdue open decision was not observable while closing");
		releaseGuard.Fire();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"close did not finalize after the close action returned");
		const auto status = coordinator.Snapshot();
		Check(
			status.state == route_capture::State::kFinalizedInert
				&& !status.finalizationTimedOut
				&& closes.load(std::memory_order_relaxed) == 1,
			"concurrent and reentrant requests did not coalesce into one close");
		Check(
			actionThread.load(std::memory_order_acquire) != 0
				&& actionThread.load(std::memory_order_acquire)
					!= requesterThread,
			"the close action ran on a requester thread");
		Check(
			coordinator.SealFinalizationDecision(),
			"a later seal did not latch an overdue open decision");
		Check(
			coordinator.Snapshot().finalizationTimedOut
				&& coordinator.TelemetrySnapshot().finalizationTimedOut,
			"a latched decision was not durable or published");
	}

	void TestRouteCaptureCoordinatorCutoff()
	{
		TempTree tree("route-cutoff");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"cutoff run did not start");

		ManualTime time;
		route_capture::Coordinator coordinator(time);
		AdmissionClosedProbe probe;
		auto lease = catalog.TryAcquireProducerLease();
		Check(
			static_cast<bool>(lease),
			"cutoff run did not lease a producer");

		std::atomic<unsigned> closes{ 0 };
		Check(
			coordinator.Begin([&] {
				closes.fetch_add(1, std::memory_order_relaxed);
				return catalog.Stop();
			}),
			"cutoff coordinator did not begin");
		coordinator.RequestClose(
			route_capture::CloseReason::kUserRequest);
		probe.Wait();
		Check(
			!static_cast<bool>(catalog.TryAcquireProducerLease()),
			"a closed producer cutoff still admitted a lease");
		Check(
			coordinator.Snapshot().state
				== route_capture::State::kClosing,
			"close completed while a producer callback was in flight");
		lease.Reset();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"close did not finalize after the in-flight callback returned");
		Check(
			coordinator.Snapshot().authoritative
				&& closes.load(std::memory_order_relaxed) == 1,
			"cutoff finalization lost authority");
	}

	// The shipped default: route capture off, the coordinator still owns the sole close.
	void TestOrdinaryRunCoordinatorClose()
	{
		TempTree tree("ordinary-coordinator-close");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		auto& catalog = CatalogDB::Get();
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		AdmissionClosedProbe probe;
		auto config = TestConfig(database, artifacts);
		config.finalizationSeal = &SealFinalizationForTesting;
		Check(
			StartReady(catalog, config),
			"ordinary coordinator run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		Check(
			!catalog.GetStats().routeCaptureRequested
				&& !catalog.GetStats().routeCaptureActive,
			"an ordinary run requested route capture");
		auto lease = catalog.TryAcquireProducerLease();
		Check(
			static_cast<bool>(lease),
			"ordinary coordinator run did not lease a producer");

		std::atomic<unsigned> closes{ 0 };
		std::atomic<unsigned long> closeThread{ 0 };
		const auto requesterThread = GetCurrentThreadId();
		Check(
			coordinator.Begin([&] {
				closeThread.store(
					GetCurrentThreadId(), std::memory_order_release);
				closes.fetch_add(1, std::memory_order_relaxed);
				return catalog.Stop();
			}),
			"ordinary coordinator did not begin");
		coordinator.RequestClose(
			route_capture::CloseReason::kProcessExit);
		probe.Wait();
		// The requester already returned while Stop is still draining, so nothing ran inline.
		Check(
			coordinator.Snapshot().state
					== route_capture::State::kClosing
				&& !coordinator.Snapshot().deadlineArmed
				&& !static_cast<bool>(catalog.TryAcquireProducerLease()),
			"a process-exit request finalized on the requester thread");
		// Admission is shut but Stop is still draining: rejection must precede accounting.
		const auto racedBefore = catalog.GetStats();
		SubmitRejectedProducerEntries(catalog, 1);
		const auto racedAfter = catalog.GetStats();
		Check(
			QualityTuple(racedAfter.quality)
					== QualityTuple(racedBefore.quality)
				&& StatTuple(racedAfter) == StatTuple(racedBefore),
			"a cutoff-race entry mutated quality outside the producer lease");
		lease.Reset();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"ordinary coordinator close did not finalize");
		const auto status = coordinator.Snapshot();
		Check(
			status.state == route_capture::State::kFinalizedInert
				&& status.reason
					== route_capture::CloseReason::kProcessExit
				&& status.authoritative
				&& !status.finalizationTimedOut
				&& !status.deadlineArmed
				&& status.remainingCaptureSeconds == 0
				&& closes.load(std::memory_order_relaxed) == 1,
			"an ordinary route-disabled close lost its finalized authority");
		Check(
			closeThread.load(std::memory_order_acquire) != 0
				&& closeThread.load(std::memory_order_acquire)
					!= requesterThread,
			"the sole Stop ran on the requesting thread");
		coordinator.RequestClose(
			route_capture::CloseReason::kProcessExit);
		Check(
			closes.load(std::memory_order_relaxed) == 1
				&& coordinator.Snapshot().state
					== route_capture::State::kFinalizedInert,
			"a repeated process-exit request ran a second Stop");
		Check(
			SqlInt(
				database,
				("SELECT COUNT(*) FROM catalog_runs WHERE "
				 "generated_run_id='" + runId + "' AND "
				 "lifecycle='running' AND authoritative=0 "
				 "AND manifest_published=0 AND publication_pending=1")
					.c_str())
				== 1,
			"the finalized run did not record its verified publication intent");
		Check(
			ReadRunManifest(artifacts, runId)
					.find("\"authoritative\":true")
				!= std::string::npos,
			"the ordinary run manifest lost catalog authority");
		Check(
			SqlInt(
				database,
				"SELECT lifecycle_failure FROM catalog_run_quality")
				== 0,
			"a clean ordinary close persisted a lifecycle failure");
		Check(
			std::filesystem::is_empty(routeRoot)
				&& !std::filesystem::exists(artifacts / "observations")
				&& !std::filesystem::exists(artifacts / "manifests"),
			"a route-disabled run published route artifacts");
	}

	// Samples startup state at the commit point, with the insert transaction still open.
	std::atomic<std::uint64_t> g_commitPointWriterEntries{ 0 };
	std::atomic<bool> g_commitPointReached{ false };
	std::atomic<bool> g_commitPointLifecycleInactive{ false };
	std::atomic<bool> g_commitPointAdmissionClosed{ false };
	std::atomic<bool> g_commitPointIdentityReady{ false };
	std::atomic<bool> g_commitPointRunIdHidden{ false };
	std::atomic<bool> g_commitPointTupleClean{ false };
	std::atomic<bool> g_commitPointRouteAdmissionRejected{ false };

	// Every identity and service field GetStats exposes, compared as one tuple.
	std::tuple<
		std::string, std::optional<std::string>, std::optional<std::string>,
		std::string, bool, bool, bool, bool, bool, bool, bool, bool>
		IdentityTuple(const CatalogDB::Stats& a_stats)
	{
		return {
			a_stats.generatedRunId,
			a_stats.externalRunId,
			a_stats.scenarioId,
			a_stats.lifecycle,
			a_stats.authoritative,
			a_stats.rawExportRequested,
			a_stats.rawExportComplete,
			a_stats.writerDrained,
			a_stats.hookCoverageReady,
			a_stats.orderlyFinalizerReady,
			a_stats.routeCaptureRequested,
			a_stats.routeCaptureActive
		};
	}

	// The lifecycle-owned subset published under the identity lock.
	std::tuple<
		std::string, std::optional<std::string>, std::optional<std::string>,
		std::string, std::uint64_t, bool, bool, bool>
		PublishedTuple(const CatalogDB::Stats& a_stats)
	{
		return {
			a_stats.generatedRunId,
			a_stats.externalRunId,
			a_stats.scenarioId,
			a_stats.lifecycle,
			a_stats.generation,
			a_stats.authoritative,
			a_stats.routeCaptureRequested,
			a_stats.routeCaptureActive
		};
	}

	// Legal-state invariants every published tuple must satisfy in any phase.
	bool IdentityTupleIsLegal(const CatalogDB::Stats& a_stats)
	{
		if (a_stats.authoritative && a_stats.lifecycle != "finalized")
			return false;
		if (a_stats.routeCaptureActive
			&& (a_stats.lifecycle != "running"
				|| !a_stats.routeCaptureRequested
				|| a_stats.generation == 0))
			return false;
		if (a_stats.lifecycle == "inactive") {
			return a_stats.generatedRunId.empty()
				&& !a_stats.externalRunId
				&& !a_stats.scenarioId
				&& a_stats.generation == 0
				&& !a_stats.authoritative
				&& !a_stats.routeCaptureRequested
				&& !a_stats.routeCaptureActive;
		}
		return !a_stats.generatedRunId.empty() && a_stats.generation != 0;
	}

	void SampleStartupCommitPointForTesting() noexcept
	{
		try {
			const auto stats = CatalogDB::Get().GetStats();
			g_commitPointWriterEntries.store(
				CatalogDB::WriterRunEntriesForTesting(),
				std::memory_order_release);
			g_commitPointLifecycleInactive.store(
				stats.lifecycle == "inactive", std::memory_order_release);
			// A scoped lease balances any unexpected admission instead of hanging Stop.
			auto lease = CatalogDB::Get().TryAcquireProducerLease();
			g_commitPointAdmissionClosed.store(
				!lease, std::memory_order_release);
			g_commitPointIdentityReady.store(
				!CatalogDB::Get().AttemptedRunIdForTesting().empty(),
				std::memory_order_release);
			g_commitPointRunIdHidden.store(
				stats.generatedRunId.empty(), std::memory_order_release);
			// The lifecycle-owned tuple must read pristine inactive before the commit.
			g_commitPointTupleClean.store(
				PublishedTuple(stats) == PublishedTuple(CatalogDB::Stats{}),
				std::memory_order_release);
			{
				// Move-only tokens released in this scope if anything unexpectedly admits.
				auto create = CatalogDB::Get().BeginRouteCreate(
					TestRouteCreateInput(true));
				auto bind = CatalogDB::Get().BeginRouteBind();
				g_commitPointRouteAdmissionRejected.store(
					!create && !bind, std::memory_order_release);
			}
			g_commitPointReached.store(true, std::memory_order_release);
		} catch (...) {
		}
	}

	// Clears the commit-point seam even when a Check throws.
	class StartupCommitPointProbe
	{
	public:
		StartupCommitPointProbe()
		{
			g_commitPointReached.store(false, std::memory_order_release);
			CatalogDB::SetBeforeRunCommitCallbackForTesting(
				&SampleStartupCommitPointForTesting);
		}

		~StartupCommitPointProbe()
		{
			CatalogDB::SetBeforeRunCommitCallbackForTesting(nullptr);
		}

		StartupCommitPointProbe(const StartupCommitPointProbe&) = delete;
		StartupCommitPointProbe& operator=(
			const StartupCommitPointProbe&) = delete;
	};

	// Holds Start inside its lifecycle-owning commit point while a rival Start blocks.
	std::promise<void> g_lifecycleRelease;
	std::shared_future<void> g_lifecycleGate;
	std::promise<void> g_lifecycleAtCommit;
	std::promise<void> g_lifecycleRivalAnnounced;
	std::shared_future<void> g_lifecycleRivalReady;
	std::atomic<bool> g_lifecycleRivalCompleted{ false };
	std::atomic<bool> g_lifecycleRivalRacedIn{ false };
	std::atomic<bool> g_lifecycleRivalAnnouncedOnce{ false };

	void HoldStartupCommitPointForTesting() noexcept
	{
		try {
			g_lifecycleAtCommit.set_value();
			g_lifecycleRivalReady.wait();
			// The rival announced; it must not finish Start while this one owns lifecycle.
			for (unsigned spin = 0; spin < 20000; ++spin) {
				if (g_lifecycleRivalCompleted.load(
						std::memory_order_acquire)) {
					g_lifecycleRivalRacedIn.store(
						true, std::memory_order_release);
					break;
				}
				std::this_thread::yield();
			}
			g_lifecycleGate.wait();
		} catch (...) {
		}
	}

	// Restores the lifecycle seam and releases the held Start even when a Check throws.
	class LifecycleHoldProbe
	{
	public:
		LifecycleHoldProbe()
		{
			g_lifecycleRelease = {};
			g_lifecycleAtCommit = {};
			g_lifecycleRivalAnnounced = {};
			g_lifecycleGate = g_lifecycleRelease.get_future().share();
			g_lifecycleRivalReady =
				g_lifecycleRivalAnnounced.get_future().share();
			g_lifecycleRivalCompleted.store(false, std::memory_order_release);
			g_lifecycleRivalRacedIn.store(false, std::memory_order_release);
			g_lifecycleRivalAnnouncedOnce.store(
				false, std::memory_order_release);
			CatalogDB::SetBeforeRunCommitCallbackForTesting(
				&HoldStartupCommitPointForTesting);
		}

		~LifecycleHoldProbe()
		{
			CatalogDB::SetBeforeRunCommitCallbackForTesting(nullptr);
			Release();
		}

		LifecycleHoldProbe(const LifecycleHoldProbe&) = delete;
		LifecycleHoldProbe& operator=(const LifecycleHoldProbe&) = delete;

		void Release() noexcept
		{
			if (!_released.exchange(true, std::memory_order_acq_rel)) {
				// Satisfies both gates so a held callback can never block on either.
				AnnounceRival();
				try {
					g_lifecycleRelease.set_value();
				} catch (...) {
				}
			}
		}

		// Idempotent announcement so rival lambdas never double-set the promise.
		static void AnnounceRival() noexcept
		{
			if (g_lifecycleRivalAnnouncedOnce.exchange(
					true, std::memory_order_acq_rel))
				return;
			try {
				g_lifecycleRivalAnnounced.set_value();
			} catch (...) {
			}
		}

	private:
		std::atomic<bool> _released{ false };
	};

	// One lifecycle owner: a rival Start cannot interleave into shared startup state.
	void TestLifecycleSerialization()
	{
		TempTree tree("lifecycle-serialization");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		LifecycleHoldProbe probe;
		const auto writerBaseline = CatalogDB::WriterRunEntriesForTesting();
		const auto config = TestConfig(database, artifacts);

		auto owner = std::async(std::launch::async, [&] {
			return catalog.Start(config, TestIdentity());
		});
		// Declared straight after the owner future so any later throw unblocks the hold.
		struct HoldRelease
		{
			LifecycleHoldProbe& held;
			~HoldRelease() { held.Release(); }
		} holdRelease{ probe };
		g_lifecycleAtCommit.get_future().wait();

		std::atomic<bool> rivalStarted{ false };
		auto rival = std::async(std::launch::async, [&] {
			LifecycleHoldProbe::AnnounceRival();
			const bool result = catalog.Start(config, TestIdentity());
			rivalStarted.store(result, std::memory_order_release);
			g_lifecycleRivalCompleted.store(true, std::memory_order_release);
			return result;
		});

		probe.Release();
		Check(owner.get(), "the lifecycle owner's start did not succeed");
		Check(
			!rival.get() && !rivalStarted.load(std::memory_order_acquire),
			"a rival start ran while another owned the lifecycle");
		Check(
			!g_lifecycleRivalRacedIn.load(std::memory_order_acquire),
			"a rival start completed while another owned the lifecycle");

		catalog.MarkHookCoverageReady();
		const auto stats = catalog.GetStats();
		Check(
			stats.lifecycle == "running" && !stats.generatedRunId.empty(),
			"the surviving run was disturbed by the rival start");
		Check(
			SqlInt(database, "SELECT COUNT(*) FROM catalog_runs") == 1
				&& SqlText(
					database, "SELECT generated_run_id FROM catalog_runs")
					== stats.generatedRunId,
			"a rival start added a second run row");
		Check(catalog.Stop(), "the serialized run did not finalize");
		Check(
			CatalogDB::WriterRunEntriesForTesting() - writerBaseline == 1,
			"the serialized run released more than one writer");
	}

	// GetStats must stay coherent while startup aborts and restarts churn identity.
	void TestConcurrentStatsDuringLifecycleChurn()
	{
		TempTree tree("stats-churn");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		std::atomic<bool> stopReader{ false };
		std::atomic<std::uint64_t> samples{ 0 };
		std::atomic<bool> violated{ false };
		// Stops and joins the reader even when a Check throws.
		struct ReaderGuard
		{
			std::atomic<bool>& stop;
			std::thread worker;
			~ReaderGuard()
			{
				stop.store(true, std::memory_order_release);
				if (worker.joinable())
					worker.join();
			}
		} reader{ stopReader, std::thread([&] {
			while (!stopReader.load(std::memory_order_acquire)) {
				const auto stats = catalog.GetStats();
				const bool active = catalog.RouteCaptureActive();
				const bool impossible =
					!IdentityTupleIsLegal(stats)
					|| (active && !stats.routeCaptureRequested);
				if (impossible)
					violated.store(true, std::memory_order_release);
				samples.fetch_add(1, std::memory_order_relaxed);
			}
		}) };

		auto aborting = TestConfig(database, artifacts);
		aborting.failRunCommitForTesting = true;
		for (unsigned cycle = 0; cycle < 24; ++cycle) {
			Check(
				!catalog.Start(aborting, TestIdentity()),
				"a churn abort cycle reported success");
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"a churn valid cycle did not start");
			Check(catalog.Stop(), "a churn valid cycle did not finalize");
		}
		stopReader.store(true, std::memory_order_release);
		if (reader.worker.joinable())
			reader.worker.join();
		Check(
			samples.load(std::memory_order_relaxed) > 0,
			"the concurrent stats reader never sampled");
		Check(
			!violated.load(std::memory_order_acquire),
			"GetStats exposed an impossible lifecycle or capture tuple");
	}

	// Stop cannot interleave into a Start that owns the lifecycle boundary.
	void TestLifecycleStartStopInterleaving()
	{
		auto& catalog = CatalogDB::Get();
		{
			TempTree tree("lifecycle-stop-blocks");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			LifecycleHoldProbe probe;
			const auto writerBaseline =
				CatalogDB::WriterRunEntriesForTesting();
			const auto config = TestConfig(database, artifacts);

			auto owner = std::async(std::launch::async, [&] {
				return catalog.Start(config, TestIdentity());
			});
			// Declared straight after the owner future so any later throw unblocks it.
			struct HoldRelease
			{
				LifecycleHoldProbe& held;
				~HoldRelease() { held.Release(); }
			} holdRelease{ probe };
			g_lifecycleAtCommit.get_future().wait();
			catalog.MarkHookCoverageReady();

			auto stopper = std::async(std::launch::async, [&] {
				LifecycleHoldProbe::AnnounceRival();
				const bool result = catalog.Stop();
				g_lifecycleRivalCompleted.store(
					true, std::memory_order_release);
				return result;
			});

			probe.Release();
			Check(owner.get(), "the lifecycle owner's start did not succeed");
			Check(
				!g_lifecycleRivalRacedIn.load(std::memory_order_acquire),
				"a stop completed while a start owned the lifecycle");
			Check(
				stopper.get(),
				"the serialized stop did not finalize the started run");
			Check(
				!catalog.Stop() && !catalog.Stop(),
				"the serialized run closed more than once");
			Check(
				CatalogDB::WriterRunEntriesForTesting() - writerBaseline
					== 1,
				"the serialized run released more than one writer");
			Check(
				SqlInt(database, "SELECT COUNT(*) FROM catalog_runs") == 1,
				"an interleaved start and stop produced more than one run");
			Check(
				CatalogDB::ActiveTransientStatementsForTesting() == 0,
				"an interleaved start and stop leaked a transient statement");
		}
		{
			// An aborted start leaves nothing for a later stop to close or publish.
			TempTree tree("lifecycle-stop-after-abort");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			std::filesystem::create_directory(artifacts);
			auto config = TestConfig(database, artifacts);
			config.failRunCommitForTesting = true;
			Check(
				!catalog.Start(config, TestIdentity()),
				"an aborted start reported success");
			Check(
				!catalog.Stop() && !catalog.Stop(),
				"an aborted start left a closable database");
			Check(
				!std::filesystem::exists(artifacts / "runs"),
				"an aborted start published a manifest");
			Check(
				SqlInt(database, "SELECT COUNT(*) FROM catalog_runs") == 0,
				"an aborted start left a run row for a later stop");
			Check(
				CatalogDB::ActiveTransientStatementsForTesting() == 0
					&& CatalogDB::LastStartupCloseResultForTesting()
						== SQLITE_OK,
				"an aborted start leaked a statement or closed busy");
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"a later start was blocked by the aborted attempt");
			Check(catalog.Stop(), "the later start did not finalize");
		}
	}

	// Readiness that arrives after the close cutoff can never promote authority.
	// Graphics identity freezes with readiness; post-cutoff facts cannot apply.
	void TestLateGraphicsCannotApply()
	{
		TempTree tree("late-graphics");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		AdmissionClosedProbe probe;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"late graphics run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		catalog.SetGraphicsFacts("early-adapter", "early-level");
		auto lease = catalog.TryAcquireProducerLease();
		Check(
			static_cast<bool>(lease),
			"late graphics run did not lease a producer");

		Check(
			coordinator.Begin([&] { return catalog.Stop(); }),
			"late graphics coordinator did not begin");
		coordinator.RequestClose(
			route_capture::CloseReason::kUserRequest);
		probe.Wait();

		// Graphics facts change after the cutoff, while Stop is drain-blocked.
		catalog.SetGraphicsFacts("late-adapter", "late-level");
		lease.Reset();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"late graphics close did not finalize");

		const auto runScope = " WHERE generated_run_id='" + runId + "'";
		const auto runManifest = ReadRunManifest(artifacts, runId);
		Check(
			runManifest.find("\"graphics_adapter\":\"early-adapter\"")
					!= std::string::npos
				&& runManifest.find(
					   "\"graphics_feature_level\":\"early-level\"")
					!= std::string::npos,
			"the run manifest used live graphics instead of the cutoff snapshot");
		// The frozen runtime identity must match the row the run committed with.
		Check(
			runManifest.find(
				"\"runtime_family\":\""
				+ SqlText(
					database,
					("SELECT runtime_family FROM catalog_runs" + runScope)
						.c_str())
				+ "\"") != std::string::npos,
			"the run manifest and run row disagreed on frozen identity");
		Check(
			runManifest.find("late-adapter") == std::string::npos
				&& runManifest.find("late-level") == std::string::npos,
			"the run manifest leaked post-cutoff graphics facts");
		Check(
			SqlInt(
				database,
				("SELECT COUNT(*) FROM catalog_runs" + runScope
				 + " AND graphics_adapter='early-adapter' "
				   "AND graphics_feature_level='early-level'")
					.c_str())
				== 1,
			"the run row recorded post-cutoff graphics facts");
	}

	void TestLateReadinessCannotPromote()
	{		TempTree tree("late-readiness");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		g_routeHooksReady = false;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		AdmissionClosedProbe probe;
		auto& catalog = CatalogDB::Get();
		auto config = RouteCaptureRunConfig(database, artifacts, routeRoot);
		config.orderlyFinalizerReadyForTesting = false;
		Check(
			catalog.Start(config, TestIdentity()),
			"late readiness run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);
		auto lease = catalog.TryAcquireProducerLease();
		Check(
			static_cast<bool>(lease),
			"late readiness run did not lease a producer");

		Check(
			coordinator.Begin([&] { return catalog.Stop(); }),
			"late readiness coordinator did not begin");
		coordinator.RequestClose(
			route_capture::CloseReason::kUserRequest);
		probe.Wait();

		// Every readiness input flips true after the cutoff, while Stop is drain-blocked.
		Check(
			!g_routeHooksReady && !catalog.GetStats().hookCoverageReady
				&& !catalog.GetStats().orderlyFinalizerReady,
			"the late readiness run did not start from false readiness");
		g_routeHooksReady = true;
		catalog.MarkHookCoverageReady();
		Check(
			catalog.MarkOrderlyFinalizerReady(),
			"late orderly finalizer readiness did not persist");
		Check(
			catalog.GetStats().hookCoverageReady
				&& catalog.GetStats().orderlyFinalizerReady,
			"the late readiness flips did not take effect live");
		lease.Reset();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"late readiness close did not finalize");
		Check(
			!coordinator.Snapshot().authoritative,
			"late readiness promoted the run to authoritative");
		Check(
			!catalog.RoutePublisherPresentForTesting(),
			"a vetoed route publication retained the publisher");

		const auto routeManifest = ReadRouteManifest(routeRoot);
		Check(
			routeManifest.starts_with(
				"{\"authority_reasons\":[\"producer-declined\"],"
				"\"capture_authoritative\":false"),
			"the route manifest lost its cutoff hook-coverage veto");
		const auto runManifest = ReadRunManifest(artifacts, runId);
		Check(
			runManifest.find("\"authoritative\":false") != std::string::npos
				&& runManifest.find("\"hook_coverage_ready\":false")
					!= std::string::npos
				&& runManifest.find("\"orderly_finalizer_ready\":false")
					!= std::string::npos,
			"the run manifest used live readiness instead of the cutoff snapshot");
		const auto runScope = " WHERE generated_run_id='" + runId + "'";
		Check(
			SqlInt(
				database,
				("SELECT COUNT(*) FROM catalog_runs" + runScope
				 + " AND authoritative=0 AND hook_coverage_ready=0 "
				   "AND orderly_finalizer_ready=0")
					.c_str())
				== 1,
			"the run row recorded post-cutoff readiness");
		Check(
			SqlInt(
				database,
				("SELECT hook_observer_gap FROM catalog_run_quality"
				 + runScope)
					.c_str())
				>= 1,
			"the cutoff hook gap veto was dropped");
	}

	// The inverse: a route provider going false after cutoff cannot demote a true snapshot.
	void TestLateReadinessCannotDemote()
	{
		TempTree tree("late-readiness-demote");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		AdmissionClosedProbe probe;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"demotion run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);
		auto lease = catalog.TryAcquireProducerLease();
		Check(
			static_cast<bool>(lease),
			"demotion run did not lease a producer");

		Check(
			coordinator.Begin([&] { return catalog.Stop(); }),
			"demotion coordinator did not begin");
		coordinator.RequestClose(
			route_capture::CloseReason::kUserRequest);
		probe.Wait();
		// The route provider goes false after the cutoff captured true.
		g_routeHooksReady = false;
		lease.Reset();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"demotion close did not finalize");
		Check(
			coordinator.Snapshot().authoritative,
			"a post-cutoff provider change demoted a true snapshot");
		Check(
			ReadRouteManifest(routeRoot)
					.find("\"capture_authoritative\":true")
				!= std::string::npos,
			"the route manifest lost its cutoff hook-coverage snapshot");
		Check(
			ReadRunManifest(artifacts, runId)
					.find("\"authoritative\":true")
				!= std::string::npos,
			"the run manifest was demoted by a post-cutoff provider change");
	}

	// Admitted pre-cutoff work settles into the frozen quality; post-cutoff work cannot.
	void TestAdmittedPreCutoffUpdateSettlesBeforeDrain()
	{
		TempTree tree("cutoff-admitted");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		AdmissionClosedProbe probe;
		auto& catalog = CatalogDB::Get();
		auto config = TestConfig(database, artifacts);
		config.finalizationSeal = &SealFinalizationForTesting;
		Check(StartReady(catalog, config), "cutoff run did not start");
		const auto runId = catalog.GetStats().generatedRunId;

		// An admission opened before the cutoff holds Stop inside its producer drain.
		Check(
			catalog.TryBeginProducerAdmission(),
			"the pre-cutoff producer admission was refused");
		Check(
			coordinator.Begin([&] { return catalog.Stop(); }),
			"cutoff coordinator did not begin");
		coordinator.RequestClose(
			route_capture::CloseReason::kUserRequest);
		probe.Wait();

		// Included: admitted before the cutoff, settling while the drain waits.
		catalog.EnqueueObservationAdmitted(MakeMalformedOutcome(1));
		Sha1Result sha{};
		sha.bytes.fill(0x11);
		catalog.EnqueueAttributionAdmitted(
			sha,
			OversizedSubclassName().c_str(),
			0x01200202,
			AttributionKind::kCreationContext,
			AttributionObjectKind::kStock);
		// Excluded: no admission, arriving after the cutoff.
		SubmitRejectedProducerEntries(catalog, 10);

		catalog.EndProducerAdmission();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"cutoff close did not finalize");
		Check(
			!coordinator.Snapshot().authoritative,
			"admitted malformed evidence left the run authoritative");

		const auto runScope = " WHERE generated_run_id='" + runId + "'";
		Check(
			SqlInt(
				database,
				("SELECT malformed_bytecode FROM catalog_run_quality"
				 + runScope)
					.c_str())
				== 1,
			"the frozen quality lost or double-counted the admitted observation");
		Check(
			SqlInt(
				database,
				("SELECT metadata_truncated FROM catalog_run_quality"
				 + runScope)
					.c_str())
				== 1,
			"the frozen quality lost or double-counted the admitted attribution");
	}

	// Mutates the resolver registry after the publisher stored its close snapshot.
	CatalogDB::Stats g_routeFrozenStats{};
	std::atomic<unsigned> g_routeFrozenCalls{ 0 };

	void MutateRegistryAfterFreezeForTesting() noexcept
	{
		try {
			g_routeFrozenStats = CatalogDB::Get().GetStats();
			g_routeRegistrySnapshot = {
				true, 7, false, std::string(64, 'c')
			};
		} catch (...) {
		}
		g_routeFrozenCalls.fetch_add(1, std::memory_order_acq_rel);
	}

	// Clears the post-freeze seam even when a Check throws.
	class RouteFrozenProbe
	{
	public:
		RouteFrozenProbe()
		{
			g_routeFrozenStats = {};
			g_routeFrozenCalls.store(0, std::memory_order_release);
			CatalogDB::SetRouteCaptureFrozenCallbackForTesting(
				&MutateRegistryAfterFreezeForTesting);
		}

		~RouteFrozenProbe()
		{
			CatalogDB::SetRouteCaptureFrozenCallbackForTesting(nullptr);
		}

		RouteFrozenProbe(const RouteFrozenProbe&) = delete;
		RouteFrozenProbe& operator=(const RouteFrozenProbe&) = delete;
	};

	// A registry change after the freeze cannot alter finalized route bytes or authority.
	void TestRegistryFrozenAtRouteFreeze()
	{
		using namespace std::chrono_literals;
		TempTree tree("registry-frozen");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		RouteFrozenProbe frozenProbe;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"registry freeze run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		const auto liveGeneration = catalog.GetStats().generation;
		RecordRouteEvidence(catalog);

		Check(
			coordinator.Begin([&] { return catalog.Stop(); }),
			"registry freeze coordinator did not begin");
		Check(
			coordinator.ArmCaptureDeadline(90s),
			"registry freeze deadline was not armed");
		time.Advance(91s);
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"registry freeze close did not finalize");
		Check(
			g_routeRegistrySnapshot.generation == 7,
			"the post-freeze registry mutation did not run");
		// The route-frozen boundary already exposes a closing tuple with active false.
		Check(
			g_routeFrozenCalls.load(std::memory_order_acquire) == 1,
			"the route-frozen boundary did not run exactly once");
		Check(
			g_routeFrozenStats.lifecycle == "running"
				&& g_routeFrozenStats.generatedRunId == runId
				&& g_routeFrozenStats.generation == liveGeneration
				&& g_routeFrozenStats.routeCaptureRequested
				&& !g_routeFrozenStats.routeCaptureActive
				&& !g_routeFrozenStats.authoritative
				&& IdentityTupleIsLegal(g_routeFrozenStats),
			"the route-frozen tuple did not match the closing matrix");
		Check(
			coordinator.Snapshot().authoritative,
			"a post-freeze registry change demoted the run");
		const auto manifest = ReadRouteManifest(routeRoot);
		Check(
			manifest.starts_with(
				"{\"authority_reasons\":[],"
				"\"capture_authoritative\":true"),
			"the route manifest used a post-freeze registry snapshot");
		Check(
			manifest.find("\"resolver_registry_generation_close\":0")
					!= std::string::npos
				&& manifest.find("\"resolver_registry_empty_close\":true")
					!= std::string::npos,
			"the frozen registry close evidence was overwritten");
		Check(
			ReadRunManifest(artifacts, runId)
					.find("\"authoritative\":true")
				!= std::string::npos,
			"a post-freeze registry change demoted the catalog run");
	}

	// Records the sealed context and the artifact state at the publication barrier.
	std::filesystem::path g_sealedBarrierRouteRoot;
	std::filesystem::path g_sealedBarrierRunManifest;
	std::atomic<unsigned> g_sealedBarrierCalls{ 0 };
	std::atomic<bool> g_sealedBarrierRouteDocsPresent{ false };
	std::atomic<bool> g_sealedBarrierRunManifestPresent{ false };
	CatalogDB::SealedContextForTesting g_sealedBarrierContext{};

	void RecordSealedContextForTesting(
		const CatalogDB::SealedContextForTesting& a_context) noexcept
	{
		try {
			g_sealedBarrierContext = a_context;
			const bool routeDocs =
				std::filesystem::exists(
					g_sealedBarrierRouteRoot / "observations")
				|| std::filesystem::exists(
					g_sealedBarrierRouteRoot / "manifests");
			g_sealedBarrierRouteDocsPresent.store(
				routeDocs, std::memory_order_release);
			g_sealedBarrierRunManifestPresent.store(
				std::filesystem::exists(g_sealedBarrierRunManifest),
				std::memory_order_release);
		} catch (...) {
		}
		g_sealedBarrierCalls.fetch_add(1, std::memory_order_acq_rel);
	}

	// Clears the sealed-context seam even when a Check throws.
	class SealedContextProbe
	{
	public:
		SealedContextProbe(
			std::filesystem::path a_routeRoot,
			std::filesystem::path a_runManifest)
		{
			g_sealedBarrierRouteRoot = std::move(a_routeRoot);
			g_sealedBarrierRunManifest = std::move(a_runManifest);
			g_sealedBarrierCalls.store(0, std::memory_order_release);
			g_sealedBarrierRouteDocsPresent.store(
				false, std::memory_order_release);
			g_sealedBarrierRunManifestPresent.store(
				false, std::memory_order_release);
			g_sealedBarrierContext = {};
			CatalogDB::SetContextSealedCallbackForTesting(
				&RecordSealedContextForTesting);
		}

		~SealedContextProbe()
		{
			CatalogDB::SetContextSealedCallbackForTesting(nullptr);
			g_sealedBarrierRouteRoot.clear();
			g_sealedBarrierRunManifest.clear();
		}

		SealedContextProbe(const SealedContextProbe&) = delete;
		SealedContextProbe& operator=(const SealedContextProbe&) = delete;
	};

	// Asserts the barrier ran once with nothing published yet.
	void CheckSealedBarrierPublishedNothing(const char* a_what)
	{
		Check(
			g_sealedBarrierCalls.load(std::memory_order_acquire) == 1,
			std::string(a_what)
				+ ": the sealed context barrier did not run exactly once");
		Check(
			!g_sealedBarrierRouteDocsPresent.load(std::memory_order_acquire)
				&& !g_sealedBarrierRunManifestPresent.load(
					std::memory_order_acquire),
			std::string(a_what)
				+ ": artifacts existed before the sealed context barrier");
	}

	// The sealed context is complete and nothing is published before that barrier.
	void TestSealedContextPrecedesPublication()
	{
		using namespace std::chrono_literals;
		auto& catalog = CatalogDB::Get();
		{
			TempTree tree("sealed-barrier");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			const auto routeRoot = tree.path / "route";
			std::filesystem::create_directory(artifacts);
			std::filesystem::create_directory(routeRoot);
			RouteCaptureSeamScope seams;
			ManualTime time;
			route_capture::Coordinator coordinator(time);
			SealTargetScope seal(coordinator);
			Check(
				StartReady(
					catalog,
					RouteCaptureRunConfig(database, artifacts, routeRoot)),
				"sealed barrier run did not start");
			const auto runId = catalog.GetStats().generatedRunId;
			RecordRouteEvidence(catalog);
			SealedContextProbe barrier(
				routeRoot,
				artifacts / "runs" / runId / "manifest.v1.json");

			Check(
				coordinator.Begin([&] { return catalog.Stop(); }),
				"sealed barrier coordinator did not begin");
			Check(
				coordinator.ArmCaptureDeadline(90s),
				"sealed barrier deadline was not armed");
			time.Advance(91s);
			Check(
				coordinator.WaitForTerminal(kTestWaitBudget),
				"sealed barrier close did not finalize");

			CheckSealedBarrierPublishedNothing("clean run");
			// The context is already complete at the barrier.
			Check(
				g_sealedBarrierContext.hookCoverageReady
					&& g_sealedBarrierContext.orderlyFinalizerReady
					&& g_sealedBarrierContext.routeHookCoverageReady
					&& g_sealedBarrierContext.drained
					&& !g_sealedBarrierContext.finalizationTimedOut,
				"the sealed context was incomplete at the publication barrier");
			Check(
				g_sealedBarrierContext.lifecycleFailure == 0
					&& g_sealedBarrierContext.malformedBytecode == 0
					&& g_sealedBarrierContext.hookObserverGap == 0,
				"the sealed context carried unexpected frozen quality");

			// Publication completes only after the barrier.
			Check(
				ReadRouteDocuments(routeRoot).size() == 2,
				"route publication did not complete after the barrier");
			Check(
				ReadRouteManifest(routeRoot)
						.find("\"capture_authoritative\":true")
					!= std::string::npos,
				"the post-barrier route manifest lost authority");
			Check(
				ReadRunManifest(artifacts, runId)
						.find("\"authoritative\":true")
					!= std::string::npos,
				"the post-barrier catalog manifest lost authority");
			Check(
				coordinator.Snapshot().authoritative,
				"the sealed barrier run lost its authority");
		}
		{
			// A vetoed close must already carry its timeout delta at the barrier.
			TempTree tree("sealed-barrier-veto");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			const auto routeRoot = tree.path / "route";
			std::filesystem::create_directory(artifacts);
			std::filesystem::create_directory(routeRoot);
			RouteCaptureSeamScope seams;
			ManualTime time;
			route_capture::Coordinator coordinator(time);
			SealTargetScope seal(coordinator);
			Check(
				StartReady(
					catalog,
					RouteCaptureRunConfig(database, artifacts, routeRoot)),
				"vetoed barrier run did not start");
			const auto runId = catalog.GetStats().generatedRunId;
			RecordRouteEvidence(catalog);
			SealedContextProbe barrier(
				routeRoot,
				artifacts / "runs" / runId / "manifest.v1.json");

			Check(
				coordinator.Begin([&] {
					time.Advance(
						route_capture::kFinalizationBudget + 1s);
					return catalog.Stop();
				}),
				"vetoed barrier coordinator did not begin");
			coordinator.RequestClose(
				route_capture::CloseReason::kUserRequest);
			Check(
				coordinator.WaitForTerminal(kTestWaitBudget),
				"vetoed barrier close did not finalize");

			CheckSealedBarrierPublishedNothing("vetoed run");
			Check(
				g_sealedBarrierContext.finalizationTimedOut
					&& g_sealedBarrierContext.lifecycleFailure == 1,
				"the timeout delta was not applied to the sealed context");
			Check(
				ReadRouteManifest(routeRoot).starts_with(
					"{\"authority_reasons\":[\"producer-declined\"],"
					"\"capture_authoritative\":false"),
				"the vetoed route manifest did not publish after the barrier");
			Check(
				ReadRunManifest(artifacts, runId)
						.find("\"authoritative\":false")
					!= std::string::npos,
				"the vetoed catalog manifest claimed authority");
			Check(
				SqlInt(
					database,
					("SELECT lifecycle_failure FROM catalog_run_quality "
					 "WHERE generated_run_id='" + runId + "'")
						.c_str())
					== 1,
				"the persisted quality lost the sealed timeout delta");
		}
	}

	// Route admission shuts at the same cutoff as generic admission, before any wait.
	void TestRouteAdmissionClosesWithGenericCutoff()
	{
		TempTree tree("route-cutoff-together");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		AdmissionClosedProbe probe;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"route cutoff run did not start");
		const auto runId = catalog.GetStats().generatedRunId;

		// One complete pre-cutoff record, plus one left pending its bind.
		RecordRouteEvidence(catalog);
		auto pending = catalog.BeginRouteCreate(TestRouteCreateInput(true));
		Check(
			static_cast<bool>(pending),
			"the pre-cutoff pending create was refused");
		auto pendingRecord = catalog.CompleteRouteCreate(
			std::move(pending), TestRouteCreateOutcome());
		Check(
			pendingRecord.enqueued && pendingRecord.record,
			"the pre-cutoff pending create was not queued");
		{
			std::scoped_lock lock(pendingRecord.record->mutex);
			pendingRecord.record->bindReserved = true;
		}

		auto lease = catalog.TryAcquireProducerLease();
		Check(
			static_cast<bool>(lease),
			"route cutoff run did not lease a producer");
		const auto closeCallsBefore =
			CatalogDB::RouteAdmissionCloseCallsForTesting();
		Check(
			coordinator.Begin([&] { return catalog.Stop(); }),
			"route cutoff coordinator did not begin");
		coordinator.RequestClose(
			route_capture::CloseReason::kUserRequest);
		probe.Wait();

		// Drain-blocked: no new generic or route admission may enter.
		{
			auto create =
				catalog.BeginRouteCreate(TestRouteCreateInput(true));
			auto bind = catalog.BeginRouteBind();
			Check(
				!create && !bind
					&& !catalog.TryBeginProducerAdmission()
					&& !catalog.RouteCaptureActive(),
				"route admission stayed open past the generic cutoff");
		}
		// The pending record can no longer obtain its bind token.
		catalog.ReleaseRouteBindReservation(pendingRecord.record);
		lease.Reset();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"route cutoff close did not finalize");
		Check(
			CatalogDB::RouteAdmissionCloseCallsForTesting()
				- closeCallsBefore == 1,
			"the route admission cutoff did not run exactly once");

		// Both pre-cutoff records stay visible; no post-cutoff record appears.
		const auto documents = ReadRouteDocuments(routeRoot);
		Check(
			documents.size() == 3,
			"post-cutoff evidence reached route publication");
		const auto manifest = ReadRouteManifest(routeRoot);
		Check(
			manifest.find("\"capture_authoritative\":false")
				!= std::string::npos,
			"an incomplete pending record still claimed route authority");
		// Catalog authority is route-independent, so composed acceptance rejects
		// on route membership alone.
		Check(
			!ReadRunManifest(artifacts, runId).empty(),
			"the cutoff run did not publish its catalog manifest");
	}

	// The shared producer gate alone rejects route Begin, before route state closes.
	void TestRouteBeginRejectsAtSharedGate()
	{
		TempTree tree("shared-gate");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		SharedGateObservation observation;
		SharedGateProbe probe(observation);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"shared gate run did not start");
		RecordRouteEvidence(catalog);
		Check(catalog.Stop(), "the shared gate run did not finalize");

		Check(observation.fired, "the shared gate seam never ran");
		// The discriminator: route state is still live at this instant.
		Check(
			observation.generationStillActive,
			"route generation closed before the shared gate seam");
		Check(
			!observation.genericStillOpen,
			"the shared gate was still open at its own seam");
		Check(
			!observation.createAdmitted && !observation.bindAdmitted,
			"route Begin bypassed the closed shared gate");
	}

	// A live route token holds the outer producer admission for its whole life.
	void TestRouteTokenHoldsOuterAdmission()
	{
		TempTree tree("route-token-outer");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"route token run did not start");
		Check(
			catalog.ActiveProducerAdmissionsForTesting() == 0,
			"a fresh run already held a producer admission");

		{
			auto bind = catalog.BeginRouteBind();
			Check(
				static_cast<bool>(bind)
					&& catalog.ActiveProducerAdmissionsForTesting() == 1,
				"a bind token did not hold the outer admission");
			// Moves transfer the single outer admission, never duplicate it.
			auto moved = std::move(bind);
			Check(
				static_cast<bool>(moved) && !bind
					&& catalog.ActiveProducerAdmissionsForTesting() == 1,
				"a moved bind token lost or duplicated the outer admission");
			RouteBindAdmission assigned;
			assigned = std::move(moved);
			Check(
				static_cast<bool>(assigned) && !moved
					&& catalog.ActiveProducerAdmissionsForTesting() == 1,
				"a move-assigned bind token mishandled the outer admission");
		}
		Check(
			catalog.ActiveProducerAdmissionsForTesting() == 0,
			"a destroyed bind token leaked the outer admission");

		// An abandoned create token releases both claims on destruction.
		{
			auto create =
				catalog.BeginRouteCreate(TestRouteCreateInput(true));
			Check(
				static_cast<bool>(create)
					&& catalog.ActiveProducerAdmissionsForTesting() == 1,
				"a create token did not hold the outer admission");
		}
		Check(
			catalog.ActiveProducerAdmissionsForTesting() == 0,
			"an abandoned create token leaked the outer admission");

		// Unwinding through a live token releases its admission.
		try {
			auto thrown = catalog.BeginRouteBind();
			Check(
				static_cast<bool>(thrown)
					&& catalog.ActiveProducerAdmissionsForTesting() == 1,
				"the unwind token did not hold the outer admission");
			throw std::runtime_error("unwind");
		} catch (const std::runtime_error&) {
		}
		Check(
			catalog.ActiveProducerAdmissionsForTesting() == 0,
			"an unwound token leaked the outer admission");

		// Move-assigning over a live token releases the target exactly once.
		{
			auto first = catalog.BeginRouteBind();
			auto second = catalog.BeginRouteBind();
			Check(
				static_cast<bool>(first) && static_cast<bool>(second)
					&& catalog.ActiveProducerAdmissionsForTesting() == 2,
				"two bind tokens did not hold two outer admissions");
			first = std::move(second);
			Check(
				static_cast<bool>(first) && !second
					&& catalog.ActiveProducerAdmissionsForTesting() == 1,
				"move-assignment leaked or double-released an admission");
		}
		Check(
			catalog.ActiveProducerAdmissionsForTesting() == 0,
			"the move-assignment scope leaked an outer admission");

		// A completed create releases the outer admission with the publisher claim.
		auto completed =
			catalog.BeginRouteCreate(TestRouteCreateInput(true));
		Check(
			static_cast<bool>(completed),
			"the completed create token was refused");
		const auto result = catalog.CompleteRouteCreate(
			std::move(completed), TestRouteCreateOutcome());
		Check(
			result.enqueued
				&& catalog.ActiveProducerAdmissionsForTesting() == 0,
			"a completed create token retained the outer admission");
		// The consumed token still destructs; the second release must be inert.
		{
			auto reused = catalog.BeginRouteBind();
			Check(
				static_cast<bool>(reused)
					&& catalog.ActiveProducerAdmissionsForTesting() == 1,
				"a double release corrupted the admission count");
		}
		Check(
			catalog.ActiveProducerAdmissionsForTesting() == 0,
			"the post-completion token leaked the outer admission");
		Check(catalog.Stop(), "the route token run did not finalize");
		Check(
			catalog.ActiveProducerAdmissionsForTesting() == 0,
			"the finalized run leaked a producer admission");
	}

	// Records publisher presence at the startup commit point and at route freeze.
	struct PublisherPresenceObservation
	{
		bool precommitFired = false;
		bool precommitPresent = false;
		bool frozenFired = false;
		bool frozenPresent = false;
		std::uint64_t frozenAdmissions = 1;
	};

	std::atomic<PublisherPresenceObservation*> g_publisherPresence{ nullptr };

	void RecordPrecommitPublisherPresenceForTesting() noexcept
	{
		if (auto* observation =
				g_publisherPresence.load(std::memory_order_acquire)) {
			observation->precommitPresent =
				CatalogDB::Get().RoutePublisherPresentForTesting();
			observation->precommitFired = true;
		}
	}

	void RecordFrozenPublisherPresenceForTesting() noexcept
	{
		if (auto* observation =
				g_publisherPresence.load(std::memory_order_acquire)) {
			auto& catalog = CatalogDB::Get();
			observation->frozenPresent =
				catalog.RoutePublisherPresentForTesting();
			observation->frozenAdmissions =
				catalog.ActiveProducerAdmissionsForTesting();
			observation->frozenFired = true;
		}
	}

	// Clears both presence seams even when a Check throws.
	class PublisherPresenceProbe
	{
	public:
		explicit PublisherPresenceProbe(
			PublisherPresenceObservation& a_observation)
		{
			g_publisherPresence.store(
				&a_observation, std::memory_order_release);
			CatalogDB::SetBeforeRunCommitCallbackForTesting(
				&RecordPrecommitPublisherPresenceForTesting);
			CatalogDB::SetRouteCaptureFrozenCallbackForTesting(
				&RecordFrozenPublisherPresenceForTesting);
		}

		~PublisherPresenceProbe()
		{
			CatalogDB::SetRouteCaptureFrozenCallbackForTesting(nullptr);
			CatalogDB::SetBeforeRunCommitCallbackForTesting(nullptr);
			g_publisherPresence.store(nullptr, std::memory_order_release);
		}

		PublisherPresenceProbe(const PublisherPresenceProbe&) = delete;
		PublisherPresenceProbe& operator=(
			const PublisherPresenceProbe&) = delete;
	};

	// The publisher lives from construction to publication, then is released once.
	void TestRoutePublisherPresenceStates()
	{
		TempTree tree("publisher-presence");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		PublisherPresenceObservation observation;
		PublisherPresenceProbe probe(observation);
		auto& catalog = CatalogDB::Get();
		const auto destructionsBefore =
			RoutePublisherDestructionsForTesting();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"publisher presence run did not start");

		// Precommit: constructed but not yet admitting; running: admitting.
		Check(
			observation.precommitFired && observation.precommitPresent,
			"the publisher was absent at the startup commit point");
		Check(
			catalog.RoutePublisherPresentForTesting()
				&& catalog.RouteCaptureActive(),
			"a running route capture had no publisher");
		RecordRouteEvidence(catalog);
		const auto running = catalog.GetStats();
		Check(catalog.Stop(), "publisher presence run did not finalize");

		// Closing: alive at route freeze, with every admission already drained.
		Check(
			observation.frozenFired && observation.frozenPresent,
			"the publisher was released before route publication");
		Check(
			observation.frozenAdmissions == 0,
			"route freeze ran before the admission drain completed");
		// Final: released exactly once, and no longer admitting.
		Check(
			!catalog.RoutePublisherPresentForTesting(),
			"a finalized run retained the route publisher");
		Check(
			RoutePublisherDestructionsForTesting()
				== destructionsBefore + 1,
			"the route publisher was not destroyed exactly once");
		{
			auto create =
				catalog.BeginRouteCreate(TestRouteCreateInput(true));
			auto bind = catalog.BeginRouteBind();
			Check(
				!create && !bind
					&& catalog.ActiveProducerAdmissionsForTesting() == 0,
				"a released publisher still admitted route work");
		}
		// The cached tuple must not depend on the released publisher.
		const auto finalStats = catalog.GetStats();
		Check(
			finalStats.routeCaptureRequested
				&& !finalStats.routeCaptureActive
				&& finalStats.generatedRunId == running.generatedRunId
				&& finalStats.generation == running.generation,
			"the final tuple depended on the released publisher");
	}

	// Disarms the observation failure point even when a Check throws.
	class ObservationPublishFailureScope
	{
	public:
		ObservationPublishFailureScope()
		{
			FailNextObservationPublishForTesting(true);
		}

		~ObservationPublishFailureScope()
		{
			FailNextObservationPublishForTesting(false);
		}

		ObservationPublishFailureScope(
			const ObservationPublishFailureScope&) = delete;
		ObservationPublishFailureScope& operator=(
			const ObservationPublishFailureScope&) = delete;
	};

	// A local observation write failure stays route-local and fail-closed.
	void TestRouteObservationPublishFailure()
	{
		RouteCaptureSeamScope seams;
		auto& catalog = CatalogDB::Get();

		// Baseline: the identical run shape with no injected failure.
		std::int64_t baselineAuthority = -1;
		{
			TempTree tree("route-publish-ok");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			const auto routeRoot = tree.path / "route";
			std::filesystem::create_directory(artifacts);
			std::filesystem::create_directory(routeRoot);
			Check(
				StartReady(
					catalog,
					RouteCaptureRunConfig(database, artifacts, routeRoot)),
				"route publish baseline run did not start");
			const auto runId = catalog.GetStats().generatedRunId;
			RecordRouteEvidence(catalog);
			Check(catalog.Stop(), "route publish baseline did not finalize");
			baselineAuthority = SqlInt(
				database,
				("SELECT authoritative FROM catalog_runs"
				 " WHERE generated_run_id='"
				 + runId + "'")
					.c_str());
		}

		TempTree tree("route-publish-fail");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		const auto destructionsBefore =
			RoutePublisherDestructionsForTesting();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"route publish failure run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);
		const auto running = catalog.GetStats();
		{
			ObservationPublishFailureScope armed;
			Check(
				catalog.Stop(),
				"a route observation failure blocked catalog finalization");
		}

		// The failed observation left no document; only the manifest exists.
		Check(
			!std::filesystem::exists(routeRoot / "observations")
				|| std::filesystem::is_empty(routeRoot / "observations"),
			"a failed observation publish still wrote a document");
		const auto manifest = ReadRouteManifest(routeRoot);
		Check(
			manifest.find("\"capture_authoritative\":false")
					!= std::string::npos
				&& manifest.find("\"persistence-loss\"") != std::string::npos,
			"a failed observation publish lost its persistence-loss reason");
		// No route row exists, so composed acceptance rejects on membership.
		Check(
			manifest.find("\"observations\":[{") == std::string::npos,
			"a failed observation still claimed a route manifest row");

		// Catalog authority is route-independent: identical to the baseline.
		Check(
			!ReadRunManifest(artifacts, runId).empty(),
			"the catalog run lost its own manifest to a route failure");
		Check(
			SqlInt(
				database,
				("SELECT authoritative FROM catalog_runs"
				 " WHERE generated_run_id='"
				 + runId + "'")
					.c_str())
				== baselineAuthority,
			"a route-local failure changed catalog authority");

		// The publisher is still released exactly once on this failure path.
		Check(
			!catalog.RoutePublisherPresentForTesting(),
			"a failed route publication retained the publisher");
		Check(
			RoutePublisherDestructionsForTesting()
				== destructionsBefore + 1,
			"a failed route publication did not release the publisher once");
		const auto finalStats = catalog.GetStats();
		Check(
			finalStats.routeCaptureRequested
				&& !finalStats.routeCaptureActive
				&& finalStats.generatedRunId == running.generatedRunId
				&& finalStats.generation == running.generation,
			"a failed route publication disturbed the final tuple");
	}

	// Route admission stays shut until the startup commit publishes it.
	void TestRouteAdmissionHiddenUntilCommit()	{
		std::uint64_t g_savedGeneration = 0;
		auto& catalog = CatalogDB::Get();
		{
			TempTree tree("route-admission-commit");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			const auto routeRoot = tree.path / "route";
			std::filesystem::create_directory(artifacts);
			std::filesystem::create_directory(routeRoot);
			RouteCaptureSeamScope seams;
			StartupCommitPointProbe probe;
			Check(
				StartReady(
					catalog,
					RouteCaptureRunConfig(database, artifacts, routeRoot)),
				"route admission run did not start");
			Check(
				g_commitPointReached.load(std::memory_order_acquire),
				"the route admission commit point was never observed");
			// The publisher already exists here, yet nothing may be admitted.
			Check(
				g_commitPointIdentityReady.load(std::memory_order_acquire)
					&& g_commitPointRouteAdmissionRejected.load(
						std::memory_order_acquire),
				"route admission was granted before the startup commit");
			Check(
				g_commitPointTupleClean.load(std::memory_order_acquire),
				"route capture was published before the startup commit");

			// After the commit both admissions open under a nonzero generation.
			const auto live = catalog.GetStats();
			Check(
				live.routeCaptureRequested
					&& live.routeCaptureActive
					&& live.generation != 0
					&& catalog.RouteCaptureActive()
					&& IdentityTupleIsLegal(live),
				"the committed run did not publish route capture");
			{
				auto create =
					catalog.BeginRouteCreate(TestRouteCreateInput(true));
				auto bind = catalog.BeginRouteBind();
				Check(
					static_cast<bool>(create) && static_cast<bool>(bind),
					"the committed run refused route admission");
			}
			{
				// A stale generation cannot admit into the live run.
				auto stale =
					catalog.BeginRouteCreateForGenerationForTesting(
						live.generation - 1, TestRouteCreateInput(true));
				auto staleBind =
					catalog.BeginRouteBindForGenerationForTesting(
						live.generation - 1);
				Check(
					!stale && !staleBind,
					"a stale generation admitted into the live run");
			}
			g_savedGeneration = live.generation;
			Check(catalog.Stop(), "route admission run did not finalize");
			const auto terminal = catalog.GetStats();
			Check(
				terminal.lifecycle == "finalized"
					&& terminal.generation == live.generation
					&& !terminal.routeCaptureActive
					&& !catalog.RouteCaptureActive()
					&& IdentityTupleIsLegal(terminal),
				"the terminal tuple lost its generation or stayed active");
			{
				// Finalized retains identity and generation but admits nothing.
				auto create =
					catalog.BeginRouteCreate(TestRouteCreateInput(true));
				auto bind = catalog.BeginRouteBind();
				auto sameGen =
					catalog.BeginRouteCreateForGenerationForTesting(
						terminal.generation, TestRouteCreateInput(true));
				Check(
					!create && !bind && !sameGen,
					"a finalized run still admitted route capture");
			}
		}
		{
			// An aborted commit leaves no admission and a safe publisher reset.
			TempTree tree("route-admission-abort");
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			const auto routeRoot = tree.path / "route";
			std::filesystem::create_directory(artifacts);
			std::filesystem::create_directory(routeRoot);
			RouteCaptureSeamScope seams;
			auto config =
				RouteCaptureRunConfig(database, artifacts, routeRoot);
			config.failRunCommitForTesting = true;
			Check(
				!catalog.Start(config, TestIdentity()),
				"an aborted route run reported success");
			Check(
				!catalog.RouteCaptureActive()
					&& !catalog.GetStats().routeCaptureActive
					&& !catalog.GetStats().routeCaptureRequested,
				"an aborted route run left capture published");
			{
				auto create =
					catalog.BeginRouteCreate(TestRouteCreateInput(true));
				auto bind = catalog.BeginRouteBind();
				Check(
					!create && !bind,
					"an aborted route run still admitted capture");
			}
			Check(
				!std::filesystem::exists(routeRoot / "observations")
					&& !std::filesystem::exists(routeRoot / "manifests"),
				"an aborted route run published documents");
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"a later run was blocked by the aborted route attempt");
			// Every committed run gets a strictly larger generation.
			const auto next = catalog.GetStats();
			Check(
				next.generation > g_savedGeneration
					&& IdentityTupleIsLegal(next),
				"the next run did not take a strictly larger generation");
			{
				// The previous run's generation cannot admit into this one.
				auto stale =
					catalog.BeginRouteCreateForGenerationForTesting(
						g_savedGeneration, TestRouteCreateInput(true));
				auto staleBind =
					catalog.BeginRouteBindForGenerationForTesting(
						g_savedGeneration);
				Check(
					!stale && !staleBind,
					"a stale generation admitted into the next run");
			}
			Check(catalog.Stop(), "the later run did not finalize");
		}
	}

	// Finalization exceptions run fail-closed cleanup and publish nothing authoritative.
	void TestStopExceptionFailsClosed()
	{
		using namespace std::chrono_literals;
		const std::pair<const char*, int> seams[] = {
			{ "before-route-publication", 2 },
			{ "manifest-load", 0 },
			{ "after-route-publication", 1 }
		};
		for (const auto& [name, kind] : seams) {
			TempTree tree(std::string("stop-throw-") + name);
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			const auto routeRoot = tree.path / "route";
			std::filesystem::create_directory(artifacts);
			std::filesystem::create_directory(routeRoot);
			RouteCaptureSeamScope seams2;
			ManualTime time;
			route_capture::Coordinator coordinator(time);
			SealTargetScope seal(coordinator);
			auto& catalog = CatalogDB::Get();
			auto config =
				RouteCaptureRunConfig(database, artifacts, routeRoot);
			config.failManifestLoadForTesting = kind == 0;
			config.failAfterRoutePublicationForTesting = kind == 1;
			config.failBeforeRoutePublicationForTesting = kind == 2;
			Check(
				StartReady(catalog, config),
				"stop-throw run did not start");
			const auto runId = catalog.GetStats().generatedRunId;
			RecordRouteEvidence(catalog);
			const auto closeCallsBefore =
				CatalogDB::RouteAdmissionCloseCallsForTesting();

			// The close runs on the coordinator so its terminal outcome is bounded.
			std::atomic<unsigned> closes{ 0 };
			Check(
				coordinator.Begin([&] {
					closes.fetch_add(1, std::memory_order_relaxed);
					return catalog.Stop();
				}),
				"stop-throw coordinator did not begin");
			coordinator.RequestClose(
				route_capture::CloseReason::kUserRequest);
			Check(
				coordinator.WaitForTerminal(kTestWaitBudget),
				"a throwing finalization left the coordinator non-terminal");
			const auto status = coordinator.Snapshot();
			Check(
				status.state == route_capture::State::kFinalizedInert
					&& !status.authoritative
					&& closes.load(std::memory_order_relaxed) == 1,
				"a throwing finalization broke the coordinator terminal outcome");
			Check(
				CatalogDB::RouteAdmissionCloseCallsForTesting()
					- closeCallsBefore == 1,
				"the route admission cutoff did not run exactly once");
			Check(
				PublishedTuple(catalog.GetStats())
					== PublishedTuple(CatalogDB::Stats{}),
				"a throwing finalization left a live public tuple");
			Check(
				!catalog.RouteCaptureActive()
					&& !catalog.Stop() && !catalog.Stop(),
				"a throwing finalization left a closable database");
			Check(
				CatalogDB::ActiveTransientStatementsForTesting() == 0,
				"a throwing finalization leaked a transient statement");
			Check(
				!catalog.RoutePublisherPresentForTesting(),
				"a throwing finalization retained the route publisher");
			// The manifest seam throws holding a live prepared statement, so the
			// shared assertions below prove finalize, repair, checked close and
			// repeat Stop all survive one.
			if (kind == 0) {
				Check(
					CatalogDB::LastManifestThrowActiveStatementsForTesting()
						== 1,
					"the manifest seam threw without a live transient statement");
			}
			Check(
				CatalogDB::LastStartupCloseResultForTesting() == SQLITE_OK,
				"a throwing finalization closed the database busy");
			Check(
				!std::filesystem::exists(
					artifacts / "runs" / runId / "manifest.v1.json"),
				"a throwing finalization published a main manifest");
			const auto runScope =
				" WHERE generated_run_id='" + runId + "'";
			Check(
				SqlText(
					database,
					("SELECT lifecycle FROM catalog_runs" + runScope)
						.c_str())
					== "abandoned",
				"a throwing finalization left a live run row");
			Check(
				SqlInt(
					database,
					("SELECT COUNT(*) FROM catalog_runs" + runScope
					 + " AND (authoritative=1 OR manifest_published=1 "
					   "OR publication_pending=1)")
						.c_str())
					== 0,
				"a throwing finalization claimed catalog authority");
			// An orphan route manifest is allowed, but composed acceptance rejects.
			if (kind == 2) {
				Check(
					!std::filesystem::exists(routeRoot / "manifests")
						&& !std::filesystem::exists(
							routeRoot / "observations"),
					"a pre-publication throw still wrote route documents");
			} else if (std::filesystem::exists(routeRoot / "manifests")) {
				Check(
					!catalog.GetStats().authoritative,
					"an orphan route manifest gained catalog authority");
			}
			Check(
				SqlInt(
					database,
					("SELECT lifecycle_failure FROM catalog_run_quality"
					 + runScope)
						.c_str())
					>= 1,
				"a throwing finalization recorded no lifecycle failure");
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"a later start was blocked by the throwing finalization");
			Check(catalog.Stop(), "the later start did not finalize");
		}
	}

	// Releases the held statement and retained handle even when a Check throws.
	struct HeldStatementScope
	{
		CatalogDB& db;
		~HeldStatementScope()
		{
			db.ReleaseHeldStatementForTesting();
			db.RetryCheckedCloseForTesting();
		}
	};

	// A close that reports BUSY must retain the handle and refuse service, not lie.
	void TestCloseBusyRetainsNonServiceState()
	{
		TempTree tree("close-busy");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		HeldStatementScope held{ catalog };

		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"close-busy run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		Check(
			catalog.HoldStatementForCloseBusyForTesting(),
			"the held statement was not prepared");

		Check(!catalog.Stop(), "a busy close reported success");
		Check(
			catalog.CloseRetainedBusyForTesting(),
			"a busy close did not retain the handle");
		Check(
			PublishedTuple(catalog.GetStats())
				== PublishedTuple(CatalogDB::Stats{}),
			"a busy close left a live public tuple");
		const auto runScope = " WHERE generated_run_id='" + runId + "'";
		Check(
			SqlText(
				database,
				("SELECT lifecycle FROM catalog_runs" + runScope).c_str())
				== "abandoned",
			"a busy close left a live run row");
		Check(
			SqlInt(
				database,
				("SELECT COUNT(*) FROM catalog_runs" + runScope
				 + " AND (authoritative=1 OR manifest_published=1 "
				   "OR publication_pending=1)")
					.c_str())
				== 0,
			"a busy close claimed catalog authority");
		const auto closeCallsAfterClose =
			CatalogDB::RouteAdmissionCloseCallsForTesting();
		// The handle is retained, so no new run may start and Stop stays false.
		Check(
			catalog.CloseRetainedBusyForTesting(),
			"the retained busy handle was cleared before the repeat close");
		Check(
			!catalog.Stop() && !catalog.Stop(),
			"a busy close became closable without releasing the statement");
		// A terminal retained handle must never re-enter finalization at all.
		Check(
			CatalogDB::RouteAdmissionCloseCallsForTesting()
				== closeCallsAfterClose,
			"a repeated busy close re-ran finalization");
		Check(
			!catalog.Start(TestConfig(database, artifacts), TestIdentity()),
			"a retained busy handle still admitted a new run");
		Check(
			CatalogDB::ActiveTransientStatementsForTesting() == 0,
			"a busy close leaked a transient statement");

		// Releasing the statement and retrying the checked close restores service.
		catalog.ReleaseHeldStatementForTesting();
		Check(
			catalog.RetryCheckedCloseForTesting(),
			"the checked close retry did not succeed");
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"the singleton could not continue after a busy close");
		Check(
			catalog.GetStats().generatedRunId != runId,
			"the recovered run reused the busy run identity");
		Check(catalog.Stop(), "the recovered run did not finalize");
	}

	// A finalization exception whose cleanup close reports BUSY must stay terminal.
	void TestStopExceptionWithBusyClose()
	{
		TempTree tree("stop-throw-busy");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		auto& catalog = CatalogDB::Get();
		HeldStatementScope held{ catalog };

		auto config = RouteCaptureRunConfig(database, artifacts, routeRoot);
		config.failManifestLoadForTesting = true;
		Check(
			StartReady(catalog, config),
			"stop-throw-busy run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);
		Check(
			catalog.HoldStatementForCloseBusyForTesting(),
			"the held statement was not prepared");
		const auto closeCallsBefore =
			CatalogDB::RouteAdmissionCloseCallsForTesting();

		// The close runs on the coordinator so its terminal outcome is bounded.
		std::atomic<unsigned> closes{ 0 };
		Check(
			coordinator.Begin([&] {
				closes.fetch_add(1, std::memory_order_relaxed);
				return catalog.Stop();
			}),
			"stop-throw-busy coordinator did not begin");
		coordinator.RequestClose(route_capture::CloseReason::kUserRequest);
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"a throwing busy finalization left the coordinator non-terminal");
		const auto status = coordinator.Snapshot();
		Check(
			status.state == route_capture::State::kFinalizedInert
				&& !status.authoritative
				&& closes.load(std::memory_order_relaxed) == 1,
			"a throwing busy finalization broke the terminal outcome");
		Check(
			PublishedTuple(catalog.GetStats())
				== PublishedTuple(CatalogDB::Stats{}),
			"a throwing busy finalization left a live public tuple");
		Check(
			catalog.CloseRetainedBusyForTesting(),
			"a throwing busy finalization did not retain the handle");
		Check(
			!catalog.RoutePublisherPresentForTesting(),
			"an abort/repair terminal path retained the route publisher");

		// The retained handle is terminal: no re-entry, no new run, no cutoff.
		const auto closeCallsAfter =
			CatalogDB::RouteAdmissionCloseCallsForTesting();
		Check(
			closeCallsAfter - closeCallsBefore == 1,
			"a throwing busy finalization did not close admission exactly once");
		Check(
			!catalog.Stop() && !catalog.Stop(),
			"a throwing busy finalization became closable");
		Check(
			CatalogDB::RouteAdmissionCloseCallsForTesting() == closeCallsAfter,
			"a repeated throwing busy close re-ran finalization");
		Check(
			!catalog.Start(TestConfig(database, artifacts), TestIdentity()),
			"a retained busy handle still admitted a new run");
		Check(
			CatalogDB::ActiveTransientStatementsForTesting() == 0,
			"a throwing busy finalization leaked a transient statement");
		Check(
			!std::filesystem::exists(
				artifacts / "runs" / runId / "manifest.v1.json"),
			"a throwing busy finalization published a main manifest");
		const auto runScope = " WHERE generated_run_id='" + runId + "'";
		Check(
			SqlText(
				database,
				("SELECT lifecycle FROM catalog_runs" + runScope).c_str())
				== "abandoned",
			"a throwing busy finalization left a live run row");
		Check(
			SqlInt(
				database,
				("SELECT COUNT(*) FROM catalog_runs" + runScope
				 + " AND (authoritative=1 OR manifest_published=1 "
				   "OR publication_pending=1)")
					.c_str())
				== 0,
			"a throwing busy finalization claimed catalog authority");

		// Releasing the statement and retrying the checked close restores service.
		catalog.ReleaseHeldStatementForTesting();
		Check(
			catalog.RetryCheckedCloseForTesting(),
			"the checked close retry did not succeed");
		Check(
			!catalog.CloseRetainedBusyForTesting(),
			"a successful retry left the retained busy guard set");
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"the singleton could not continue after a throwing busy close");
		Check(
			catalog.GetStats().generatedRunId != runId,
			"the recovered run reused the throwing busy run identity");
		Check(catalog.Stop(), "the recovered run did not finalize");
	}

	// Startup order: resources ready, writer waiting, commit, publish, writer release.
	void TestStartupCommitOrdering()
	{
		TempTree tree("startup-ordering");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		std::filesystem::create_directory(artifacts);
		auto& catalog = CatalogDB::Get();
		StartupCommitPointProbe probe;
		const auto writerBaseline = CatalogDB::WriterRunEntriesForTesting();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"ordering run did not start");
		Check(
			g_commitPointReached.load(std::memory_order_acquire),
			"the startup commit point was never observed");
		Check(
			g_commitPointIdentityReady.load(std::memory_order_acquire),
			"stored identity was not ready before the startup commit");
		Check(
			g_commitPointRunIdHidden.load(std::memory_order_acquire)
				&& g_commitPointTupleClean.load(std::memory_order_acquire),
			"the public tuple was not pristine inactive before the commit");
		Check(
			g_commitPointRouteAdmissionRejected.load(
				std::memory_order_acquire),
			"route admission was granted before the startup commit");
		Check(
			g_commitPointWriterEntries.load(std::memory_order_acquire)
				== writerBaseline,
			"the writer ran before the startup commit");
		Check(
			g_commitPointLifecycleInactive.load(std::memory_order_acquire)
				&& g_commitPointAdmissionClosed.load(
					std::memory_order_acquire),
			"a half-started run was observable before the startup commit");

		const auto stats = catalog.GetStats();
		Check(
			stats.lifecycle == "running"
				&& !stats.generatedRunId.empty()
				&& !stats.authoritative
				&& IdentityTupleIsLegal(stats),
			"the committed run was not published in memory");
		auto lease = catalog.TryAcquireProducerLease();
		Check(
			static_cast<bool>(lease),
			"the committed run did not open producer admission");
		lease.Reset();
		Check(
			SqlInt(
				database,
				("SELECT COUNT(*) FROM catalog_runs WHERE generated_run_id='"
				 + stats.generatedRunId + "'")
					.c_str())
				== 1,
			"the published run has no committed row");
		Check(catalog.Stop(), "ordering run did not finalize");
		// Post-final phase: terminal lifecycle keeps the same committed identity.
		const auto finalStats = catalog.GetStats();
		Check(
			finalStats.lifecycle == "finalized"
				&& finalStats.generatedRunId == stats.generatedRunId
				&& !finalStats.routeCaptureActive
				&& finalStats.authoritative
				&& IdentityTupleIsLegal(finalStats),
			"the terminal phase lost or changed the committed identity");
		// Join proves the released writer ran exactly once, after the commit.
		Check(
			CatalogDB::WriterRunEntriesForTesting() - writerBaseline == 1,
			"the gated writer was not released exactly once");
	}

	// Neither startup failure seam may leave a durable run row or any live service.
	void TestStartupFailureLeavesNoRun()
	{
		const std::pair<const char*, int> faults[] = {
			{ "statement-prepare", 0 },
			{ "writer-start", 1 },
			{ "run-insert", 2 },
			{ "run-commit", 3 },
			{ "bootstrap-statement", 4 }
		};
		for (const auto& [name, kind] : faults) {
			TempTree tree(std::string("startup-fault-") + name);
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			const auto routeRoot = tree.path / "route";
			std::filesystem::create_directory(artifacts);
			std::filesystem::create_directory(routeRoot);
			RouteCaptureSeamScope seams;
			auto& catalog = CatalogDB::Get();

			// A committed seed run gives the failed attempt an exact row baseline.
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"startup fault seed run did not start");
			const auto seedRunId = catalog.GetStats().generatedRunId;
			Check(catalog.Stop(), "startup fault seed run did not stop");

			// Armed after the seed so it observes only the failing attempt.
			StartupCommitPointProbe commitProbe;
			const auto writerBaseline =
				CatalogDB::WriterRunEntriesForTesting();

			ManualTime time;
			route_capture::Coordinator coordinator(time);
			SealTargetScope seal(coordinator);
			std::atomic<unsigned> closes{ 0 };
			Check(
				coordinator.Begin([&] {
					closes.fetch_add(1, std::memory_order_relaxed);
					return CatalogDB::Get().Stop();
				}),
				"startup fault coordinator did not begin");

			auto config =
				RouteCaptureRunConfig(database, artifacts, routeRoot);
			config.failStatementPrepareForTesting = kind == 0;
			config.failWriterStartForTesting = kind == 1;
			config.failRunInsertForTesting = kind == 2;
			config.failRunCommitForTesting = kind == 3;
			config.failBootstrapStatementForTesting = kind == 4;
			bool started = true;
			bool threw = false;
			try {
				started = catalog.Start(config, TestIdentity());
			} catch (...) {
				threw = true;
			}
			Check(
				!started && !threw,
				"a startup fault did not return false without throwing");
			// The gated writer never consumed, persisted, checkpointed, or closed.
			Check(
				CatalogDB::WriterRunEntriesForTesting() == writerBaseline,
				"a startup fault released the gated writer");

			const auto stranded = catalog.GetStats();
			// No identity, cache, or service field for an uncommitted run may survive.
			Check(
				IdentityTuple(stranded) == IdentityTuple(CatalogDB::Stats{}),
				"a startup fault left phantom identity or service state");
			Check(
				!catalog.RouteCaptureActive(),
				"a startup fault left the route publisher open");
			Check(
				QualityTuple(stranded.quality)
						== QualityTuple(QualityCounters{})
					&& StatTuple(stranded) == StatTuple(CatalogDB::Stats{}),
				"a startup fault left phantom counters");
			// Every transient statement released and the connection actually closed.
			Check(
				CatalogDB::ActiveTransientStatementsForTesting() == 0,
				"a startup fault leaked a transient statement");
			Check(
				CatalogDB::LastStartupCloseResultForTesting() == SQLITE_OK,
				"a startup fault closed the database busy");
			// Seams that reach the commit point prove callback teardown stays bounded.
			Check(
				(kind == 2 || kind == 3)
					== g_commitPointReached.load(std::memory_order_acquire),
				"the commit point did not run exactly for the committing seams");
			Check(
				!catalog.TryBeginProducerAdmission()
					&& !static_cast<bool>(catalog.TryAcquireProducerLease())
					&& !static_cast<bool>(
						catalog.BeginRouteCreate(TestRouteCreateInput(true)))
					&& !static_cast<bool>(catalog.BeginRouteBind()),
				"a startup fault left an admission open");
			SubmitRejectedProducerEntries(catalog, 1);
			Check(
				QualityTuple(catalog.GetStats().quality)
						== QualityTuple(stranded.quality)
					&& StatTuple(catalog.GetStats()) == StatTuple(stranded),
				"a startup fault still accepted producer accounting");
			Check(
				!catalog.Stop() && !catalog.Stop(),
				"a startup fault left a closable database");
			Check(
				!std::filesystem::exists(routeRoot / "observations")
					&& !std::filesystem::exists(routeRoot / "manifests"),
				"a startup fault published route documents");

			// The insert transaction never committed, so only the seed rows exist.
			Check(
				SqlInt(database, "SELECT COUNT(*) FROM catalog_runs") == 1
					&& SqlText(
						database,
						"SELECT generated_run_id FROM catalog_runs")
						== seedRunId,
				"a startup fault left a durable run row");
			Check(
				SqlInt(
					database,
					"SELECT COUNT(*) FROM catalog_run_quality") == 1
					&& SqlInt(
						database, "SELECT COUNT(*) FROM sessions") == 1,
				"a startup fault left a partial run row");

			Check(
				coordinator.AbortBeforeClose()
					&& coordinator.Snapshot().state
						== route_capture::State::kAborted
					&& closes.load(std::memory_order_relaxed) == 0,
				"the prestarted coordinator could not be rolled back");

			const auto recoveryBaseline =
				CatalogDB::WriterRunEntriesForTesting();
			Check(
				recoveryBaseline == writerBaseline,
				"the aborted attempt moved the writer probe");
			Check(
				StartReady(catalog, TestConfig(database, artifacts)),
				"a later valid start was blocked by the failed attempt");
			Check(
				catalog.GetStats().generatedRunId != seedRunId
					&& !catalog.GetStats().generatedRunId.empty()
					&& catalog.GetStats().lifecycle == "running",
				"the later valid start did not open a new run");
			Check(catalog.Stop(), "the later valid start did not finalize");
			Check(
				CatalogDB::WriterRunEntriesForTesting() - recoveryBaseline
					== 1,
				"the recovered run did not release exactly one writer");
			Check(
				SqlInt(database, "SELECT COUNT(*) FROM catalog_runs") == 2,
				"the later valid start did not add exactly one run row");
		}
	}

	void TestRouteCaptureStartupRollback()
	{
		using namespace std::chrono_literals;
		{
			ManualTime time;
			route_capture::Coordinator coordinator(time);
			Check(
				!coordinator.Begin({}),
				"a coordinator without a close action started a worker");
			coordinator.RequestClose(
				route_capture::CloseReason::kProcessExit);
			const auto status = coordinator.Snapshot();
			Check(
				status.state == route_capture::State::kInactive
					&& !status.deadlineArmed
					&& !status.finalizationTimedOut
					&& !coordinator.WaitForTerminal(kTestWaitBudget)
					&& !coordinator.ArmCaptureDeadline(90s),
				"a failed coordinator startup left a serviceable state");
		}

		TempTree tree("route-startup-rollback");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		g_routeHooksReady = true;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, TestConfig(database, artifacts)),
			"rollback seed run did not start");
		Check(catalog.Stop(), "rollback seed run did not stop");
		SqlExec(
			database,
			"UPDATE corpus_meta SET value='99' WHERE key='schema_version';");

		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		std::atomic<unsigned> closes{ 0 };
		Check(
			coordinator.Begin([&] {
				closes.fetch_add(1, std::memory_order_relaxed);
				return CatalogDB::Get().Stop();
			}),
			"rollback coordinator did not begin");
		Check(
			!catalog.Start(
				RouteCaptureRunConfig(database, artifacts, routeRoot),
				TestIdentity()),
			"an unsupported schema still started a run");
		Check(
			coordinator.AbortBeforeClose()
				&& coordinator.AbortBeforeClose(),
			"coordinator rollback was refused or not idempotent");
		const auto aborted = coordinator.Snapshot();
		Check(
			aborted.state == route_capture::State::kAborted
				&& !aborted.deadlineArmed
				&& !aborted.finalizationTimedOut
				&& !aborted.authoritative
				&& closes.load(std::memory_order_relaxed) == 0,
			"rollback did not reach a terminal non-service state");
		Check(
			coordinator.TelemetrySnapshot().state
				== route_capture::State::kAborted,
			"telemetry did not publish the aborted state");
		coordinator.RequestClose(
			route_capture::CloseReason::kProcessExit);
		Check(
			!coordinator.Begin([] { return true; })
				&& !coordinator.ArmCaptureDeadline(90s)
				&& !coordinator.WaitForTerminal(kTestWaitBudget)
				&& coordinator.Snapshot().state
					== route_capture::State::kAborted
				&& closes.load(std::memory_order_relaxed) == 0,
			"an aborted coordinator serviced a second run or request");
		Check(
			!static_cast<bool>(catalog.TryAcquireProducerLease())
				&& !static_cast<bool>(
					catalog.BeginRouteCreate(TestRouteCreateInput(true)))
				&& !static_cast<bool>(catalog.BeginRouteBind())
				&& !catalog.GetStats().routeCaptureActive
				&& catalog.GetStats().lifecycle == "inactive",
			"a failed database startup stranded an admission or run");
		Check(
			!std::filesystem::exists(routeRoot / "observations")
				&& !std::filesystem::exists(routeRoot / "manifests"),
			"a failed database startup published route documents");
	}

	void TestRouteCaptureTimeoutWithoutWaiter()
	{
		using namespace std::chrono_literals;
		TempTree tree("route-timeout-no-waiter");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		auto& catalog = CatalogDB::Get();
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		AdmissionClosedProbe probe;
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"no-waiter timeout run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);
		auto lease = catalog.TryAcquireProducerLease();
		Check(
			static_cast<bool>(lease),
			"no-waiter run did not lease a producer");

		std::atomic<unsigned> closes{ 0 };
		Check(
			coordinator.Begin([&] {
				closes.fetch_add(1, std::memory_order_relaxed);
				return catalog.Stop();
			}),
			"no-waiter coordinator did not begin");
		Check(
			coordinator.ArmCaptureDeadline(90s),
			"no-waiter capture deadline was not armed");
		time.Advance(91s);
		probe.Wait();
		Check(
			!coordinator.Snapshot().finalizationTimedOut,
			"a close inside its budget reported a timeout");
		time.Advance(route_capture::kFinalizationBudget + 1s);
		const auto overdue = coordinator.Snapshot();
		Check(
			overdue.state == route_capture::State::kClosing
				&& overdue.finalizationTimedOut,
			"an overdue close was not observable without a waiter");
		Check(
			coordinator.TelemetrySnapshot().finalizationTimedOut,
			"telemetry did not publish the overdue close");
		Check(
			!std::filesystem::exists(routeRoot / "manifests"),
			"route publication ran before the producer drain completed");

		lease.Reset();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"no-waiter close did not finalize after the drain");
		const auto terminal = coordinator.Snapshot();
		Check(
			terminal.state == route_capture::State::kFinalizedInert
				&& terminal.finalizationTimedOut
				&& !terminal.authoritative
				&& closes.load(std::memory_order_relaxed) == 1,
			"the seal did not latch an overdue open decision");
		Check(
			ReadRouteManifest(routeRoot).starts_with(
				"{\"authority_reasons\":[\"producer-declined\"],"
				"\"capture_authoritative\":false"),
			"the vetoed route manifest lost its contract fallback reason");
		const auto authority = ReadRouteAuthorityFacts(routeRoot);
		Check(
			authority.row && !authority.routeManifest,
			"a run-level veto rewrote the row-local capture facts");
		Check(
			!authority.MembershipAuthoritative(),
			"an authoritative row under a false route manifest satisfied membership");
		Check(
			ReadRunManifest(artifacts, runId)
					.find("\"authoritative\":false")
				!= std::string::npos,
			"the vetoed catalog run manifest claimed authority");
		Check(
			SqlInt(
				database,
				"SELECT lifecycle_failure FROM catalog_run_quality")
				== 1,
			"the timeout was not the exact durable lifecycle failure");
	}

	void TestRouteCaptureFinalizedInert()
	{
		using namespace std::chrono_literals;
		TempTree tree("route-inert");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		g_routeHooksReady = true;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"route capture run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		Check(
			catalog.GetStats().routeCaptureActive,
			"route capture did not open");
		Check(
			catalog.RoutePublisherPresentForTesting(),
			"an open route capture had no publisher");
		RecordRouteEvidence(catalog);

		std::atomic<unsigned> closes{ 0 };
		Check(
			coordinator.Begin([&] {
				closes.fetch_add(1, std::memory_order_relaxed);
				return catalog.Stop();
			}),
			"route capture coordinator did not begin");
		Check(
			coordinator.ArmCaptureDeadline(90s),
			"route capture deadline was not armed");
		time.Advance(91s);
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"route capture deadline did not finalize");
		Check(
			!catalog.RoutePublisherPresentForTesting(),
			"a finalized-inert close retained the route publisher");
		const auto status = coordinator.Snapshot();
		Check(
			status.state == route_capture::State::kFinalizedInert
				&& status.reason
					== route_capture::CloseReason::kCaptureDeadline
				&& status.authoritative
				&& !status.finalizationTimedOut
				&& closes.load(std::memory_order_relaxed) == 1,
			"in-process route capture finalization lost authority");
		const auto documents = ReadRouteDocuments(routeRoot);
		Check(
			documents.size() == 2,
			"route capture did not publish one observation and one manifest");
		Check(
			ReadRouteManifest(routeRoot)
					.find("\"capture_authoritative\":true")
				!= std::string::npos,
			"deadline finalization lost route manifest authority");
		Check(
			ReadRunManifest(artifacts, runId)
					.find("\"authoritative\":true")
				!= std::string::npos,
			"deadline finalization lost catalog run authority");

		Check(
			!catalog.GetStats().routeCaptureActive
				&& !static_cast<bool>(
					catalog.BeginRouteCreate(TestRouteCreateInput(true)))
				&& !static_cast<bool>(catalog.BeginRouteBind())
				&& !catalog.TryBeginProducerAdmission()
				&& !static_cast<bool>(catalog.TryAcquireProducerLease()),
			"finalized-inert capture admitted new evidence");

		const auto statsBefore = catalog.GetStats();
		const auto runManifestBefore = ReadRunManifest(artifacts, runId);
		SubmitRejectedProducerEntries(catalog, 1);
		const auto statsAfter = catalog.GetStats();
		Check(
			QualityTuple(statsAfter.quality)
					== QualityTuple(statsBefore.quality)
				&& StatTuple(statsAfter) == StatTuple(statsBefore),
			"post-close producer evidence mutated run counters");
		Check(
			ReadRouteDocuments(routeRoot) == documents
				&& ReadRunManifest(artifacts, runId) == runManifestBefore,
			"post-close producer evidence mutated published bytes");
	}

	void TestRouteCaptureWithoutExitHooks()
	{
		using namespace std::chrono_literals;
		TempTree tree("route-no-exit-hooks");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		g_routeHooksReady = true;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		auto config = RouteCaptureRunConfig(database, artifacts, routeRoot);
		config.orderlyFinalizerReadyForTesting = false;
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(catalog, config),
			"exit-hook-free route capture run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);
		Check(
			coordinator.Begin([&] { return catalog.Stop(); }),
			"exit-hook-free coordinator did not begin");
		Check(
			coordinator.ArmCaptureDeadline(90s),
			"exit-hook-free capture deadline was not armed");
		time.Advance(91s);
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"exit-hook-free capture did not finalize at its deadline");
		Check(
			ReadRouteManifest(routeRoot)
					.find("\"capture_authoritative\":true")
				!= std::string::npos,
			"route capture authority still depended on the exit path");
		Check(
			ReadRunManifest(artifacts, runId)
					.find("\"orderly_finalizer_ready\":false")
				!= std::string::npos,
			"the run manifest lost its exit-hook install state");
	}

	void TestRouteCaptureFinalizationTimeout()
	{
		TempTree tree("route-timeout");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		RouteCaptureSeamScope seams;
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		auto& catalog = CatalogDB::Get();
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"timeout run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);
		Check(
			coordinator.Begin([&] {
				time.Advance(
					route_capture::kFinalizationBudget
					+ std::chrono::seconds(1));
				g_routeRegistrySnapshot.generation += 1;
				return catalog.Stop();
			}),
			"timeout coordinator did not begin");
		coordinator.RequestClose(
			route_capture::CloseReason::kUserRequest);
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"timed-out close did not finalize");
		Check(
			!catalog.RoutePublisherPresentForTesting(),
			"a timed-out close retained the route publisher");
		const auto status = coordinator.Snapshot();
		Check(
			status.state == route_capture::State::kFinalizedInert
				&& status.finalizationTimedOut
				&& !status.authoritative,
			"an overrun close stayed authoritative");
		const auto manifest = ReadRouteManifest(routeRoot);
		Check(
			manifest.find("\"capture_authoritative\":false")
					!= std::string::npos
				&& manifest.find("\"stock-only-violation\"")
					!= std::string::npos,
			"the route manifest lost its derived fail-closed reason");
		Check(
			manifest.find("finalization-timeout") == std::string::npos
				&& manifest.find("producer-declined")
					== std::string::npos,
			"an external veto changed the closed derived reason set");
		Check(
			ReadRunManifest(artifacts, runId)
					.find("\"authoritative\":false")
				!= std::string::npos,
			"an overrun catalog run manifest claimed authority");
		Check(
			SqlInt(
				database,
				"SELECT lifecycle_failure FROM catalog_run_quality")
				== 1,
			"an overrun close did not persist exactly the timeout lifecycle failure");
		Check(
			coordinator.SealFinalizationDecision(),
			"a latched finalization timeout was promoted");
		coordinator.RequestClose(
			route_capture::CloseReason::kProcessExit);
		Check(
			coordinator.Snapshot().state
					== route_capture::State::kFinalizedInert
				&& coordinator.Snapshot().finalizationTimedOut,
			"a late request cleared the finalization timeout veto");

		TempTree cleanTree("route-timeout-clean");
		const auto cleanDatabase = cleanTree.path / "catalog.sqlite";
		const auto cleanArtifacts = cleanTree.path / "artifacts";
		const auto cleanRouteRoot = cleanTree.path / "route";
		std::filesystem::create_directory(cleanArtifacts);
		std::filesystem::create_directory(cleanRouteRoot);
		RouteCaptureSeamScope cleanSeams;
		ManualTime cleanTime;
		route_capture::Coordinator cleanCoordinator(cleanTime);
		SealTargetScope cleanSeal(cleanCoordinator);
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(
					cleanDatabase, cleanArtifacts, cleanRouteRoot)),
			"clean timeout run did not start");
		const auto cleanRunId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);
		Check(
			cleanCoordinator.Begin([&] {
				cleanTime.Advance(
					route_capture::kFinalizationBudget
					+ std::chrono::seconds(1));
				return catalog.Stop();
			}),
			"clean timeout coordinator did not begin");
		cleanCoordinator.RequestClose(
			route_capture::CloseReason::kUserRequest);
		Check(
			cleanCoordinator.WaitForTerminal(kTestWaitBudget),
			"clean timed-out close did not finalize");
		Check(
			cleanCoordinator.Snapshot().finalizationTimedOut,
			"a clean overrun close did not latch the timeout veto");
		const auto cleanManifest = ReadRouteManifest(cleanRouteRoot);
		Check(
			cleanManifest.starts_with(
				"{\"authority_reasons\":[\"producer-declined\"],"
				"\"capture_authoritative\":false"),
			"an otherwise clean vetoed run lost the contract fallback reason");
		Check(
			cleanManifest.find("finalization-timeout")
				== std::string::npos,
			"the route wire gained a reason outside the closed set");
		Check(
			ReadRunManifest(cleanArtifacts, cleanRunId)
					.find("\"authoritative\":false")
				!= std::string::npos,
			"a clean overrun catalog run manifest claimed authority");
		Check(
			SqlInt(
				cleanDatabase,
				"SELECT lifecycle_failure FROM catalog_run_quality")
				== 1,
			"a clean overrun close did not persist exactly the timeout lifecycle failure");
	}

	// Releases a held close action even when a Check throws.
	class HeldCloseAction
	{
	public:
		HeldCloseAction() :
			_gate(_release.get_future().share())
		{}

		~HeldCloseAction()
		{
			try {
				Release();
			} catch (...) {
			}
		}

		HeldCloseAction(const HeldCloseAction&) = delete;
		HeldCloseAction& operator=(const HeldCloseAction&) = delete;

		void Hold() const { _gate.wait(); }

		void Release()
		{
			if (_released.exchange(true, std::memory_order_acq_rel))
				return;
			_release.set_value();
		}

	private:
		std::promise<void> _release;
		std::shared_future<void> _gate;
		std::atomic<bool> _released{ false };
	};

	void TestRouteCaptureSealedDecisionWins()
	{
		using namespace std::chrono_literals;
		TempTree tree("route-sealed-decision");
		const auto database = tree.path / "catalog.sqlite";
		const auto artifacts = tree.path / "artifacts";
		const auto routeRoot = tree.path / "route";
		std::filesystem::create_directory(artifacts);
		std::filesystem::create_directory(routeRoot);
		g_routeRegistrySnapshot = {
			true, 0, true, std::string(kEmptyRouteRegistrySha256)
		};
		g_routeHooksReady = true;
		auto& catalog = CatalogDB::Get();
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		SealTargetScope seal(coordinator);
		HeldCloseAction held;
		Check(
			StartReady(
				catalog,
				RouteCaptureRunConfig(database, artifacts, routeRoot)),
			"sealed decision run did not start");
		const auto runId = catalog.GetStats().generatedRunId;
		RecordRouteEvidence(catalog);

		std::atomic<unsigned> closes{ 0 };
		std::promise<void> stopReturned;
		const auto stopped = stopReturned.get_future();
		Check(
			coordinator.Begin([&] {
				closes.fetch_add(1, std::memory_order_relaxed);
				const bool authoritative = catalog.Stop();
				stopReturned.set_value();
				held.Hold();
				return authoritative;
			}),
			"sealed decision coordinator did not begin");
		Check(
			coordinator.ArmCaptureDeadline(90s),
			"sealed decision deadline was not armed");
		time.Advance(91s);
		stopped.wait();

		const auto sealed = coordinator.Snapshot();
		Check(
			sealed.state == route_capture::State::kClosing
				&& !sealed.finalizationTimedOut
				&& !coordinator.TelemetrySnapshot().finalizationTimedOut
				&& closes.load(std::memory_order_relaxed) == 1,
			"the sealed close did not stay closing with a clean decision");
		const auto documents = ReadRouteDocuments(routeRoot);
		const auto routeManifest = ReadRouteManifest(routeRoot);
		const auto runManifest = ReadRunManifest(artifacts, runId);
		Check(
			routeManifest.starts_with(
				"{\"authority_reasons\":[],"
				"\"capture_authoritative\":true"),
			"the sealed route manifest lost its clean authority");
		Check(
			runManifest.find("\"authoritative\":true")
				!= std::string::npos,
			"the sealed catalog run manifest lost authority");
		Check(
			SqlInt(
				database,
				"SELECT lifecycle_failure FROM catalog_run_quality")
				== 0,
			"a clean sealed close persisted a lifecycle failure");

		time.Advance(route_capture::kFinalizationBudget + 1s);
		auto late = std::async(std::launch::async, [&] {
			return coordinator.WaitForTerminal(
				route_capture::kFinalizationBudget);
		});
		while (late.wait_for(0s) != std::future_status::ready) {
			std::this_thread::yield();
			time.Advance(route_capture::kFinalizationBudget);
		}
		Check(
			!late.get(),
			"a late bounded wait reported terminal while the close was held");
		coordinator.RequestClose(
			route_capture::CloseReason::kProcessExit);
		const auto raced = coordinator.Snapshot();
		Check(
			raced.state == route_capture::State::kClosing
				&& !raced.finalizationTimedOut
				&& !coordinator.TelemetrySnapshot().finalizationTimedOut
				&& closes.load(std::memory_order_relaxed) == 1,
			"a late race latched a timeout after the decision was sealed");
		Check(
			ReadRouteDocuments(routeRoot) == documents
				&& ReadRouteManifest(routeRoot) == routeManifest
				&& ReadRunManifest(artifacts, runId) == runManifest
				&& SqlInt(
					   database,
					   "SELECT lifecycle_failure FROM catalog_run_quality")
					== 0,
			"a late race mutated sealed output or durable quality");

		held.Release();
		Check(
			coordinator.WaitForTerminal(kTestWaitBudget),
			"the held close did not finalize after release");
		const auto terminal = coordinator.Snapshot();
		Check(
			terminal.state == route_capture::State::kFinalizedInert
				&& terminal.authoritative
				&& !terminal.finalizationTimedOut
				&& !coordinator.TelemetrySnapshot().finalizationTimedOut
				&& coordinator.TelemetrySnapshot().authoritative
				&& closes.load(std::memory_order_relaxed) == 1,
			"the released close lost its sealed clean authority");
		Check(
			!coordinator.SealFinalizationDecision(),
			"a sealed clean decision reported a timeout after completion");
		Check(
			ReadRouteManifest(routeRoot) == routeManifest
				&& ReadRouteDocuments(routeRoot) == documents
				&& SqlInt(
					   database,
					   "SELECT lifecycle_failure FROM catalog_run_quality")
					== 0,
			"terminal state changed the sealed route output or quality");
	}

	// Route artifacts publish before catalog persistence, so the two roots are never atomic.
	void TestRouteAndCatalogFailureWindow()
	{
		using namespace std::chrono_literals;
		const std::pair<const char*, int> faults[] = {
			{ "persistence", 0 },
			{ "checkpoint", 1 },
			{ "publication", 2 }
		};
		for (const auto& [name, kind] : faults) {
			TempTree tree(std::string("route-catalog-fault-") + name);
			const auto database = tree.path / "catalog.sqlite";
			const auto artifacts = tree.path / "artifacts";
			const auto routeRoot = tree.path / "route";
			std::filesystem::create_directory(artifacts);
			std::filesystem::create_directory(routeRoot);
			RouteCaptureSeamScope seams;
			ManualTime time;
			route_capture::Coordinator coordinator(time);
			SealTargetScope seal(coordinator);
			auto& catalog = CatalogDB::Get();
			auto config =
				RouteCaptureRunConfig(database, artifacts, routeRoot);
			config.finalizationFaults.persistence = kind == 0;
			config.finalizationFaults.checkpoint = kind == 1;
			config.finalizationFaults.publication = kind == 2;
			Check(
				StartReady(catalog, config),
				"route capture fault run did not start");
			const auto runId = catalog.GetStats().generatedRunId;
			const auto runScope =
				" WHERE generated_run_id='" + runId + "'";
			RecordRouteEvidence(catalog);

			std::atomic<unsigned> closes{ 0 };
			Check(
				coordinator.Begin([&] {
					closes.fetch_add(1, std::memory_order_relaxed);
					return catalog.Stop();
				}),
				"route capture fault coordinator did not begin");
			Check(
				coordinator.ArmCaptureDeadline(90s),
				"route capture fault deadline was not armed");
			time.Advance(91s);
			Check(
				coordinator.WaitForTerminal(kTestWaitBudget),
				"route capture fault close did not finalize");
			const auto status = coordinator.Snapshot();
			Check(
				status.state == route_capture::State::kFinalizedInert
					&& !status.authoritative
					&& !status.finalizationTimedOut
					&& closes.load(std::memory_order_relaxed) == 1,
				"a catalog finalization fault still reported run authority");

			const auto authority = ReadRouteAuthorityFacts(routeRoot);
			Check(
				authority.row
					&& authority.routeManifest
					&& authority.MembershipAuthoritative(),
				"a catalog fault rewrote route-local capture authority");
			const auto routeManifest = ReadRouteManifest(routeRoot);
			Check(
				routeManifest.find("\"lifecycle\"") == std::string::npos
					&& routeManifest.find("\"authoritative\"")
						== std::string::npos
					&& routeManifest.find("\"manifest_published\"")
						== std::string::npos,
				"route-local output asserted enclosing catalog authority");
			Check(
				!std::filesystem::exists(
					artifacts / "runs" / runId / "manifest.v1.json"),
				"a faulted catalog run exposed a contract manifest");
			Check(
				PublishedTuple(catalog.GetStats())
					== PublishedTuple(CatalogDB::Stats{}),
				"a failed close left a live public tuple");
			Check(
				SqlText(
					database,
					("SELECT lifecycle FROM catalog_runs" + runScope)
						.c_str())
					== "abandoned",
				"a faulted catalog run was not conservatively abandoned");
			Check(
				SqlInt(
					database,
					("SELECT COUNT(*) FROM catalog_runs" + runScope
					 + " AND (lifecycle='finalized' OR authoritative=1 "
					   "OR manifest_published=1 OR publication_pending=1)")
						.c_str())
					== 0,
				"a faulted catalog run claimed a completed authoritative lifecycle");
			Check(
				SqlInt(
					database,
					("SELECT lifecycle_failure FROM catalog_run_quality"
					 + runScope)
						.c_str())
					== 1,
				"a faulted catalog run lost its exact lifecycle failure");
		}
	}

	void TestRouteCaptureDurationSetting()
	{
		using namespace std::chrono_literals;
		Check(
			route_capture::kDefaultCaptureSeconds == 90
				&& route_capture::kMinCaptureSeconds == 1
				&& route_capture::kMaxCaptureSeconds == 3600,
			"the persisted capture duration contract changed");
		Check(
			!route_capture::IsValidCaptureSeconds(-1)
				&& !route_capture::IsValidCaptureSeconds(0)
				&& !route_capture::IsValidCaptureSeconds(3601)
				&& route_capture::IsValidCaptureSeconds(1)
				&& route_capture::IsValidCaptureSeconds(
					route_capture::kDefaultCaptureSeconds)
				&& route_capture::IsValidCaptureSeconds(3600),
			"capture duration validation accepted an out-of-range window");
		ManualTime time;
		route_capture::Coordinator coordinator(time);
		Check(
			coordinator.Begin([] { return true; }),
			"duration coordinator did not begin");
		Check(
			!coordinator.ArmCaptureDeadline(0s)
				&& !coordinator.ArmCaptureDeadline(3601s)
				&& !coordinator.Snapshot().deadlineArmed,
			"an invalid capture window armed the deadline");
		time.Advance(2h);
		Check(
			coordinator.Snapshot().state
				== route_capture::State::kCapturing,
			"a rejected capture window still closed the run");
	}

	void WriteSyntheticRoutePe(
		const std::filesystem::path& a_path,
		bool a_duplicateRuntimeFunction)
	{
		std::vector<std::byte> bytes(0x700);
		IMAGE_DOS_HEADER dos{};
		dos.e_magic = IMAGE_DOS_SIGNATURE;
		dos.e_lfanew = 0x80;
		std::memcpy(bytes.data(), &dos, sizeof(dos));
		const DWORD signature = IMAGE_NT_SIGNATURE;
		std::memcpy(bytes.data() + 0x80, &signature, sizeof(signature));
		IMAGE_FILE_HEADER file{};
		file.Machine = IMAGE_FILE_MACHINE_AMD64;
		file.NumberOfSections = 2;
		file.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
		std::memcpy(
			bytes.data() + 0x84, &file, sizeof(file));
		IMAGE_OPTIONAL_HEADER64 optional{};
		optional.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		optional.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
		optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = {
			0x3000,
			static_cast<DWORD>(
				sizeof(RUNTIME_FUNCTION)
				* (a_duplicateRuntimeFunction ? 2 : 1))
		};
		std::memcpy(
			bytes.data() + 0x84 + sizeof(file),
			&optional,
			sizeof(optional));
		const auto sectionOffset =
			0x84 + sizeof(file) + sizeof(optional);
		IMAGE_SECTION_HEADER text{};
		text.VirtualAddress = 0x1000;
		text.SizeOfRawData = 0x100;
		text.PointerToRawData = 0x400;
		IMAGE_SECTION_HEADER pdata{};
		pdata.VirtualAddress = 0x3000;
		pdata.SizeOfRawData = 0x100;
		pdata.PointerToRawData = 0x500;
		std::memcpy(
			bytes.data() + sectionOffset, &text, sizeof(text));
		std::memcpy(
			bytes.data() + sectionOffset + sizeof(text),
			&pdata,
			sizeof(pdata));
		for (std::size_t index = 0; index < 16; ++index)
			bytes[0x400 + index] = static_cast<std::byte>(index);
		const RUNTIME_FUNCTION function{
			0x1000,
			0x1010,
			0
		};
		std::memcpy(
			bytes.data() + 0x500,
			&function,
			sizeof(function));
		if (a_duplicateRuntimeFunction) {
			std::memcpy(
				bytes.data() + 0x500 + sizeof(function),
				&function,
				sizeof(function));
		}
		std::ofstream output(a_path, std::ios::binary);
		output.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		Check(static_cast<bool>(output), "synthetic PE write failed");
	}

	void TestRoutePeCodeIdentity()
	{
		TempTree tree("route-pe");
		const auto valid = tree.path / "valid.dll";
		WriteSyntheticRoutePe(valid, false);
		RouteCodeIdentity identity;
		std::string error;
		Check(
			ResolveRouteCodeIdentity(
				valid,
				0x10000000,
				0x10001000,
				"SyntheticHook",
				identity,
				error),
			"valid synthetic PE identity rejected: " + error);
		Check(
			identity.rva == 0x1000
				&& identity.codeRange.startRva == 0x1000
				&& identity.codeRange.byteLength == 16
				&& IsLowerHexDigest(identity.codeSha256, 64),
			"synthetic PE identity fields are wrong");
		const auto duplicate = tree.path / "duplicate.dll";
		WriteSyntheticRoutePe(duplicate, true);
		Check(
			!ResolveRouteCodeIdentity(
				duplicate,
				0x20000000,
				0x20001000,
				"SyntheticHook",
				identity,
				error),
			"duplicate unwind range was accepted");
		Check(
			!ResolveRouteCodeIdentity(
				valid,
				0x10000000,
				0x10002000,
				"SyntheticHook",
				identity,
				error),
			"missing unwind range was accepted");
	}

	void TestCanonicalUtf8()
	{
		auto document = SampleManifest();
		document.runtimeFamily = std::string("\xc3\x28", 2);
		document.blobs.front().relativePath =
			"../injected";
		const auto json = BuildCanonicalManifest(document);
		Check(IsValidUtf8(json), "canonical manifest emitted invalid UTF-8");
		Check(
			json.find("\xef\xbf\xbd(") != std::string::npos,
			"invalid UTF-8 was not replaced deterministically");

		TempTree tree("invalid-json");
		const std::string invalidJson = std::string("{\"x\":\"", 6)
			+ std::string("\xc3\x28", 2) + "\"}\n";
		Check(
			!PublishManifest(
				tree.path, "valid-run-id", invalidJson).success,
			"publication accepted invalid UTF-8 JSON");
	}

	void Run(std::string_view a_name, void (*a_test)(), int& a_failures)
	{
		try {
			a_test();
			std::cout << "[pass] " << a_name << '\n';
		} catch (const std::exception& error) {
			++a_failures;
			std::cerr << "[fail] " << a_name << ": " << error.what() << '\n';
		}
	}
}

int main()
{
	int failures = 0;
	Run("SHA vectors", &TestHashes, failures);
	Run("run policy", &TestRunPolicy, failures);
	Run("stream-output identity", &TestStreamOutputIdentity, failures);
	Run("manifest determinism", &TestManifestDeterminism, failures);
	Run("blob publication", &TestBlobPublication, failures);
	Run(
		"directory create convergence",
		&TestDirectoryCreateConvergence,
		failures);
	Run("reparse defense", &TestReparseDefense, failures);
	Run(
		"immutable manifest publication",
		&TestImmutableManifestPublication,
		failures);
	Run("broker classification", &TestBrokerClassification, failures);
	Run(
		"creation attribution policy",
		&TestCreationAttributionPolicy,
		failures);
	Run("run aggregation and lifecycle", &TestRunAggregationAndLifecycle, failures);
	Run("legacy migration unscoped", &TestLegacyMigrationUnscoped, failures);
	Run(
		"raw export association completeness",
		&TestRawExportAssociationCompleteness,
		failures);
	Run("abandoned recovery", &TestAbandonedRecovery, failures);
	Run("schema incompatibility", &TestSchemaIncompatibility, failures);
	Run("failure authority gates", &TestFailureAuthorityGates, failures);
	Run("bounds and queue quality", &TestBoundsAndQueueQuality, failures);
	Run("producer lease stop race", &TestProducerLeaseStopRace, failures);
	Run(
		"pixel admission allocation failure",
		&TestPixelAdmissionAllocationFailure,
		failures);
	Run("finalization fault seams", &TestFinalizationFaultSeams, failures);
	Run("publication crash recovery", &TestPublicationCrashRecovery, failures);
	Run("hook coverage authority", &TestHookCoverageAuthority, failures);
	Run("orderly finalizer gate", &TestOrderlyFinalizerGate, failures);
	Run("hook coverage reduction", &TestHookCoverageReduction, failures);
	Run("outcome semantics and ordering", &TestOutcomeSemanticsAndOrdering, failures);
	Run("environment empty values", &TestEnvironmentEmptyValues, failures);
	Run("guarded input copies", &TestGuardedInputCopies, failures);
	Run(
		"stream-output quality classification",
		&TestStreamOutputQualityClassification,
		failures);
	Run("malformed v2 rollback", &TestMalformedV2Rollback, failures);
	Run(
		"legacy v3 observation upgrade",
		&TestLegacyV3ObservationUpgrade,
		failures);
	Run("HRESULT overflow", &TestHresultOverflow, failures);
	Run("attribution identity kinds", &TestAttributionIdentityKinds, failures);
	Run("pixel tracker alias lookup", &TestPixelTrackerAliasLookup, failures);
	Run("per-run blob associations", &TestPerRunBlobAssociations, failures);
	Run("injected blob path rejected", &TestInjectedBlobPathRejected, failures);
	Run("route receipt publication", &TestRouteReceiptPublication, failures);
	Run(
		"route finalize run veto",
		&TestRouteFinalizeRunVeto,
		failures);
	Run(
		"route capture disabled by default",
		&TestRouteCaptureDisabledByDefault,
		failures);
	Run(
		"route capture loss and registry violation",
		&TestRouteCaptureLossAndRegistryViolation,
		failures);
	Run("route capture lease race", &TestRouteCaptureLeaseRace, failures);
	Run(
		"route lineage conflict and stale output",
		&TestRouteLineageConflictAndStaleOutput,
		failures);
	Run(
		"route capture coordinator deadline",
		&TestRouteCaptureCoordinatorDeadline,
		failures);
	Run(
		"route capture coordinator coalescing",
		&TestRouteCaptureCoordinatorCoalescing,
		failures);
	Run(
		"route capture coordinator cutoff",
		&TestRouteCaptureCoordinatorCutoff,
		failures);
	Run(
		"ordinary run coordinator close",
		&TestOrdinaryRunCoordinatorClose,
		failures);
	Run(
		"route capture startup rollback",
		&TestRouteCaptureStartupRollback,
		failures);
	Run(
		"startup commit ordering",
		&TestStartupCommitOrdering,
		failures);
	Run(
		"route admission hidden until commit",
		&TestRouteAdmissionHiddenUntilCommit,
		failures);
	Run(
		"route admission closes with generic cutoff",
		&TestRouteAdmissionClosesWithGenericCutoff,
		failures);
	Run(
		"stop exception fails closed",
		&TestStopExceptionFailsClosed,
		failures);
	Run(
		"close busy retains non-service state",
		&TestCloseBusyRetainsNonServiceState,
		failures);
	Run(
		"stop exception with busy close",
		&TestStopExceptionWithBusyClose,
		failures);
	Run(
		"late readiness cannot promote",
		&TestLateReadinessCannotPromote,
		failures);
	Run(
		"late graphics cannot apply",
		&TestLateGraphicsCannotApply,
		failures);
	Run(
		"route begin rejects at shared gate",
		&TestRouteBeginRejectsAtSharedGate,
		failures);
	Run(
		"route token holds outer admission",
		&TestRouteTokenHoldsOuterAdmission,
		failures);
	Run(
		"route publisher presence states",
		&TestRoutePublisherPresenceStates,
		failures);
	Run(
		"route observation publish failure",
		&TestRouteObservationPublishFailure,
		failures);
	Run(
		"late readiness cannot demote",
		&TestLateReadinessCannotDemote,
		failures);
	Run(
		"admitted pre-cutoff update settles before drain",
		&TestAdmittedPreCutoffUpdateSettlesBeforeDrain,
		failures);
	Run(
		"registry frozen at route freeze",
		&TestRegistryFrozenAtRouteFreeze,
		failures);
	Run(
		"sealed context precedes publication",
		&TestSealedContextPrecedesPublication,
		failures);
	Run(
		"lifecycle serialization",
		&TestLifecycleSerialization,
		failures);
	Run(
		"lifecycle start stop interleaving",
		&TestLifecycleStartStopInterleaving,
		failures);
	Run(
		"concurrent stats during lifecycle churn",
		&TestConcurrentStatsDuringLifecycleChurn,
		failures);
	Run(
		"startup failure leaves no run",
		&TestStartupFailureLeavesNoRun,
		failures);
	Run(
		"route capture timeout without a waiter",
		&TestRouteCaptureTimeoutWithoutWaiter,
		failures);
	Run(
		"route capture finalized inert",
		&TestRouteCaptureFinalizedInert,
		failures);
	Run(
		"route capture without exit hooks",
		&TestRouteCaptureWithoutExitHooks,
		failures);
	Run(
		"route capture finalization timeout",
		&TestRouteCaptureFinalizationTimeout,
		failures);
	Run(
		"route capture sealed decision wins",
		&TestRouteCaptureSealedDecisionWins,
		failures);
	Run(
		"route and catalog failure window",
		&TestRouteAndCatalogFailureWindow,
		failures);
	Run(
		"route capture duration setting",
		&TestRouteCaptureDurationSetting,
		failures);
	Run("route PE code identity", &TestRoutePeCodeIdentity, failures);
	Run("canonical UTF-8", &TestCanonicalUtf8, failures);
	return failures == 0 ? 0 : 1;
}
