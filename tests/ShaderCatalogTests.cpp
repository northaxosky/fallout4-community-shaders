#include "AttributionPolicy.h"
#include "CatalogDB.h"
#include "HookCoverage.h"
#include "OrderlyExit.h"
#include "PixelShaderTracker.h"
#include "Provenance.h"
#include "Render/PixelShaderSwapBroker.h"

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
#include <stdexcept>
#include <string>
#include <thread>
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
		orderly_exit::FinalizerGate gate;
		std::atomic<unsigned> winners{ 0 };
		std::vector<std::thread> threads;
		for (unsigned index = 0; index < 16; ++index) {
			threads.emplace_back([&] {
				if (gate.TryBegin())
					winners.fetch_add(1, std::memory_order_relaxed);
			});
		}
		for (auto& thread : threads)
			thread.join();
		Check(winners.load(std::memory_order_relaxed) == 1,
			"orderly finalizer gate ran more than once");
		Check(gate.Started(), "orderly finalizer gate lost started state");

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
				snapshot, error)
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
		Check(catalog.Stop(), "disabled route capture run did not stop");
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
				snapshot, error)
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
				snapshot, error),
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
				snapshot, error),
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
				snapshot, error)
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
				snapshot, error),
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
				snapshot, error),
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
				snapshot, freezeError);
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
	Run("route PE code identity", &TestRoutePeCodeIdentity, failures);
	Run("canonical UTF-8", &TestCanonicalUtf8, failures);
	return failures == 0 ? 0 : 1;
}
