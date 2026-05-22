#include "Feature.h"

#include "Log.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	auto* L = cs::log::Get("cs");
	constexpr auto kUnvisited = std::numeric_limits<std::size_t>::max();

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
		for (auto* feature : _features) {
			L->info("Loading feature: {}", feature->GetName());
			feature->Load();
		}
	}

	void FeatureManager::OnDataLoadedAll()
	{
		for (auto* feature : _features) {
			feature->OnDataLoaded();
		}
	}

	void FeatureManager::OnPostPostLoadAll()
	{
		for (auto* feature : _features) {
			feature->OnPostPostLoad();
		}
	}
}
