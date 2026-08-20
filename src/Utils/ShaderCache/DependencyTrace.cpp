#include "Utils/ShaderCache/DependencyTrace.h"

#include "Utils/ShaderCache/CacheStorage.h"

#include <string_view>

namespace cs::shader_cache
{
	namespace
	{
		RevalidationOutcome Reject(
			RevalidationStatus a_status,
			std::string_view   a_what,
			std::string_view   a_path)
		{
			RevalidationOutcome outcome;
			outcome.status = a_status;
			outcome.detail = std::string(a_what) + ": " + std::string(a_path);
			return outcome;
		}

		bool MatchesRecordedContent(
			const FileObservation&      a_observation,
			const sha256::Sha256Result& a_digest,
			std::uint64_t               a_length)
		{
			if (a_observation.status != FileReadStatus::kOk)
				return false;
			if (sha256::Sha256IsZero(a_observation.contentDigest)
				|| sha256::Sha256IsZero(a_digest)) {
				return false;
			}
			// Length is only ever a companion to the digest; it never decides freshness on its own.
			return a_observation.contentLength == a_length
				&& a_observation.contentDigest == a_digest;
		}

		FileObservation Observe(RevalidationContext* a_context, const std::string& a_locator)
		{
			return a_context ? a_context->Observe(a_locator) : ObserveFile(a_locator);
		}
	}

	const char* DescribeRevalidation(RevalidationStatus a_status) noexcept
	{
		switch (a_status) {
		case RevalidationStatus::kValid:
			return "valid";
		case RevalidationStatus::kRootChanged:
			return "root source changed";
		case RevalidationStatus::kDependencyChanged:
			return "include content changed";
		case RevalidationStatus::kShadowAppeared:
			return "higher priority include appeared";
		case RevalidationStatus::kProbeUnstable:
			return "include probe state changed";
		}
		return "unknown";
	}

	RevalidationOutcome RevalidateDependencyManifest(
		const DependencyManifest& a_manifest,
		RevalidationContext*      a_context)
	{
		if (!MatchesRecordedContent(
				Observe(a_context, a_manifest.rootLocator),
				a_manifest.rootDigest,
				a_manifest.rootLength)) {
			return Reject(
				RevalidationStatus::kRootChanged,
				"root",
				a_manifest.rootLocator);
		}

		for (const auto& include : a_manifest.includes) {
			for (const auto& probe : include.probes) {
				const auto observation = Observe(a_context, probe.path);
				switch (probe.status) {
				case ProbeStatus::kSuccess:
					if (!MatchesRecordedContent(
							observation,
							probe.contentDigest,
							probe.contentLength)) {
						return Reject(
							RevalidationStatus::kDependencyChanged,
							include.requestedName,
							probe.path);
					}
					break;
				case ProbeStatus::kMissing:
					// A candidate that used to be absent now resolving would shadow the recorded hit.
					if (observation.status != FileReadStatus::kMissing) {
						return Reject(
							RevalidationStatus::kShadowAppeared,
							include.requestedName,
							probe.path);
					}
					break;
				case ProbeStatus::kReadFailed:
					if (observation.status == FileReadStatus::kOk
						|| observation.status == FileReadStatus::kMissing) {
						return Reject(
							RevalidationStatus::kProbeUnstable,
							include.requestedName,
							probe.path);
					}
					break;
				}
			}
		}

		return {};
	}
}
