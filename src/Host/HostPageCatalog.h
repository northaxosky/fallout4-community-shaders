#pragma once

#include "FeatureCategories.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cs::host
{
	enum class HostPageKind : std::uint8_t
	{
		kHome,
		kAdvanced,
		kProfiling,
		kPresets,
		kChangelog,
		kFeature,
		kOverlay,
		kShaderCompileOverlay
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
		std::string categoryId;
		std::string summary;
		std::int32_t sortKey{ 0 };
		HostPageKind kind{ HostPageKind::kFeature };
		std::size_t featureIndex{ 0 };
	};

	struct HostCategoryDescriptor
	{
		std::string id;
		std::string displayName;
		std::int32_t sortKey{ 0 };
		std::string iconName;
	};

	struct HostPageCatalog
	{
		std::vector<HostCategoryDescriptor> categories;
		std::vector<HostPageDescriptor> pages;
	};

	inline constexpr std::string_view kClientIconName = "cloud-sun";
	inline constexpr std::string_view kGeneralCategoryId = "general";
	inline constexpr std::string_view kGeneralCategory = "General";
	inline constexpr std::string_view kUnloadedCategoryId = "unloaded";
	inline constexpr std::string_view kUnloadedCategory = "Unloaded";
	inline constexpr std::string_view kOtherCategoryId = "other";
	inline constexpr std::string_view kUncategorized = FeatureCategories::kOther;
	inline constexpr std::string_view kOverlayCategoryId = "overlay";
	inline constexpr std::string_view kOverlayCategory = "Overlay";
	inline constexpr std::string_view kOverlayPageId = "overlay";
	inline constexpr std::string_view kShaderCompileOverlayPageId = "shader-compile-overlay";

	HostPageCatalog BuildPageCatalog(const std::vector<FeaturePageInput>& a_features);
}
