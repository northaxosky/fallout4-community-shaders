#pragma once

#include "Utils/CSSha256.h"
#include "Utils/ShaderCache/RevalidationContext.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs::shader_cache
{
	enum class IncludeKind : std::uint8_t
	{
		kLocal  = 0,
		kSystem = 1
	};

	enum class ProbeStatus : std::uint8_t
	{
		kSuccess    = 0,
		kMissing    = 1,
		kReadFailed = 2
	};

	// One candidate path tried while resolving an include, in the order it was tried.
	struct IncludeProbe
	{
		std::string          path;
		ProbeStatus          status = ProbeStatus::kMissing;
		sha256::Sha256Result contentDigest{};
		std::uint64_t        contentLength = 0;
	};

	struct IncludeResolution
	{
		IncludeKind               kind = IncludeKind::kLocal;
		std::string               requestedName;
		std::string               parentLocator;
		std::vector<IncludeProbe> probes;
	};

	// Root content plus the complete ordered resolution trace behind one compile.
	struct DependencyManifest
	{
		std::string                    rootLocator;
		sha256::Sha256Result           rootDigest{};
		std::uint64_t                  rootLength = 0;
		std::vector<IncludeResolution> includes;
	};

	enum class RevalidationStatus : std::uint8_t
	{
		kValid,
		kRootChanged,
		kDependencyChanged,
		kShadowAppeared,
		kProbeUnstable
	};

	struct RevalidationOutcome
	{
		RevalidationStatus status = RevalidationStatus::kValid;
		std::string        detail;

		[[nodiscard]] bool Valid() const noexcept
		{
			return status == RevalidationStatus::kValid;
		}
	};

	const char* DescribeRevalidation(RevalidationStatus a_status) noexcept;

	// Replays every recorded probe decision; a context makes the batch share one filesystem snapshot.
	RevalidationOutcome RevalidateDependencyManifest(
		const DependencyManifest& a_manifest,
		RevalidationContext*      a_context = nullptr);
}
