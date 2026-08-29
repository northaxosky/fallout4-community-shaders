#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cs::host
{
	enum class HostPageKind : std::uint8_t
	{
		kHome,
		kGeneral,
		kAdvanced,
		kPresets,
		kFeature,
		kOverlay
	};

	struct FeaturePageInput
	{
		std::string name;
		std::string displayName;
		std::string category;
		std::string summary;
		bool active{ false };
		bool installed{ false };
	};

	struct HostPageDescriptor
	{
		std::string id;
		std::string displayName;
		std::string category;
		std::string summary;
		std::int32_t sortKey{ 0 };
		HostPageKind kind{ HostPageKind::kFeature };
		std::size_t featureIndex{ 0 };
	};

	inline constexpr std::string_view kBuiltInCategory = "Community Shaders";
	inline constexpr std::string_view kUnloadedCategory = "Unloaded";
	inline constexpr std::string_view kUncategorized = "Other";
	inline constexpr std::string_view kOverlayCategory = "Overlay";
	inline constexpr std::string_view kOverlayPageId = "overlay";

	std::string MakeAsciiId(std::string_view a_text);

	std::vector<HostPageDescriptor> BuildPageCatalog(const std::vector<FeaturePageInput>& a_features);
}
