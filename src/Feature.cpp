#include "Feature.h"

#include "Log.h"
#include "Plugin.h"
#include "Settings/PresetManager.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	auto* L = cs::log::Get("cs");
	constexpr auto kUnvisited = std::numeric_limits<std::size_t>::max();
	constexpr const char* kConfigDir = "Data\\F4SE\\Plugins\\FO4CommunityShaders";
	constexpr const char* kGlobalConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\FO4CommunityShaders.toml";
	constexpr bool kDefaultAutoInstallAllFeatures = true;

	std::string ToString(std::string_view a_value)
	{
		return { a_value.data(), a_value.size() };
	}

	std::string PluginVersionString()
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%u.%u.%u", Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);
		return buf;
	}

	fs::path FeatureConfigPath(const cs::Feature& a_feature)
	{
		return fs::path(kConfigDir) / (ToString(a_feature.GetName()) + ".toml");
	}

	bool PathExists(const fs::path& a_path)
	{
		std::error_code ec;
		const bool exists = fs::exists(a_path, ec);
		return exists && !ec;
	}

	void EnsureConfigDirectory()
	{
		std::error_code ec;
		fs::create_directories(kConfigDir, ec);
		if (ec) {
			L->warn("Failed to create feature config directory {}: {}", kConfigDir, ec.message());
		}
	}

	bool WriteToml(const fs::path& a_path, const toml::table& a_table)
	{
		std::ofstream out(a_path);
		if (!out) {
			return false;
		}
		out << a_table;
		return out.good();
	}

	bool LoadAutoInstallAllFeatures()
	{
		toml::table table;
		const bool configExists = PathExists(kGlobalConfigPath);
		bool parsedOk = false;
		if (configExists) {
			try {
				table = toml::parse_file(kGlobalConfigPath);
				parsedOk = true;
			} catch (const toml::parse_error& e) {
				L->warn("Failed to parse global feature config {}: {}. Leaving file untouched; preset state will be preserved on a successful reparse.",
					kGlobalConfigPath, e.description());
				return kDefaultAutoInstallAllFeatures;
			}
		}

		const auto* features = table["features"].as_table();
		const bool hasKey = features && features->contains("auto_install_all_features");
		const bool autoInstall = table["features"]["auto_install_all_features"].value_or(kDefaultAutoInstallAllFeatures);

		if (!configExists || !hasKey) {
			EnsureConfigDirectory();
			// Scoped key-level mutation so sibling blocks ([preset], owned by PresetManager) survive.
			if (!table["info"].as_table()) {
				table.insert_or_assign("info", toml::table{});
			}
			(*table["info"].as_table()).insert_or_assign("version", PluginVersionString());

			if (!table["features"].as_table()) {
				table.insert_or_assign("features", toml::table{});
			}
			(*table["features"].as_table()).insert_or_assign("auto_install_all_features", autoInstall);

			if (!WriteToml(kGlobalConfigPath, table)) {
				L->warn("Failed to save global feature config {}", kGlobalConfigPath);
			}
		}

		(void)parsedOk;
		return autoInstall;
	}

	bool EnsureFeatureConfig(const cs::Feature& a_feature)
	{
		const auto path = FeatureConfigPath(a_feature);
		if (PathExists(path)) {
			return true;
		}

		EnsureConfigDirectory();
		toml::table table{ { "info", toml::table{ { "version", PluginVersionString() } } } };
		if (!WriteToml(path, table)) {
			L->warn("Failed to create feature config {}", path.string());
			return false;
		}
		L->info("Created feature config {}", path.string());
		return true;
	}

	void LogCycle(const std::vector<std::size_t>& a_component, const std::vector<cs::Feature*>& a_features)
	{
		std::string names;
		for (const auto featureIdx : a_component) {
			if (!names.empty()) {
				names += ", ";
			}
			const auto name = a_features[featureIdx]->GetName();
			names.append(name.data(), name.size());
		}
		L->error("Feature dependency cycle detected involving: {}", names);
	}

	void ReportDependencyCycles(
		const std::vector<std::vector<std::size_t>>& a_adjacency,
		const std::vector<cs::Feature*>& a_features)
	{
		const auto count = a_features.size();
		std::vector<std::size_t> index(count, kUnvisited);
		std::vector<std::size_t> lowlink(count, 0);
		std::vector<std::size_t> stack;
		std::vector<bool> onStack(count, false);
		std::size_t nextIndex = 0;

		auto strongConnect = [&](auto&& a_self, std::size_t a_node) -> void {
			index[a_node] = nextIndex;
			lowlink[a_node] = nextIndex;
			++nextIndex;
			stack.push_back(a_node);
			onStack[a_node] = true;

			for (const auto dependent : a_adjacency[a_node]) {
				if (index[dependent] == kUnvisited) {
					a_self(a_self, dependent);
					lowlink[a_node] = std::min(lowlink[a_node], lowlink[dependent]);
				} else if (onStack[dependent]) {
					lowlink[a_node] = std::min(lowlink[a_node], index[dependent]);
				}
			}

			if (lowlink[a_node] != index[a_node]) {
				return;
			}

			std::vector<std::size_t> component;
			while (!stack.empty()) {
				const auto member = stack.back();
				stack.pop_back();
				onStack[member] = false;
				component.push_back(member);
				if (member == a_node) {
					break;
				}
			}

			bool cyclic = component.size() > 1;
			if (!cyclic) {
				const auto node = component.front();
				cyclic = std::find(a_adjacency[node].begin(), a_adjacency[node].end(), node) != a_adjacency[node].end();
			}
			if (cyclic) {
				std::sort(component.begin(), component.end());
				LogCycle(component, a_features);
			}
		};

		for (std::size_t i = 0; i < count; ++i) {
			if (index[i] == kUnvisited) {
				strongConnect(strongConnect, i);
			}
		}
	}

	std::vector<cs::Feature*> SortFeaturesByDependencies(const std::vector<cs::Feature*>& a_features)
	{
		const auto count = a_features.size();
		std::unordered_map<std::string_view, std::size_t> indexByName;
		indexByName.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			const auto [it, inserted] = indexByName.emplace(a_features[i]->GetName(), i);
			if (!inserted) {
				L->warn("Feature {} is registered more than once; dependency order may be unstable", it->first);
			}
		}

		std::vector<std::vector<std::size_t>> adjacency(count);
		std::vector<std::size_t> indegree(count, 0);
		for (std::size_t featureIdx = 0; featureIdx < count; ++featureIdx) {
			std::unordered_set<std::size_t> seenDependencies;
			std::unordered_set<std::string_view> seenMissingDependencies;
			for (const auto dependency : a_features[featureIdx]->GetDependencies()) {
				const auto dependencyIt = indexByName.find(dependency);
				if (dependencyIt == indexByName.end()) {
					if (seenMissingDependencies.insert(dependency).second) {
						L->warn("Feature {} depends on {} which is not registered; load order may be unstable",
							a_features[featureIdx]->GetName(), dependency);
					}
					continue;
				}

				const auto dependencyIdx = dependencyIt->second;
				if (!seenDependencies.insert(dependencyIdx).second) {
					continue;
				}
				adjacency[dependencyIdx].push_back(featureIdx);
				++indegree[featureIdx];
			}
		}

		std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<std::size_t>> ready;
		for (std::size_t i = 0; i < count; ++i) {
			if (indegree[i] == 0) {
				ready.push(i);
			}
		}

		std::vector<cs::Feature*> sorted;
		sorted.reserve(count);
		while (!ready.empty()) {
			const auto featureIdx = ready.top();
			ready.pop();
			sorted.push_back(a_features[featureIdx]);

			for (const auto dependent : adjacency[featureIdx]) {
				--indegree[dependent];
				if (indegree[dependent] == 0) {
					ready.push(dependent);
				}
			}
		}

		if (sorted.size() != count) {
			ReportDependencyCycles(adjacency, a_features);
			return a_features;
		}

		return sorted;
	}
}

namespace cs
{
	bool Feature::IsInstalled() const
	{
		return PathExists(FeatureConfigPath(*this));
	}

	std::string Feature::GetPresetKey() const
	{
		const auto name = GetName();
		std::string out;
		out.reserve(name.size());
		for (char c : name) {
			const auto lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			const bool ok = (lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9') || lc == '_';
			out.push_back(ok ? lc : '_');
		}
		return out;
	}

	FeatureManager& FeatureManager::Get()
	{
		static FeatureManager instance;
		return instance;
	}

	void FeatureManager::Register(Feature* a_feature)
	{
		_features.push_back(a_feature);
	}

	void FeatureManager::LoadAll()
	{
		_features = SortFeaturesByDependencies(_features);
		_loadedFeatures.clear();
		for (auto* feature : _features) {
			feature->SetLoaded(false);
		}

		const bool autoInstallAll = LoadAutoInstallAllFeatures();
		L->info("Feature INI auto-install: {}", autoInstallAll ? "enabled" : "disabled");

		std::unordered_map<std::string_view, bool> installedByName;
		installedByName.reserve(_features.size());
		for (auto* feature : _features) {
			bool installed = true;
			if (autoInstallAll) {
				if (!EnsureFeatureConfig(*feature)) {
					L->warn("Feature {} INI missing but auto-install is enabled; loading without a companion INI",
						feature->GetName());
				}
			} else {
				installed = feature->IsInstalled();
			}
			installedByName.insert_or_assign(feature->GetName(), installed);
		}

		for (auto* feature : _features) {
			if (const auto installedIt = installedByName.find(feature->GetName()); installedIt == installedByName.end() || !installedIt->second) {
				L->info("Feature {} not installed (no INI); skipping", feature->GetName());
				continue;
			}

			for (const auto dependency : feature->GetDependencies()) {
				const auto dependencyIt = installedByName.find(dependency);
				if (dependencyIt != installedByName.end() && !dependencyIt->second) {
					L->warn("Feature {} depends on {} which is not installed; loading may be unstable",
						feature->GetName(), dependency);
				}
			}

			L->info("Loading feature: {}", feature->GetName());
			feature->Load();
			feature->SetLoaded(true);
			_loadedFeatures.push_back(feature);
		}
	}

	void FeatureManager::OnDataLoadedAll()
	{
		for (auto* feature : _loadedFeatures) {
			feature->OnDataLoaded();
		}
	}

	void FeatureManager::OnPostPostLoadAll()
	{
		for (auto* feature : _loadedFeatures) {
			feature->OnPostPostLoad();
		}
		PresetManager::Get().ResolveAndApplyBootPreset();
	}
}
