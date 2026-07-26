#pragma once

#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cs::features::catalog
{
	inline constexpr int kCatalogSchemaVersion = 3;
	inline constexpr int kManifestSchemaVersion = 2;
	inline constexpr std::string_view kManifestSchema = "fo4cs.shader-catalog-run";
	inline constexpr std::size_t kMaxShaderBytecodeBytes = 16u * 1024u * 1024u;
	inline constexpr std::size_t kMaxCatalogIdentifierBytes = 128;
	inline constexpr std::size_t kMaxSemanticBytes = 256;
	inline constexpr UINT kMaxStreamOutputEntries = 1024;
	inline constexpr UINT kMaxStreamOutputStrides = D3D11_SO_BUFFER_SLOT_COUNT;

	enum class BytecodeState
	{
		kNull,
		kEmpty,
		kExact,
		kUnsupportedSize,
		kAllocationFailure,
		kCopyFailure,
		kHashFailure
	};

	enum class AttributionKind
	{
		kCreationContext,
		kObservedBinding,
		kTechniqueMapAssociation
	};

	enum class StreamOutputState
	{
		kNotApplicable,
		kExact,
		kUnsupportedSize,
		kAllocationFailure,
		kCopyFailure,
		kHashFailure,
		kMetadataTruncated
	};

	enum class AttributionObjectKind
	{
		kStock,
		kReplacementUnknown,
		kOriginatingStock,
		kSubmissionNoObject
	};

	struct ContentDigest
	{
		std::array<std::uint8_t, 20> sha1{};
		std::array<std::uint8_t, 32> sha256{};
	};

	struct StreamOutputIdentity
	{
		bool present = false;
		bool valid = true;
		bool metadataTruncated = false;
		bool copyFailure = false;
		StreamOutputState state = StreamOutputState::kNotApplicable;
		std::string digestSha256;
		std::string declarationState = "not_applicable";
		std::uint32_t declarationCount = 0;
		std::string stridesState = "not_applicable";
		std::uint32_t strideCount = 0;
		std::uint32_t rasterizedStream = 0;
	};

	struct PreparedObservation
	{
		char stage = 0;
		BytecodeState bytecodeState = BytecodeState::kNull;
		std::size_t submittedSize = 0;
		std::optional<ContentDigest> digest;
		std::unique_ptr<std::byte[]> bytecode;
		StreamOutputIdentity streamOutput;
		std::uint64_t sequence = 0;
		std::int64_t qpc = 0;
		std::uint32_t threadId = 0;
		std::uintptr_t sourceVa = 0;
		std::array<std::uintptr_t, 4> stackFrames{};
	};

	struct ObservationOutcome
	{
		PreparedObservation prepared;
		std::int32_t hresult = 0;
		bool outputRequested = false;
		bool outputNonNull = false;
		bool resolverInvoked = false;
		bool resolverReportedReplacement = false;
		bool finalIsStock = false;
		bool finalIsReplacement = false;
		bool finalIsNull = true;
	};

	struct EnvironmentValues
	{
		std::optional<std::string> evidenceMode;
		std::optional<std::string> externalRunId;
		std::optional<std::string> scenarioId;
		std::optional<std::string> configId;
		std::optional<std::string> sourceId;
		std::optional<std::string> corpusRoot;
	};

	struct RunPolicy
	{
		bool evidenceMode = false;
		bool environmentValid = true;
		bool evidenceIdsSatisfied = true;
		bool rawExportRequested = false;
		bool exportRootValid = false;
		std::optional<std::string> externalRunId;
		std::optional<std::string> scenarioId;
		std::optional<std::string> configId;
		std::optional<std::string> sourceId;
		std::optional<std::filesystem::path> corpusRoot;
		std::vector<std::string> errors;
	};

	struct QualityCounters
	{
		std::uint64_t queueOverflow = 0;
		std::uint64_t malformedBytecode = 0;
		std::uint64_t unsupportedSize = 0;
		std::uint64_t allocationFailure = 0;
		std::uint64_t copyFailure = 0;
		std::uint64_t hashFailure = 0;
		std::uint64_t metadataTruncated = 0;
		std::uint64_t dbWriteFailure = 0;
		std::uint64_t rawExportFailure = 0;
		std::uint64_t manifestFailure = 0;
		std::uint64_t hookObserverGap = 0;
		std::uint64_t writerDrainFailure = 0;
		std::uint64_t lifecycleFailure = 0;
		std::uint64_t configurationFailure = 0;

		bool HasLossOrFailure() const noexcept;
	};

	struct PublicationResult
	{
		bool success = false;
		bool alreadyExisted = false;
		std::filesystem::path relativePath;
		std::string error;
	};

	struct ManifestShape
	{
		std::optional<std::string> profile;
		std::optional<int> cbCount;
		std::optional<int> srvCount;
		std::optional<int> uavCount;
		std::optional<int> samplerCount;
		std::optional<int> outputCount;
		std::optional<int> inputCount;
		std::optional<int> inputHasPositionOnly;
		std::optional<int> instructionCount;
		std::optional<int> sampleCallCount;
		std::optional<std::string> inputSignatureSummary;
		std::optional<std::string> outputSignatureSummary;
		std::optional<std::string> resourceSummary;
	};

	struct ManifestBlob
	{
		std::string sha256;
		std::string sha1;
		std::uint64_t sizeBytes = 0;
		std::optional<std::string> relativePath;
		ManifestShape shape;
	};

	struct ManifestHresult
	{
		std::int32_t code = 0;
		std::uint64_t count = 0;
	};

	struct ManifestObservation
	{
		std::string key;
		std::string stage;
		std::optional<std::string> sha256;
		std::optional<std::string> sha1;
		std::string bytecodeState;
		std::uint64_t submittedSize = 0;
		StreamOutputIdentity streamOutput;
		std::uint64_t attempts = 0;
		std::uint64_t successes = 0;
		std::uint64_t failures = 0;
		std::uint64_t outputRequests = 0;
		std::uint64_t nullOutputs = 0;
		std::uint64_t rawOutputNonNull = 0;
		std::uint64_t resolverInvocations = 0;
		std::uint64_t resolverReportedReplacements = 0;
		std::uint64_t finalStock = 0;
		std::uint64_t finalReplacement = 0;
		std::uint64_t finalNull = 0;
		std::optional<std::string> replacementSha256;
		std::uint64_t firstSequence = 0;
		std::uint64_t lastSequence = 0;
		std::int64_t firstQpc = 0;
		std::int64_t lastQpc = 0;
		std::uint32_t firstThreadId = 0;
		std::optional<std::string> firstModule;
		std::optional<std::string> firstStack;
		std::uint64_t otherHresultCount = 0;
		bool hresultDetailsTruncated = false;
		std::vector<ManifestHresult> failedHresults;
	};

	struct ManifestAttribution
	{
		std::optional<std::string> sha1;
		std::optional<std::string> originatingStockSha1;
		std::optional<std::string> sha256;
		std::string subclassName;
		std::optional<std::uint32_t> techniqueBits;
		std::string attributionKind;
		std::string objectKind;
		std::uint64_t count = 0;
	};

	struct ManifestCounters
	{
		std::uint64_t attempts = 0;
		std::uint64_t successes = 0;
		std::uint64_t failures = 0;
		std::uint64_t uniqueObservations = 0;
		std::uint64_t uniqueContents = 0;
		std::uint64_t attributionEvents = 0;
	};

	struct ManifestDocument
	{
		std::string producerVersion;
		std::string producerBuildDescribe;
		std::string producerGitIdentity;
		std::string generatedRunId;
		std::optional<std::string> externalRunId;
		std::optional<std::string> scenarioId;
		std::optional<std::string> configId;
		std::optional<std::string> sourceId;
		std::string runtimeFamily;
		std::optional<std::string> runtimeVersion;
		std::uint32_t processId = 0;
		std::optional<std::string> graphicsAdapter;
		std::optional<std::string> graphicsFeatureLevel;
		std::optional<std::uint32_t> resolutionWidth;
		std::optional<std::uint32_t> resolutionHeight;
		bool evidenceMode = false;
		bool evidenceIdsSatisfied = false;
		bool rawExportRequested = false;
		std::uint32_t writerFlushIntervalMs = 5000;
		bool subclassAttributionRequested = false;
		bool subclassAttributionEnabled = false;
		bool rawExportComplete = false;
		std::string lifecycle;
		std::string startedAt;
		std::optional<std::string> endedAt;
		bool writerDrained = false;
		bool manifestPublished = false;
		bool hookCoverageReady = false;
		bool orderlyFinalizerReady = false;
		bool authoritative = false;
		QualityCounters quality;
		ManifestCounters counters;
		std::vector<ManifestBlob> blobs;
		std::vector<ManifestObservation> observations;
		std::vector<ManifestAttribution> attributions;
	};

	std::string_view BytecodeStateName(BytecodeState a_state) noexcept;
	std::string_view StreamOutputStateName(
		StreamOutputState a_state) noexcept;
	bool ComputeDigests(const void* a_data, std::size_t a_size, ContentDigest& a_result) noexcept;
	std::string HexLower(const std::uint8_t* a_data, std::size_t a_size);
	bool IsLowerHexDigest(std::string_view a_value, std::size_t a_size) noexcept;
	std::optional<std::string> GenerateUuidV4() noexcept;
	RunPolicy ParseRunPolicy(const EnvironmentValues& a_values);
	RunPolicy ReadRunPolicyFromEnvironment();

	PreparedObservation PrepareObservation(
		char a_stage,
		const void* a_bytecode,
		std::size_t a_bytecodeSize,
		std::uint64_t a_sequence,
		ULONG a_stackFramesToSkip = 2) noexcept;
	StreamOutputIdentity PrepareStreamOutputIdentity(
		const D3D11_SO_DECLARATION_ENTRY* a_declaration,
		UINT a_entryCount,
		const UINT* a_strides,
		UINT a_strideCount,
		UINT a_rasterizedStream) noexcept;

	std::string ObservationKey(const PreparedObservation& a_observation);
	std::string BlobRelativePath(std::string_view a_sha256);
	bool ValidatePublicationRoot(const std::filesystem::path& a_root, std::string& a_error);
	PublicationResult PublishBlob(
		const std::filesystem::path& a_root,
		std::string_view a_sha256,
		const void* a_data,
		std::size_t a_size);
	PublicationResult PublishManifest(
		const std::filesystem::path& a_root,
		std::string_view a_generatedRunId,
		std::string_view a_json);
	struct StagedManifestPublication
	{
		PublicationResult result;
		std::shared_ptr<void> state;
	};
	StagedManifestPublication StageManifest(
		const std::filesystem::path& a_root,
		std::string_view a_generatedRunId,
		std::string_view a_json);
	PublicationResult PublishStagedManifest(
		StagedManifestPublication& a_staged);
	void DiscardStagedManifest(StagedManifestPublication& a_staged) noexcept;
	struct PinnedPublishedFile
	{
		bool success = false;
		std::string error;
		std::shared_ptr<void> state;
	};
	PinnedPublishedFile PinPublishedFile(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_relative,
		std::size_t a_expectedSize,
		std::string_view a_expectedSha256);
	bool VerifyPublishedFile(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_relative,
		std::size_t a_expectedSize,
		std::string_view a_expectedSha256,
		std::string& a_error);
	bool FingerprintPublicationRoot(
		const std::filesystem::path& a_root,
		std::string& a_fingerprint,
		std::string& a_error) noexcept;
#ifdef FO4CS_SHADER_CATALOG_TESTING
	using BeforeDirectoryCreateForTesting =
		void (*)(const std::filesystem::path&) noexcept;
	void SetBeforeDirectoryCreateForTesting(
		BeforeDirectoryCreateForTesting a_callback) noexcept;
	void HoldNextPublishedWinnerForTesting() noexcept;
	bool PublishedWinnerHeldForTesting() noexcept;
	bool PublicationCollisionRetriedForTesting() noexcept;
	void ReleasePublishedWinnerForTesting() noexcept;
#endif
	bool IsValidUtf8(std::string_view a_value) noexcept;
	std::string BuildCanonicalManifest(ManifestDocument a_document);
}
