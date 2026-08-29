#include "Host/HostPageCatalog.h"

#include <algorithm>
#include <array>
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

		constexpr std::array<BuiltInPage, 4> kBuiltInPages{
			BuiltInPage{ "home", "Home", "Overview, quick links, and FAQ.", 0, HostPageKind::kHome },
			BuiltInPage{ "general", "General", "Shader ownership and plugin-wide settings.", 10, HostPageKind::kGeneral },
			BuiltInPage{ "advanced", "Advanced", "Diagnostics, logging, and feature boot state.", 20, HostPageKind::kAdvanced },
			BuiltInPage{ "presets", "Presets", "Cross-feature setting presets.", 30, HostPageKind::kPresets }
		};

		constexpr std::int32_t kFeatureSortKey = 100;
		constexpr std::int32_t kOverlaySortKey = 1000;
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

	std::vector<HostPageDescriptor> BuildPageCatalog(const std::vector<FeaturePageInput>& a_features)
	{
		std::vector<HostPageDescriptor> pages;
		pages.reserve(a_features.size() + kBuiltInPages.size() + 1);

		for (const auto& builtIn : kBuiltInPages) {
			pages.push_back(HostPageDescriptor{
				.id = std::string(builtIn.id),
				.displayName = std::string(builtIn.displayName),
				.category = std::string(kBuiltInCategory),
				.summary = std::string(builtIn.summary),
				.sortKey = builtIn.sortKey,
				.kind = builtIn.kind });
		}

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
			// Two features can normalize onto the same ASCII ID; the host rejects duplicates.
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
			std::string category = feature.active ? feature.category : std::string(kUnloadedCategory);
			if (category.empty())
				category = std::string(kUncategorized);

			pages.push_back(HostPageDescriptor{
				.id = std::move(id),
				.displayName = std::move(displayName),
				.category = std::move(category),
				.summary = feature.summary,
				.sortKey = kFeatureSortKey,
				.kind = HostPageKind::kFeature,
				.featureIndex = index });
		}

		pages.push_back(HostPageDescriptor{
			.id = std::string(kOverlayPageId),
			.displayName = "Community Shaders Overlay",
			.category = std::string(kOverlayCategory),
			.summary = "On-screen feature overlays.",
			.sortKey = kOverlaySortKey,
			.kind = HostPageKind::kOverlay });

		return pages;
	}
}
