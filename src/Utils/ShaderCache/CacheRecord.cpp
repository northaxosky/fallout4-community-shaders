#include "Utils/ShaderCache/CacheRecord.h"

#include "Utils/ShaderCache/ByteCodec.h"

#include <algorithm>

namespace cs::shader_cache
{
	namespace
	{
		bool WithinTextLimit(const std::string& a_text, std::uint32_t a_limit)
		{
			return a_text.size() <= a_limit;
		}

		bool KnownProbeStatus(std::uint8_t a_status) noexcept
		{
			return a_status <= static_cast<std::uint8_t>(ProbeStatus::kReadFailed);
		}

		bool KnownIncludeKind(std::uint8_t a_kind) noexcept
		{
			return a_kind <= static_cast<std::uint8_t>(IncludeKind::kSystem);
		}

		bool ValidLocator(const std::string& a_locator) noexcept
		{
			if (a_locator.empty())
				return false;
			try {
				return !DecodeLocator(a_locator).empty();
			} catch (...) {
				return false;
			}
		}
	}

	const char* DescribeRecordStatus(RecordStatus a_status) noexcept
	{
		switch (a_status) {
		case RecordStatus::kOk:
			return "ok";
		case RecordStatus::kBadMagic:
			return "bad magic";
		case RecordStatus::kUnsupportedVersion:
			return "unsupported schema version";
		case RecordStatus::kTruncated:
			return "truncated";
		case RecordStatus::kLimitExceeded:
			return "declared size out of range";
		case RecordStatus::kUnknownStage:
			return "unknown stage";
		case RecordStatus::kMalformedManifest:
			return "malformed manifest";
		case RecordStatus::kManifestDigestMismatch:
			return "manifest digest mismatch";
		case RecordStatus::kPayloadDigestMismatch:
			return "payload digest mismatch";
		case RecordStatus::kTrailingBytes:
			return "trailing bytes";
		}
		return "unknown";
	}

	bool SerializeDependencyManifest(
		const DependencyManifest&  a_manifest,
		std::vector<std::uint8_t>& a_bytes)
	{
		a_bytes.clear();
		if (!WithinTextLimit(a_manifest.rootLocator, kMaxLocatorBytes)
			|| !ValidLocator(a_manifest.rootLocator)
			|| sha256::Sha256IsZero(a_manifest.rootDigest)
			|| a_manifest.includes.size() > kMaxManifestEntries) {
			return false;
		}

		ByteWriter writer(a_bytes);
		writer.Text(a_manifest.rootLocator);
		writer.Digest(a_manifest.rootDigest);
		writer.U64(a_manifest.rootLength);
		writer.U32(static_cast<std::uint32_t>(a_manifest.includes.size()));
		for (const auto& include : a_manifest.includes) {
			if (!WithinTextLimit(include.requestedName, kMaxLocatorBytes)
				|| !WithinTextLimit(include.parentLocator, kMaxLocatorBytes)
				|| include.probes.size() > kMaxProbesPerEntry) {
				a_bytes.clear();
				return false;
			}
			writer.U8(static_cast<std::uint8_t>(include.kind));
			writer.Text(include.requestedName);
			writer.Text(include.parentLocator);
			writer.U32(static_cast<std::uint32_t>(include.probes.size()));
			for (const auto& probe : include.probes) {
				if (!WithinTextLimit(probe.path, kMaxLocatorBytes)
					|| !ValidLocator(probe.path)
					|| (probe.status == ProbeStatus::kSuccess
						&& sha256::Sha256IsZero(probe.contentDigest))) {
					a_bytes.clear();
					return false;
				}
				writer.U8(static_cast<std::uint8_t>(probe.status));
				writer.Text(probe.path);
				if (probe.status == ProbeStatus::kSuccess) {
					writer.Digest(probe.contentDigest);
					writer.U64(probe.contentLength);
				}
			}
		}

		if (a_bytes.size() > kMaxManifestBytes) {
			a_bytes.clear();
			return false;
		}
		return true;
	}

	RecordStatus ParseDependencyManifest(
		std::span<const std::uint8_t> a_bytes,
		DependencyManifest&           a_manifest)
	{
		a_manifest = {};
		ByteReader reader(a_bytes);
		if (!reader.Text(a_manifest.rootLocator, kMaxLocatorBytes)
			|| !reader.Digest(a_manifest.rootDigest)
			|| !reader.U64(a_manifest.rootLength)) {
			return RecordStatus::kMalformedManifest;
		}
		if (!ValidLocator(a_manifest.rootLocator)
			|| sha256::Sha256IsZero(a_manifest.rootDigest)) {
			return RecordStatus::kMalformedManifest;
		}

		std::uint32_t includeCount = 0;
		if (!reader.U32(includeCount))
			return RecordStatus::kMalformedManifest;
		if (includeCount > kMaxManifestEntries)
			return RecordStatus::kLimitExceeded;

		a_manifest.includes.reserve(includeCount);
		for (std::uint32_t index = 0; index < includeCount; ++index) {
			IncludeResolution include;
			std::uint8_t      kind = 0;
			if (!reader.U8(kind) || !KnownIncludeKind(kind))
				return RecordStatus::kMalformedManifest;
			include.kind = static_cast<IncludeKind>(kind);
			if (!reader.Text(include.requestedName, kMaxLocatorBytes)
				|| !reader.Text(include.parentLocator, kMaxLocatorBytes)) {
				return RecordStatus::kMalformedManifest;
			}

			std::uint32_t probeCount = 0;
			if (!reader.U32(probeCount))
				return RecordStatus::kMalformedManifest;
			if (probeCount > kMaxProbesPerEntry)
				return RecordStatus::kLimitExceeded;

			include.probes.reserve(probeCount);
			for (std::uint32_t probeIndex = 0; probeIndex < probeCount; ++probeIndex) {
				IncludeProbe probe;
				std::uint8_t status = 0;
				if (!reader.U8(status) || !KnownProbeStatus(status))
					return RecordStatus::kMalformedManifest;
				probe.status = static_cast<ProbeStatus>(status);
				if (!reader.Text(probe.path, kMaxLocatorBytes)
					|| !ValidLocator(probe.path)) {
					return RecordStatus::kMalformedManifest;
				}
				if (probe.status == ProbeStatus::kSuccess
					&& (!reader.Digest(probe.contentDigest)
						|| !reader.U64(probe.contentLength)
						|| sha256::Sha256IsZero(probe.contentDigest))) {
					return RecordStatus::kMalformedManifest;
				}
				include.probes.push_back(std::move(probe));
			}
			a_manifest.includes.push_back(std::move(include));
		}

		if (!reader.AtEnd())
			return RecordStatus::kMalformedManifest;
		return RecordStatus::kOk;
	}

	bool SerializeShaderCacheRecord(
		const ShaderCacheRecord&   a_record,
		std::vector<std::uint8_t>& a_bytes)
	{
		a_bytes.clear();
		std::vector<std::uint8_t> manifestBytes;
		if (!SerializeDependencyManifest(a_record.manifest, manifestBytes))
			return false;
		if (!WithinTextLimit(a_record.profile, kMaxProfileBytes))
			return false;
		if (a_record.payload.empty() || a_record.payload.size() > kMaxPayloadBytes)
			return false;
		if (sha256::Sha256IsZero(a_record.logicalDigest)
			|| sha256::Sha256IsZero(a_record.recipeDigest)
			|| sha256::Sha256IsZero(a_record.dependencyDigest)) {
			return false;
		}

		ByteWriter writer(a_bytes);
		writer.Bytes(kRecordMagic.data(), kRecordMagic.size());
		writer.U32(kRecordSchemaVersion);
		writer.Digest(a_record.logicalDigest);
		writer.Digest(a_record.recipeDigest);
		writer.Digest(a_record.dependencyDigest);
		writer.U8(static_cast<std::uint8_t>(a_record.stage));
		writer.Text(a_record.profile);
		writer.U64(static_cast<std::uint64_t>(manifestBytes.size()));
		writer.U32(static_cast<std::uint32_t>(a_record.manifest.includes.size()));
		writer.U64(static_cast<std::uint64_t>(a_record.payload.size()));
		const auto payloadDigest =
			sha256::Sha256Compute(a_record.payload.data(), a_record.payload.size());
		if (sha256::Sha256IsZero(payloadDigest)) {
			a_bytes.clear();
			return false;
		}
		writer.Digest(payloadDigest);
		writer.Bytes(manifestBytes.data(), manifestBytes.size());
		writer.Bytes(a_record.payload.data(), a_record.payload.size());
		return true;
	}

	RecordStatus ParseShaderCacheRecord(
		std::span<const std::uint8_t> a_bytes,
		ShaderCacheRecord&            a_record)
	{
		a_record = {};
		ByteReader reader(a_bytes);

		const auto magic = reader.View(kRecordMagic.size());
		if (magic.empty())
			return RecordStatus::kTruncated;
		if (!std::ranges::equal(magic, kRecordMagic))
			return RecordStatus::kBadMagic;

		std::uint32_t version = 0;
		if (!reader.U32(version))
			return RecordStatus::kTruncated;
		if (version != kRecordSchemaVersion)
			return RecordStatus::kUnsupportedVersion;

		std::uint8_t stage = 0;
		if (!reader.Digest(a_record.logicalDigest)
			|| !reader.Digest(a_record.recipeDigest)
			|| !reader.Digest(a_record.dependencyDigest)
			|| !reader.U8(stage)) {
			return RecordStatus::kTruncated;
		}
		if (!IsKnownStage(stage))
			return RecordStatus::kUnknownStage;
		a_record.stage = static_cast<ShaderCacheStage>(stage);

		if (!reader.Text(a_record.profile, kMaxProfileBytes))
			return RecordStatus::kTruncated;

		std::uint64_t manifestLength = 0;
		std::uint32_t manifestCount  = 0;
		std::uint64_t payloadLength  = 0;
		if (!reader.U64(manifestLength)
			|| !reader.U32(manifestCount)
			|| !reader.U64(payloadLength)) {
			return RecordStatus::kTruncated;
		}

		if (manifestLength > kMaxManifestBytes
			|| manifestCount > kMaxManifestEntries
			|| payloadLength == 0
			|| payloadLength > kMaxPayloadBytes) {
			return RecordStatus::kLimitExceeded;
		}

		sha256::Sha256Result payloadDigest{};
		if (!reader.Digest(payloadDigest))
			return RecordStatus::kTruncated;

		const auto manifestBytes = reader.View(static_cast<std::size_t>(manifestLength));
		if (manifestBytes.empty())
			return RecordStatus::kTruncated;
		const auto manifestDigest =
			sha256::Sha256Compute(manifestBytes.data(), manifestBytes.size());
		if (sha256::Sha256IsZero(manifestDigest)
			|| manifestDigest != a_record.dependencyDigest) {
			return RecordStatus::kManifestDigestMismatch;
		}

		const auto manifestStatus =
			ParseDependencyManifest(manifestBytes, a_record.manifest);
		if (manifestStatus != RecordStatus::kOk)
			return manifestStatus;
		if (a_record.manifest.includes.size() != manifestCount)
			return RecordStatus::kMalformedManifest;

		const auto payload = reader.View(static_cast<std::size_t>(payloadLength));
		if (payload.empty())
			return RecordStatus::kTruncated;
		const auto computedPayloadDigest =
			sha256::Sha256Compute(payload.data(), payload.size());
		if (sha256::Sha256IsZero(computedPayloadDigest)
			|| computedPayloadDigest != payloadDigest) {
			return RecordStatus::kPayloadDigestMismatch;
		}
		if (!reader.AtEnd())
			return RecordStatus::kTrailingBytes;

		a_record.payload.assign(payload.begin(), payload.end());
		return RecordStatus::kOk;
	}
}
