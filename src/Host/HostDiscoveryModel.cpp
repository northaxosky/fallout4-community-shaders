#include "Host/HostDiscoveryModel.h"

#include <algorithm>
#include <cstring>
#include <tuple>

namespace cs::host
{
	std::string_view DescribeCompatibility(HostCompatibility a_compatibility) noexcept
	{
		switch (a_compatibility) {
		case HostCompatibility::kCompatible:
			return "compatible";
		case HostCompatibility::kNoApi:
			return "the export returned no API for this version";
		case HostCompatibility::kStructTooSmall:
			return "the host API struct is older than this client expects";
		case HostCompatibility::kUnsupportedVersion:
			return "unsupported API version";
		case HostCompatibility::kMissingFingerprint:
			return "the host published no Dear ImGui fingerprint";
		case HostCompatibility::kFingerprintMismatch:
			return "the host uses a different Dear ImGui build";
		case HostCompatibility::kMissingFunctions:
			return "the host API is missing required functions";
		default:
			return "unknown";
		}
	}

	HostCompatibility EvaluateHost(
		const HostApiView& a_view,
		const DMUI_ImGuiFingerprint& a_expected) noexcept
	{
		if (!a_view.present)
			return HostCompatibility::kNoApi;
		if (a_view.structSize < sizeof(DMUI_HostAPI))
			return HostCompatibility::kStructTooSmall;
		if (a_view.apiVersion != DMUI_API_VERSION_CURRENT)
			return HostCompatibility::kUnsupportedVersion;
		if (!a_view.hasRegisterClient ||
			!a_view.hasRegisterPage ||
			!a_view.hasQueryState ||
			!a_view.hasRequestFrame ||
			!a_view.hasReleaseFrame ||
			!a_view.hasIsMenuVisible ||
			!a_view.hasSelectPage)
			return HostCompatibility::kMissingFunctions;
		if (!a_view.hasFingerprint || a_view.fingerprint.structSize < sizeof(DMUI_ImGuiFingerprint))
			return HostCompatibility::kMissingFingerprint;

		const auto& host = a_view.fingerprint;
		const bool matches =
			std::memcmp(host.upstreamCommit, a_expected.upstreamCommit, sizeof(host.upstreamCommit)) == 0 &&
			host.imguiVersionNum == a_expected.imguiVersionNum &&
			host.flags == a_expected.flags &&
			host.sizeOfImGuiIO == a_expected.sizeOfImGuiIO &&
			host.sizeOfImGuiStyle == a_expected.sizeOfImGuiStyle &&
			host.sizeOfImVec2 == a_expected.sizeOfImVec2 &&
			host.sizeOfImVec4 == a_expected.sizeOfImVec4 &&
			host.sizeOfImDrawVert == a_expected.sizeOfImDrawVert &&
			host.sizeOfImDrawIdx == a_expected.sizeOfImDrawIdx;
		return matches ? HostCompatibility::kCompatible : HostCompatibility::kFingerprintMismatch;
	}

	HostSelection SelectHost(std::vector<HostCandidate>& a_candidates)
	{
		std::ranges::sort(a_candidates, [](const HostCandidate& a_lhs, const HostCandidate& a_rhs) {
			return std::tie(a_lhs.sortKey, a_lhs.displayPath) < std::tie(a_rhs.sortKey, a_rhs.displayPath);
		});

		HostSelection selection;
		selection.exporterCount = a_candidates.size();
		for (std::size_t i = 0; i < a_candidates.size(); ++i) {
			if (a_candidates[i].compatibility != HostCompatibility::kCompatible)
				continue;
			++selection.compatibleCount;
			if (!selection.selected)
				selection.selected = i;
		}
		return selection;
	}
}
