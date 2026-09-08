#include "Host/HostPageCatalog.h"

#include "FeatureCategories.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>

namespace cs::host
{
	namespace
	{
		struct BuiltInPage
		{
			std::string_view id;
			std::string_view displayName;
			std::string_view summary;
			std::int32_t sortKey;
			HostPageKind kind;
		};

		constexpr std::array<BuiltInPage, 3> kBuiltInPages{
			BuiltInPage{ "home", "Home", "Feature status, quick links, and FAQ.", 0, HostPageKind::kHome },
			BuiltInPage{ "advanced", "Advanced", "Startup loading, shader configuration, and logging.", 20, HostPageKind::kAdvanced },
			BuiltInPage{ "presets", "Presets", "Cross-feature setting presets.", 30, HostPageKind::kPresets }
		};

		struct KnownCategory
		{
			std::string_view id;
			std::string_view displayName;
		};

		constexpr std::array kKnownCategories{
			KnownCategory{ kGeneralCategoryId, kGeneralCategory },
			KnownCategory{ "lighting", FeatureCategories::kLighting },
			KnownCategory{ "post-process", FeatureCategories::kPostProcess },
			KnownCategory{ "compatibility", FeatureCategories::kCompatibility },
			KnownCategory{ "performance", FeatureCategories::kPerformance },
			KnownCategory{ "dev-tools", FeatureCategories::kDevTools },
			KnownCategory{ "misc", FeatureCategories::kMisc },
			KnownCategory{ kOtherCategoryId, kUncategorized },
			KnownCategory{ kUnloadedCategoryId, kUnloadedCategory },
			KnownCategory{ kOverlayCategoryId, kOverlayCategory }
		};

		constexpr std::int32_t kFeatureSortKey = 100;
		constexpr std::int32_t kOverlaySortKey = 1000;
		constexpr std::int32_t kCustomCategorySortKey = 220;
		constexpr std::size_t kMaximumCategoryIdLength = 128;

		const KnownCategory* FindKnownCategory(std::string_view a_displayName)
		{
			const auto result = std::ranges::find(
				kKnownCategories, a_displayName, &KnownCategory::displayName);
			return result == kKnownCategories.end() ? nullptr : &*result;
		}

		std::int32_t CategorySortKey(std::string_view a_displayName)
		{
			if (a_displayName == kGeneralCategory)
				return 0;
			const auto liveCategory =
				std::ranges::find(FeatureCategories::kRenderOrder, a_displayName);
			if (liveCategory != FeatureCategories::kRenderOrder.end()) {
				return 100 +
					static_cast<std::int32_t>(
						liveCategory - FeatureCategories::kRenderOrder.begin()) *
						10;
			}
			if (a_displayName == FeatureCategories::kMisc)
				return 200;
			if (a_displayName == kUncategorized)
				return 210;
			if (a_displayName == kUnloadedCategory)
				return 900;
			if (a_displayName == kOverlayCategory)
				return 1000;
			return kCustomCategorySortKey;
		}

		std::string MakeUniqueCategoryId(
			std::string_view a_displayName,
			std::unordered_set<std::string>& a_usedIds)
		{
			std::string base = MakeAsciiId(a_displayName);
			base.resize((std::min)(base.size(), kMaximumCategoryIdLength));
			if (!a_usedIds.contains(base)) {
				a_usedIds.insert(base);
				return base;
			}

			for (std::size_t suffix = 2;; ++suffix) {
				const auto suffixText = "-" + std::to_string(suffix);
				std::string candidate = base.substr(
					0,
					(std::min)(
						base.size(),
						kMaximumCategoryIdLength - suffixText.size()));
				candidate += suffixText;
				if (!a_usedIds.contains(candidate)) {
					a_usedIds.insert(candidate);
					return candidate;
				}
			}
		}
	}

	std::string MakeAsciiId(std::string_view a_text)
	{
		std::string id;
		id.reserve(a_text.size());
		for (const char character : a_text) {
			const auto value = static_cast<unsigned char>(character);
			char mapped = '-';
			if (value >= 'a' && value <= 'z')
				mapped = character;
			else if (value >= 'A' && value <= 'Z')
				mapped = static_cast<char>(value - 'A' + 'a');
			else if (value >= '0' && value <= '9')
				mapped = character;
			else if (character == '.' || character == '_' || character == '-')
				mapped = character;

			if (mapped == '-' && (id.empty() || id.back() == '-'))
				continue;
			id.push_back(mapped);
		}
		while (!id.empty() && id.back() == '-')
			id.pop_back();
		return id.empty() ? std::string("page") : id;
	}

	HostPageCatalog BuildPageCatalog(const std::vector<FeaturePageInput>& a_features)
	{
		HostPageCatalog catalog;
		auto& categories = catalog.categories;
		auto& pages = catalog.pages;
		pages.reserve(a_features.size() + kBuiltInPages.size() + 1);

		for (const auto& builtIn : kBuiltInPages) {
			pages.push_back(HostPageDescriptor{
				.id = std::string(builtIn.id),
				.displayName = std::string(builtIn.displayName),
				.categoryId = std::string(kGeneralCategoryId),
				.summary = std::string(builtIn.summary),
				.sortKey = builtIn.sortKey,
				.kind = builtIn.kind });
		}

		std::set<std::string> categoryNames{
			std::string(kGeneralCategory),
			std::string(kOverlayCategory)
		};
		for (const auto& feature : a_features) {
			if (!feature.active) {
				categoryNames.emplace(kUnloadedCategory);
				continue;
			}
			categoryNames.emplace(
				feature.category.empty() ?
					kUncategorized :
					std::string_view(feature.category));
		}

		std::unordered_set<std::string> usedCategoryIds;
		for (const auto& known : kKnownCategories)
			usedCategoryIds.emplace(known.id);

		std::map<std::string, std::string, std::less<>> categoryIds;
		categories.reserve(categoryNames.size());
		for (const auto& displayName : categoryNames) {
			const auto* known = FindKnownCategory(displayName);
			auto id = known ?
				std::string(known->id) :
				MakeUniqueCategoryId(displayName, usedCategoryIds);
			categoryIds.emplace(displayName, id);
			categories.push_back(HostCategoryDescriptor{
				.id = std::move(id),
				.displayName = displayName,
				.sortKey = CategorySortKey(displayName) });
		}
		std::ranges::sort(categories, [](const auto& a_lhs, const auto& a_rhs) {
			return std::tie(a_lhs.sortKey, a_lhs.displayName, a_lhs.id) <
				std::tie(a_rhs.sortKey, a_rhs.displayName, a_rhs.id);
		});

		std::vector<std::size_t> order(a_features.size());
		for (std::size_t i = 0; i < order.size(); ++i)
			order[i] = i;
		std::ranges::sort(order, [&a_features](std::size_t a_lhs, std::size_t a_rhs) {
			const auto& lhs = a_features[a_lhs];
			const auto& rhs = a_features[a_rhs];
			return std::tie(lhs.displayName, lhs.name) < std::tie(rhs.displayName, rhs.name);
		});

		std::unordered_set<std::string> usedIds;
		for (const auto& page : pages)
			usedIds.insert(page.id);

		for (const std::size_t index : order) {
			const auto& feature = a_features[index];
			std::string id = "feature-" + MakeAsciiId(feature.name);
			// Normalized feature IDs can collide.
			if (usedIds.contains(id)) {
				std::string candidate;
				for (std::size_t suffix = 2;; ++suffix) {
					candidate = id + "-" + std::to_string(suffix);
					if (!usedIds.contains(candidate))
						break;
				}
				id = std::move(candidate);
			}
			usedIds.insert(id);

			std::string displayName = feature.displayName.empty() ? feature.name : feature.displayName;
			if (displayName.empty())
				displayName = id;
			const std::string_view category = feature.active ?
				(feature.category.empty() ?
					 kUncategorized :
					 std::string_view(feature.category)) :
				kUnloadedCategory;

			pages.push_back(HostPageDescriptor{
				.id = std::move(id),
				.displayName = std::move(displayName),
				.categoryId = categoryIds.at(std::string(category)),
				.summary = feature.summary,
				.sortKey = kFeatureSortKey,
				.kind = HostPageKind::kFeature,
				.featureIndex = index });
		}

		pages.push_back(HostPageDescriptor{
			.id = std::string(kOverlayPageId),
			.displayName = "Community Shaders Overlay",
			.categoryId = std::string(kOverlayCategoryId),
			.summary = "On-screen feature overlays.",
			.sortKey = kOverlaySortKey,
			.kind = HostPageKind::kOverlay });

		return catalog;
	}
}
