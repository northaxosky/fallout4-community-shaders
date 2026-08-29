#include "Host/HostDiscoveryModel.h"

#include <algorithm>
#include <cstring>
#include <tuple>

namespace cs::host
{
	namespace
	{
		bool FingerprintsMatch(
			const DMUI_ImGuiFingerprint& a_left,
			const DMUI_ImGuiFingerprint& a_right) noexcept
		{
#define DMUI_MATCH_FIELD(field) a_left.field == a_right.field
			return std::memcmp(
					   a_left.upstreamCommit,
					   a_right.upstreamCommit,
					   sizeof(a_left.upstreamCommit)) == 0 &&
				DMUI_MATCH_FIELD(imguiVersionNum) &&
				DMUI_MATCH_FIELD(flags) &&
				DMUI_MATCH_FIELD(sizeOfImGuiIO) &&
				DMUI_MATCH_FIELD(sizeOfImGuiStyle) &&
				DMUI_MATCH_FIELD(sizeOfImVec2) &&
				DMUI_MATCH_FIELD(sizeOfImVec4) &&
				DMUI_MATCH_FIELD(sizeOfImDrawVert) &&
				DMUI_MATCH_FIELD(sizeOfImDrawIdx) &&
				DMUI_MATCH_FIELD(alignOfImGuiIO) &&
				DMUI_MATCH_FIELD(alignOfImGuiStyle) &&
				DMUI_MATCH_FIELD(alignOfImVec2) &&
				DMUI_MATCH_FIELD(alignOfImVec4) &&
				DMUI_MATCH_FIELD(alignOfImDrawVert) &&
				DMUI_MATCH_FIELD(alignOfImDrawIdx) &&
				DMUI_MATCH_FIELD(sizeOfImWchar) &&
				DMUI_MATCH_FIELD(alignOfImWchar) &&
				DMUI_MATCH_FIELD(sizeOfImTextureID) &&
				DMUI_MATCH_FIELD(alignOfImTextureID) &&
				DMUI_MATCH_FIELD(sizeOfImGuiID) &&
				DMUI_MATCH_FIELD(alignOfImGuiID) &&
				DMUI_MATCH_FIELD(sizeOfImFont) &&
				DMUI_MATCH_FIELD(alignOfImFont) &&
				DMUI_MATCH_FIELD(sizeOfImFontConfig) &&
				DMUI_MATCH_FIELD(alignOfImFontConfig) &&
				DMUI_MATCH_FIELD(sizeOfImFontGlyph) &&
				DMUI_MATCH_FIELD(alignOfImFontGlyph) &&
				DMUI_MATCH_FIELD(sizeOfImGuiContext) &&
				DMUI_MATCH_FIELD(alignOfImGuiContext) &&
				DMUI_MATCH_FIELD(sizeOfImGuiErrorRecoveryState) &&
				DMUI_MATCH_FIELD(alignOfImGuiErrorRecoveryState) &&
				DMUI_MATCH_FIELD(sizeOfImGuiNextWindowData) &&
				DMUI_MATCH_FIELD(alignOfImGuiNextWindowData) &&
				DMUI_MATCH_FIELD(sizeOfImGuiNextItemData) &&
				DMUI_MATCH_FIELD(alignOfImGuiNextItemData) &&
				DMUI_MATCH_FIELD(sizeOfImGuiPopupData) &&
				DMUI_MATCH_FIELD(alignOfImGuiPopupData) &&
				DMUI_MATCH_FIELD(offsetOfImDrawVertPos) &&
				DMUI_MATCH_FIELD(offsetOfImDrawVertUv) &&
				DMUI_MATCH_FIELD(offsetOfImDrawVertCol) &&
				DMUI_MATCH_FIELD(layoutSignature);
#undef DMUI_MATCH_FIELD
		}
	}

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
		if (a_view.structSize < DMUI_HOST_API_ATTACH_SWAP_CHAIN_SIZE)
			return HostCompatibility::kStructTooSmall;
		if (a_view.apiVersion != DMUI_API_VERSION_CURRENT)
			return HostCompatibility::kUnsupportedVersion;
		if (!a_view.hasRegisterClient ||
			!a_view.hasRegisterPage ||
			!a_view.hasQueryState ||
			!a_view.hasRequestFrame ||
			!a_view.hasReleaseFrame ||
			!a_view.hasIsMenuVisible ||
			!a_view.hasSelectPage ||
			!a_view.hasAttachSwapChain)
			return HostCompatibility::kMissingFunctions;
		if (!a_view.hasFingerprint || a_view.fingerprint.structSize < sizeof(DMUI_ImGuiFingerprint))
			return HostCompatibility::kMissingFingerprint;

		return FingerprintsMatch(a_view.fingerprint, a_expected) ?
		           HostCompatibility::kCompatible :
		           HostCompatibility::kFingerprintMismatch;
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
