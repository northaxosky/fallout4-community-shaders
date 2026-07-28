#pragma once

#include "Provenance.h"
#include "Sha1.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace cs::features::catalog
{
	struct DbConfig
	{
		struct RouteCaptureConfig
		{
			bool requested = false;
			std::filesystem::path outputRoot;
			RouteProducerIdentity producer;
			RouteRuntimeIdentity runtime;
			RouteCaptureScope scope;
		};

		struct FinalizationFaults
		{
			bool persistence = false;
			bool checkpoint = false;
			bool publication = false;
		};

		std::string catalogPath;
		std::uint32_t flushIntervalMs = 5000;
		bool subclassAttributionRequested = false;
		bool subclassAttributionEnabled = false;
		std::optional<RunPolicy> policyOverride;
		std::optional<std::filesystem::path> artifactRootOverride;
		RouteCaptureConfig routeCapture;
#ifdef FO4CS_SHADER_CATALOG_TESTING
		bool orderlyFinalizerReadyForTesting = false;
#endif
		FinalizationFaults finalizationFaults;
	};

	struct RuntimeIdentity
	{
		std::string runtimeFamily = "unknown";
		std::optional<std::string> runtimeVersion;
		std::string pluginVersion = "unknown";
		std::string pluginBuildDescribe = "unknown";
		std::string pluginGitIdentity = "unknown";
	};

	class CatalogDB
	{
	public:
		class ProducerLease
		{
		public:
			ProducerLease() = default;
			~ProducerLease();
			ProducerLease(ProducerLease&& a_other) noexcept;
			ProducerLease& operator=(ProducerLease&& a_other) noexcept;
			ProducerLease(const ProducerLease&) = delete;
			ProducerLease& operator=(const ProducerLease&) = delete;
			explicit operator bool() const noexcept { return _owner != nullptr; }
			void Reset() noexcept;

		private:
			friend class CatalogDB;
			explicit ProducerLease(CatalogDB* a_owner) noexcept : _owner(a_owner) {}
			CatalogDB* _owner = nullptr;
		};

		static CatalogDB& Get();

		bool Start(const DbConfig& a_config, RuntimeIdentity a_identity);
		bool Stop();

		ProducerLease TryAcquireProducerLease() noexcept;
		bool TryBeginProducerAdmission() noexcept;
		void EndProducerAdmission() noexcept;
		std::uint64_t NextSequence() noexcept;
		void EnqueueObservation(
			ObservationOutcome a_observation,
			const ProducerLease* a_lease = nullptr) noexcept;
		void EnqueueObservationAdmitted(
			ObservationOutcome a_observation) noexcept;
		void EnqueueAttribution(
			const Sha1Result& a_sha,
			const char* a_subclassName,
			std::uint32_t a_techniqueBits,
			AttributionKind a_kind,
			AttributionObjectKind a_objectKind,
			const ProducerLease* a_lease = nullptr) noexcept;
		void EnqueueAttributionAdmitted(
			const Sha1Result& a_sha,
			const char* a_subclassName,
			std::uint32_t a_techniqueBits,
			AttributionKind a_kind,
			AttributionObjectKind a_objectKind) noexcept;
		void SetGraphicsFacts(
			std::optional<std::string> a_adapter,
			std::optional<std::string> a_featureLevel);
		bool MarkOrderlyFinalizerReady() noexcept;
		void MarkHookCoverageReady() noexcept;
		void RecordHookObserverGap() noexcept;
		void RecordAllocationFailure() noexcept;
		RouteCaptureAdmission BeginRouteCreate(
			const RouteCreateInput& a_input) noexcept;
		RouteCreateCommitResult CompleteRouteCreate(
			RouteCaptureAdmission&& a_admission,
			const RouteCreateOutcome& a_outcome) noexcept;
		RouteBindAdmission BeginRouteBind() noexcept;
		bool RecordRouteBind(
			RouteBindAdmission&& a_admission,
			const std::shared_ptr<RouteCaptureRecordState>& a_record,
			std::optional<RouteBindSnapshot> a_routeSnapshot) noexcept;
		void ReleaseRouteBindReservation(
			const std::shared_ptr<RouteCaptureRecordState>& a_record) noexcept;
		[[nodiscard]] bool RouteCaptureActive() const noexcept;

		struct Stats
		{
			std::string generatedRunId;
			std::optional<std::string> externalRunId;
			std::optional<std::string> scenarioId;
			std::string lifecycle = "inactive";
			bool authoritative = false;
			bool rawExportRequested = false;
			bool rawExportComplete = false;
			bool writerDrained = false;
			bool hookCoverageReady = false;
			bool orderlyFinalizerReady = false;
			bool routeCaptureRequested = false;
			bool routeCaptureActive = false;
			std::uint64_t attempts = 0;
			std::uint64_t successes = 0;
			std::uint64_t failures = 0;
			std::uint64_t uniqueObservations = 0;
			std::uint64_t uniqueContents = 0;
			std::uint64_t attributionEvents = 0;
			std::uint64_t reflected = 0;
			std::uint64_t attributedPs = 0;
			std::uint64_t totalPs = 0;
			QualityCounters quality;
		};
		Stats GetStats() const noexcept;

		static bool InspectSchemaVersion(
			const std::filesystem::path& a_path,
			int& a_version,
			std::string& a_error);

	private:
		enum class EventKind
		{
			kObservation,
			kAttribution
		};

		struct Event
		{
			EventKind kind = EventKind::kObservation;
			ObservationOutcome observation;
			std::array<std::uint8_t, 20> attributionSha1{};
			std::string attributionSubclass;
			std::uint32_t attributionTechnique = 0;
			bool attributionHasTechnique = false;
			std::uint32_t attributionThreadId = 0;
			std::int64_t attributionQpc = 0;
			AttributionKind attributionKind = AttributionKind::kCreationContext;
			AttributionObjectKind attributionObjectKind =
				AttributionObjectKind::kStock;

			Event() = default;
			Event(const Event&) = delete;
			Event& operator=(const Event&) = delete;
			Event(Event&&) noexcept = default;
			Event& operator=(Event&&) noexcept = default;
		};

		struct Cell
		{
			std::atomic<std::uint64_t> sequence{ 0 };
			Event data{};
		};

		struct PendingEnrichment
		{
			std::string sha256;
			std::string sha1;
			std::size_t size = 0;
			std::unique_ptr<std::byte[]> bytecode;
		};

		CatalogDB() = default;
		CatalogDB(const CatalogDB&) = delete;
		CatalogDB& operator=(const CatalogDB&) = delete;

		void ResetState();
		bool OpenAndBootstrap();
		bool MigrateSchema();
		bool RecoverAbandonedRuns();
		bool InsertRun();
		bool PrepareStatements();
		bool FinalizeStatements();
		bool Enqueue(Event a_event, bool a_admitted = false) noexcept;
		void EnqueueObservationImpl(
			ObservationOutcome a_observation,
			bool a_admitted) noexcept;
		void EnqueueAttributionImpl(
			const Sha1Result& a_sha,
			const char* a_subclassName,
			std::uint32_t a_techniqueBits,
			AttributionKind a_kind,
			AttributionObjectKind a_objectKind,
			bool a_admitted) noexcept;
		void ReleaseProducerLease() noexcept;
		void WakeWriter() noexcept;
		bool RingHasReady() noexcept;
		void WriterLoop();
		std::vector<Event> DequeueBatch();
		bool PersistBatch(std::vector<Event>& a_batch);
		bool PersistObservation(const ObservationOutcome& a_observation);
		bool PersistAttribution(const Event& a_event);
		bool PersistLegacyObservation(const ObservationOutcome& a_observation);
		bool PersistLegacyAttribution(const Event& a_event);
		void ProcessDeferredWork(std::vector<Event>& a_batch);
		void EnrichOne();
		void ExportObservation(const ObservationOutcome& a_observation);
		bool UpdateContentShape(const PendingEnrichment& a_item);
		bool PersistQuality();
		bool RefreshStats();
		bool CheckRawExportAssociations(bool& a_complete);
		bool PersistFinalRunState(
			const std::string& a_endedAt,
			bool a_rawExportComplete,
			bool a_authoritative,
			std::string_view a_manifestSha256,
			std::size_t a_manifestSize);
		bool RepairFailedPublication(const std::string& a_endedAt);
		bool FinalizeLegacySession(const std::string& a_endedAt);
		bool LoadManifestDocument(
			const std::string& a_endedAt,
			ManifestDocument& a_document);
		bool RecoverPublicationWindows();
		bool Checkpoint(int a_mode, const char* a_name);

		std::string ResolveModule(std::uintptr_t a_address);
		std::string FormatStack(const std::array<std::uintptr_t, 4>& a_frames);
		QualityCounters QualitySnapshot() const noexcept;
		void StoreStatsIdentity();

		DbConfig _config{};
		RuntimeIdentity _identity{};
		RunPolicy _policy{};
		std::filesystem::path _artifactRoot;
		std::string _artifactRootFingerprint;
		std::string _generatedRunId;
		std::string _startedAt;
		std::unique_ptr<StockRuntimeRoutePublisher> _routePublisher;

		sqlite3* _db = nullptr;
		sqlite3_stmt* _upsertContent = nullptr;
		sqlite3_stmt* _upsertObservation = nullptr;
		sqlite3_stmt* _upsertHresult = nullptr;
		sqlite3_stmt* _upsertHresultOverflow = nullptr;
		sqlite3_stmt* _upsertAttribution = nullptr;
		sqlite3_stmt* _upsertRunBlob = nullptr;
		sqlite3_stmt* _updateContentShape = nullptr;
		sqlite3_stmt* _insertLegacyShader = nullptr;
		sqlite3_stmt* _upsertLegacyAttribution = nullptr;
		sqlite3_stmt* _updateLegacyShape = nullptr;
		sqlite3_stmt* _updateLegacySession = nullptr;
		sqlite3_stmt* _updateQuality = nullptr;

		std::thread _writer;
		std::atomic<bool> _running{ false };
		std::atomic<bool> _accepting{ false };
		static constexpr std::uint64_t kProducerAdmissionClosed = 1ull << 63;
		static constexpr std::uint64_t kProducerCountMask =
			kProducerAdmissionClosed - 1;
		std::atomic<std::uint64_t> _producerAdmission{
			kProducerAdmissionClosed
		};
		std::condition_variable _wakeWriter;
		std::mutex _wakeMutex;

		// AE startup can enqueue more than 4096 events before SQLite catches up.
		static constexpr std::size_t kCapacity = 8192;
		static_assert((kCapacity & (kCapacity - 1)) == 0);
		std::array<Cell, kCapacity> _ring{};
		std::atomic<std::uint64_t> _enqueuePosition{ 0 };
		std::atomic<std::uint64_t> _dequeuePosition{ 0 };
		std::atomic<std::uint64_t> _nextSequence{ 1 };

		std::deque<PendingEnrichment> _enrichmentQueue;
		std::unordered_set<std::string> _queuedEnrichment;
		std::unordered_set<std::string> _enrichedContents;
		std::size_t _retainedBytes = 0;
		std::unordered_set<std::string> _exportedContents;
		std::unordered_map<std::string, unsigned> _exportAttempts;
		std::unordered_map<std::uintptr_t, std::string> _moduleCache;

		mutable std::mutex _identityMutex;
		std::optional<std::string> _graphicsAdapter;
		std::optional<std::string> _graphicsFeatureLevel;
		Stats _statsIdentity{};

		std::atomic<std::uint64_t> _statAttempts{ 0 };
		std::atomic<std::uint64_t> _statSuccesses{ 0 };
		std::atomic<std::uint64_t> _statFailures{ 0 };
		std::atomic<std::uint64_t> _statUniqueObservations{ 0 };
		std::atomic<std::uint64_t> _statUniqueContents{ 0 };
		std::atomic<std::uint64_t> _statAttributionEvents{ 0 };
		std::atomic<std::uint64_t> _statReflected{ 0 };
		std::atomic<std::uint64_t> _statAttributedPs{ 0 };
		std::atomic<std::uint64_t> _statTotalPs{ 0 };
		std::atomic<int> _lifecycle{ 0 };
		std::atomic<bool> _authoritative{ false };
		std::atomic<bool> _rawExportComplete{ false };
		std::atomic<bool> _writerDrained{ false };
		std::atomic<bool> _hookCoverageReady{ false };
		std::atomic<bool> _orderlyFinalizerReady{ false };
		std::atomic<bool> _writerPersistenceHealthy{ true };

		std::atomic<std::uint64_t> _qualityQueueOverflow{ 0 };
		std::atomic<std::uint64_t> _qualityMalformedBytecode{ 0 };
		std::atomic<std::uint64_t> _qualityUnsupportedSize{ 0 };
		std::atomic<std::uint64_t> _qualityAllocationFailure{ 0 };
		std::atomic<std::uint64_t> _qualityCopyFailure{ 0 };
		std::atomic<std::uint64_t> _qualityHashFailure{ 0 };
		std::atomic<std::uint64_t> _qualityMetadataTruncated{ 0 };
		std::atomic<std::uint64_t> _qualityHresultOverflow{ 0 };
		std::atomic<std::uint64_t> _qualityDbWriteFailure{ 0 };
		std::atomic<std::uint64_t> _qualityRawExportFailure{ 0 };
		std::atomic<std::uint64_t> _qualityManifestFailure{ 0 };
		std::atomic<std::uint64_t> _qualityHookObserverGap{ 0 };
		std::atomic<std::uint64_t> _qualityWriterDrainFailure{ 0 };
		std::atomic<std::uint64_t> _qualityLifecycleFailure{ 0 };
		std::atomic<std::uint64_t> _qualityConfigurationFailure{ 0 };
	};
}
