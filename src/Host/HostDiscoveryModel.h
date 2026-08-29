#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <DearModdingUI/API.h>

namespace cs::host
{
	enum class HostCompatibility : std::uint8_t
	{
		kCompatible,
		kNoApi,
		kStructTooSmall,
		kUnsupportedVersion,
		kMissingFingerprint,
		kFingerprintMismatch,
		kMissingFunctions
	};

	std::string_view DescribeCompatibility(HostCompatibility a_compatibility) noexcept;

	struct HostApiView
	{
		bool present{ false };
		std::uint32_t structSize{ 0 };
		std::uint32_t apiVersion{ 0 };
		bool hasFingerprint{ false };
		DMUI_ImGuiFingerprint fingerprint{};
		bool hasRegisterClient{ false };
		bool hasRegisterPage{ false };
		bool hasQueryState{ false };
		bool hasRequestFrame{ false };
		bool hasReleaseFrame{ false };
		bool hasIsMenuVisible{ false };
		bool hasSelectPage{ false };
	};

	HostCompatibility EvaluateHost(
		const HostApiView& a_view,
		const DMUI_ImGuiFingerprint& a_expected) noexcept;

	struct HostCandidate
	{
		std::string sortKey;
		std::string displayPath;
		HostCompatibility compatibility{ HostCompatibility::kNoApi };
	};

	struct HostSelection
	{
		std::optional<std::size_t> selected;
		std::size_t exporterCount{ 0 };
		std::size_t compatibleCount{ 0 };

		bool HasAmbiguousExporters() const noexcept { return exporterCount > 1; }
		bool HasAmbiguousHosts() const noexcept { return compatibleCount > 1; }
	};

	HostSelection SelectHost(std::vector<HostCandidate>& a_candidates);
}
