#include "Compat/Compat.h"

#include "Compat/PluginScan.h"
#include "Compat/Rules.h"
#include "ENB/ENBSeriesAPI.h"
#include "Log.h"
#include "Utils/CSUtil.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <ranges>
#include <vector>

namespace cs::compat
{
	namespace
	{
		auto* L = cs::log::Get("cs.compat");
		bool g_detected = false;
		std::vector<Finding> g_findings;

		constexpr std::string_view kF4SEPluginDirectory = "Data/F4SE/Plugins";

		struct Detector
		{
			std::vector<ScannedPlugin> plugins;
			bool enb{};

			std::optional<std::string> Match(const Rule& a_rule) const
			{
				switch (a_rule.identity.source) {
				case Source::kF4SEPlugin:
					{
						const auto found = std::ranges::find_if(plugins, [&](const ScannedPlugin& a_plugin) {
							return cs::util::EqualsIgnoreCase(a_plugin.name, a_rule.identity.id);
						});
						if (found == plugins.end())
							return std::nullopt;
						if (a_rule.exempt && a_rule.exempt(found->version)) {
							L->info("{} detected ({} {}): it runs in a compatible mode, so nothing is turned off", a_rule.mod, found->name, found->version.string("."));
							return std::nullopt;
						}
						return std::format("{} {} ({})", found->name, found->version.string("."), found->file.filename().string());
					}
				case Source::kGameFile:
					{
						std::error_code ec;
						if (!std::filesystem::exists(std::filesystem::path(a_rule.identity.id), ec))
							return std::nullopt;
						return std::string(a_rule.identity.id);
					}
				case Source::kENB:
					return enb ? std::optional<std::string>("ENB") : std::nullopt;
				}
				return std::nullopt;
			}
		};
	}

	bool Finding::Affects(std::string_view a_featureKey) const noexcept
	{
		return rule.features.empty() || std::ranges::find(rule.features, a_featureKey) != rule.features.end();
	}

	void Detect()
	{
		if (g_detected)
			return;
		g_detected = true;

		Detector detector;
		detector.plugins = ScanF4SEPlugins(kF4SEPluginDirectory);
		for (const auto& plugin : detector.plugins)
			L->debug("{}: F4SE plugin {} {}", plugin.file.filename().string(), plugin.name, plugin.version.string("."));
		L->info("Scanned {} F4SE plugins", detector.plugins.size());
		detector.enb = ENB_API::RequestENBAPI() != nullptr;

		for (const auto& rule : Rules()) {
			auto source = detector.Match(rule);
			if (!source)
				continue;
			auto& finding = g_findings.emplace_back(Finding{ rule, std::move(*source) });
			L->warn("{} detected ({}): {}", rule.mod, finding.source, rule.reason);
		}
	}

	std::span<const Finding> Findings() noexcept
	{
		return g_findings;
	}

	const Finding* Find(std::string_view a_featureKey) noexcept
	{
		const auto found = std::ranges::find_if(g_findings, [&](const Finding& a_finding) {
			return a_finding.Affects(a_featureKey);
		});
		return found == g_findings.end() ? nullptr : &*found;
	}
}
