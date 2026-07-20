#include "Feature.h"

#include "Env.h"
#include "Log.h"
#include "Settings/FeatureConfig.h"
#include "Settings/PresetManager.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
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

	std::string ToString(std::string_view a_value)
	{
		return { a_value.data(), a_value.size() };
	}

	std::string_view CapabilityName(cs::FeatureCapability a_capability)
	{
		switch (a_capability) {
		case cs::FeatureCapability::kPixelShaderSwapBroker:
			return "pixel-shader swap broker";
		}
		return "unknown capability";
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
		L->error("Feature requirement cycle detected involving: {}", names);
	}

	void ReportRequirementCycles(
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

	std::vector<cs::Feature*> SortFeaturesByRequirements(const std::vector<cs::Feature*>& a_features)
	{
		const auto count = a_features.size();
		std::unordered_map<std::string_view, std::size_t> indexByName;
		indexByName.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			const auto [it, inserted] = indexByName.emplace(a_features[i]->GetName(), i);
			if (!inserted) {
				L->warn("Feature {} is registered more than once; requirement order may be unstable", it->first);
			}
		}

		std::vector<std::vector<std::size_t>> adjacency(count);
		std::vector<std::size_t> indegree(count, 0);
		for (std::size_t featureIdx = 0; featureIdx < count; ++featureIdx) {
			std::unordered_set<std::size_t> seenProviders;
			std::unordered_set<std::string_view> seenMissingProviders;
			for (const auto& requirement : a_features[featureIdx]->GetRequirements()) {
				const auto providerIt = indexByName.find(requirement.provider);
				if (providerIt == indexByName.end()) {
					if (seenMissingProviders.insert(requirement.provider).second) {
						L->warn(
							"Feature {} requires capability '{}' from unregistered provider {}; activation order may be incomplete",
							a_features[featureIdx]->GetName(),
							CapabilityName(requirement.capability),
							requirement.provider);
					}
					continue;
				}

				const auto providerIdx = providerIt->second;
				if (!seenProviders.insert(providerIdx).second) {
					continue;
				}
				adjacency[providerIdx].push_back(featureIdx);
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
			ReportRequirementCycles(adjacency, a_features);
			// Return only features with a safe requirement order.
			std::unordered_set<const cs::Feature*> orderable(sorted.begin(), sorted.end());
			for (auto* feature : a_features) {
				if (orderable.find(feature) == orderable.end()) {
					L->error("Feature {} excluded from activation: part of or downstream of a requirement cycle",
						feature->GetName());
				}
			}
			return sorted;
		}

		return sorted;
	}

	template <class Callback>
	void DispatchRuntimeCallbacks(
		cs::FeatureManager& a_manager,
		std::string_view a_phase,
		Callback&& a_callback) noexcept
	{
		for (auto* feature : a_manager.GetAll()) {
			if (!feature || !a_manager.PrepareRuntimeCallback(*feature, a_phase)) {
				continue;
			}

			try {
				a_callback(*feature);
			} catch (const std::exception& e) {
				a_manager.QuarantineRuntimeCallback(*feature, a_phase, e.what());
			} catch (...) {
				a_manager.QuarantineRuntimeCallback(*feature, a_phase, "non-standard exception");
			}
		}
		a_manager.FinishRuntimeCallbackPass();
	}
}

namespace cs
{
	bool Feature::IsInstalled() const
	{
		return feature_config::GetFeature(GetConfigKey()).has_value();
	}

	ActivationResult Feature::Activate()
	{
		ResetLoadFailure();
		Load();
		if (HasLoadFailed()) {
			return ActivationResult::Failed(LoadFailureReason());
		}
		return ActivationResult::Active();
	}

	spdlog::logger* Feature::Log() const
	{
		if (_log) {
			return _log;
		}

		const auto featureName = GetName();
		std::string loggerName = "cs.feature.";
		loggerName.reserve(loggerName.size() + featureName.size());
		for (const char c : featureName) {
			loggerName.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c);
		}

		_log = cs::log::Get(loggerName.c_str());
		return _log;
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
		if (std::find(_registeredFeatures.begin(), _registeredFeatures.end(), a_feature) != _registeredFeatures.end()) {
			return;
		}

		const auto name = a_feature->GetName();
		if (std::find_if(_registeredFeatures.begin(), _registeredFeatures.end(), [name](const Feature* a_registered) {
				return a_registered->GetName() == name;
			}) != _registeredFeatures.end()) {
			return;
		}

		_registeredFeatures.push_back(a_feature);
	}

	std::optional<std::string> FeatureManager::FindRequirementFailure(const Feature& a_feature) const
	{
		for (const auto& requirement : a_feature.GetRequirements()) {
			const auto providerIt = std::find_if(
				_registeredFeatures.begin(),
				_registeredFeatures.end(),
				[provider = requirement.provider](const Feature* a_provider) {
					return a_provider->GetName() == provider;
				});
			const auto capability = CapabilityName(requirement.capability);
			if (providerIt == _registeredFeatures.end()) {
				return "Required provider '" + ToString(requirement.provider)
					+ "' for capability '" + ToString(capability) + "' is not registered";
			}

			const auto* provider = *providerIt;
			if (!provider->IsHealthy()) {
				return "Required provider '" + ToString(requirement.provider)
					+ "' is not healthy-active for capability '" + ToString(capability) + "'";
			}
			if (!provider->HasCapability(requirement.capability)) {
				return "Required provider '" + ToString(requirement.provider)
					+ "' does not provide capability '" + ToString(capability) + "'";
			}
		}
		return std::nullopt;
	}

	bool FeatureManager::PrepareRuntimeCallback(Feature& a_feature, std::string_view a_phase) noexcept
	{
		if (!a_feature.IsHealthy()) {
			return false;
		}

		try {
			if (auto failure = FindRequirementFailure(a_feature)) {
				QuarantineRuntimeCallback(a_feature, a_phase, *failure);
				return false;
			}
			return true;
		} catch (const std::exception& e) {
			QuarantineRuntimeCallback(a_feature, a_phase, e.what());
		} catch (...) {
			QuarantineRuntimeCallback(a_feature, a_phase, "requirement validation threw a non-standard exception");
		}
		return false;
	}

	bool FeatureManager::PrepareMenuCallback(Feature& a_feature, std::string_view a_phase) noexcept
	{
		const auto& state = a_feature.GetState();
		if (!state.installed) {
			return false;
		}
		// Active features get the full requirement revalidation (and may be quarantined).
		if (a_feature.IsHealthy()) {
			return PrepareRuntimeCallback(a_feature, a_phase);
		}
		// Only installed kInactive features draw settings before enabling; degraded, failed, pending, and uninstalled features do not.
		return state.runtimeState == FeatureRuntimeState::kInactive;
	}

	void FeatureManager::QuarantineRuntimeCallback(
		Feature& a_feature,
		std::string_view a_phase,
		std::string_view a_reason) noexcept
	{
		if (!a_feature.IsHealthy()) {
			return;
		}

		a_feature.SetRuntimeStateOnly(FeatureRuntimeState::kDegraded);
		try {
			const auto phase = a_phase.empty() ? std::string_view("runtime callback") : a_phase.substr(0, 64);
			const auto reason = a_reason.empty() ? std::string_view("unknown failure") : a_reason.substr(0, 256);
			std::string detail;
			detail.reserve(phase.size() + reason.size() + 96);
			detail.append("Runtime phase '");
			detail.append(phase);
			detail.append("' quarantined: ");
			detail.append(reason);
			detail.append(". Partial hooks/resources may remain; restart required.");
			a_feature.SetRuntimeState(FeatureRuntimeState::kDegraded, std::move(detail));

			try {
				L->error("Feature {} degraded: {}", a_feature.GetName(), a_feature.GetState().detail);
			} catch (...) {
			}
		} catch (...) {
			try {
				a_feature.SetRuntimeState(FeatureRuntimeState::kDegraded, "Runtime quarantine; restart required");
			} catch (...) {
			}
			try {
				L->error("Feature runtime callback quarantined; restart required");
			} catch (...) {
			}
		}
	}

	void FeatureManager::FinishRuntimeCallbackPass() noexcept
	{
		_loadedFeatures.erase(
			std::remove_if(
				_loadedFeatures.begin(),
				_loadedFeatures.end(),
				[](const Feature* a_feature) {
					return !a_feature || !a_feature->IsHealthy();
				}),
			_loadedFeatures.end());
	}

	void FeatureManager::PrepareAll()
	{
		_loadedFeatures.clear();
		_activationOrder.clear();
		for (auto* feature : _registeredFeatures) {
			feature->SetState({});
		}

		_activationOrder = SortFeaturesByRequirements(_registeredFeatures);
		const std::unordered_set<const Feature*> orderedSet(_activationOrder.begin(), _activationOrder.end());

		const auto configRoot = feature_config::GetMergedRoot();
		const auto* features = configRoot["features"].as_table();
		std::unordered_set<std::string> registeredKeys;
		registeredKeys.reserve(_registeredFeatures.size());
		for (auto* feature : _registeredFeatures) {
			registeredKeys.insert(feature->GetConfigKey());
		}
		if (features) {
			for (const auto& [key, node] : *features) {
				if (!registeredKeys.contains(std::string(key.str()))) {
					L->warn("Ignoring unknown unified configuration feature key '{}'", key.str());
				}
			}
		}

		std::vector<std::string> missingKeys;
		for (auto* feature : _registeredFeatures) {
			const auto key = feature->GetConfigKey();
			const auto* featureNode = features ? features->get(key) : nullptr;
			const bool installed = featureNode && featureNode->is_table();
			if (!installed) {
				missingKeys.push_back(key);
			}
			feature->SetState({
				.installed = installed,
				.desiredActive = false,
				.runtimeState = installed ? FeatureRuntimeState::kPending : FeatureRuntimeState::kInactive,
				.detail = installed ? std::string{} : "Unified feature configuration is missing"
			});
		}
		if (!missingKeys.empty()) {
			std::string names;
			for (const auto& key : missingKeys) {
				if (!names.empty()) {
					names += ", ";
				}
				names += key;
			}
			L->warn("Registered features absent from unified configuration: {}", names);
		}

		const auto failConfiguration = [](Feature* a_feature, std::string a_detail) {
			if (a_detail.empty()) {
				a_detail = "Feature configuration failed";
			}
			a_feature->SetRuntimeState(FeatureRuntimeState::kFailed, a_detail);
			L->error("Feature {} configuration failed: {}", a_feature->GetName(), a_detail);
		};

		for (auto* feature : _registeredFeatures) {
			if (!feature->GetState().installed) {
				continue;
			}

			const auto key = feature->GetConfigKey();
			const auto* featureTable = features->get(key)->as_table();
			const auto activation = feature_config::ParseActivation(*featureTable);
			if (!activation.present) {
				L->warn("Feature {} configuration has no boolean load key; treating as false", key);
			}
			if (!activation.valid) {
				L->warn("Feature {} configuration load key must be a boolean; treating as false", key);
			}

			std::string error;
			try {
				if (!feature->Configure(*featureTable, error)) {
					failConfiguration(feature, std::move(error));
					continue;
				}

				const bool deactivatedForEnb =
					feature->GetEnbPolicy() == EnbPolicy::kDeactivate && cs::env::IsENBLoaded();
				const bool desiredActive = activation.valid && activation.load && !deactivatedForEnb;
				feature->SetState({
					.installed = true,
					.desiredActive = desiredActive,
					.runtimeState = desiredActive ? FeatureRuntimeState::kPending : FeatureRuntimeState::kInactive,
					.detail = deactivatedForEnb
						? "ENB is active; Community Shaders yields its overlapping effects to ENB."
						: std::string{}
				});
			} catch (const std::exception& e) {
				failConfiguration(feature, e.what());
			} catch (...) {
				failConfiguration(feature, "Feature configuration threw a non-standard exception");
			}
		}

		for (auto* feature : _registeredFeatures) {
			if (orderedSet.contains(feature)
				|| feature->GetState().runtimeState == FeatureRuntimeState::kFailed
				|| !feature->GetState().desiredActive) {
				continue;
			}

			feature->SetRuntimeState(FeatureRuntimeState::kFailed, "Feature requirement order could not be resolved");
		}
	}

	void FeatureManager::ActivateAll()
	{
		_loadedFeatures.clear();

		const auto failPendingRequirementValidation = [](Feature& a_feature, std::string_view a_reason) noexcept {
			a_feature.SetRuntimeStateOnly(FeatureRuntimeState::kFailed);
			try {
				const auto reason = a_reason.empty() ? std::string_view("standard exception") : a_reason.substr(0, 256);
				std::string detail = "ActivateAll requirement validation threw: ";
				detail.append(reason);
				a_feature.SetRuntimeState(FeatureRuntimeState::kFailed, std::move(detail));
			} catch (...) {
				try {
					a_feature.SetRuntimeState(
						FeatureRuntimeState::kFailed,
						"ActivateAll requirement validation failed");
				} catch (...) {
				}
			}
		};

		for (auto* feature : _activationOrder) {
			const auto& state = feature->GetState();
			if (state.runtimeState == FeatureRuntimeState::kFailed
				|| state.runtimeState == FeatureRuntimeState::kDegraded) {
				continue;
			}
			if (state.runtimeState == FeatureRuntimeState::kInactive) {
				if (state.installed) {
					L->info("Feature {} inactive per TOML configuration; skipping", feature->GetName());
				} else {
					L->info("Feature {} not installed (no TOML configuration); skipping", feature->GetName());
				}
				continue;
			}
			const bool wasActive = state.runtimeState == FeatureRuntimeState::kActive;
			if (!state.desiredActive
				|| (!wasActive && state.runtimeState != FeatureRuntimeState::kPending)) {
				continue;
			}

			std::optional<std::string> requirementFailure;
			try {
				requirementFailure = FindRequirementFailure(*feature);
			} catch (const std::exception& e) {
				if (wasActive) {
					QuarantineRuntimeCallback(*feature, "ActivateAll requirement validation", e.what());
				} else {
					failPendingRequirementValidation(*feature, e.what());
				}
				continue;
			} catch (...) {
				if (wasActive) {
					QuarantineRuntimeCallback(
						*feature,
						"ActivateAll requirement validation",
						"non-standard exception");
				} else {
					failPendingRequirementValidation(*feature, "non-standard exception");
				}
				continue;
			}
			if (requirementFailure) {
				if (wasActive) {
					QuarantineRuntimeCallback(*feature, "ActivateAll", *requirementFailure);
				} else {
					L->error("Feature {} requirement failed: {}; skipping", feature->GetName(), *requirementFailure);
					feature->SetRuntimeState(FeatureRuntimeState::kFailed, *requirementFailure);
				}
				continue;
			}

			if (wasActive) {
				_loadedFeatures.push_back(feature);
				continue;
			}

			L->info("Loading feature: {}", feature->GetName());
			try {
				const auto result = feature->Activate();
				feature->ApplyActivationResult(result);
				switch (result.GetOutcome()) {
				case ActivationOutcome::kFailed:
					L->error("Feature {} reported a load failure: {}", feature->GetName(), result.GetDetail());
					break;
				case ActivationOutcome::kActive:
					_loadedFeatures.push_back(feature);
					break;
				case ActivationOutcome::kDegraded:
					L->error("Feature {} reported degraded activation: {}", feature->GetName(), result.GetDetail());
					break;
				}
			} catch (const std::exception& e) {
				feature->ApplyActivationResult(ActivationResult::Degraded(e.what()));
				L->error("Feature {} threw during Activate(): {}", feature->GetName(), e.what());
			} catch (...) {
				feature->ApplyActivationResult(ActivationResult::Degraded("Non-standard exception thrown during Activate()"));
				L->error("Feature {} threw a non-standard exception during Activate()", feature->GetName());
			}
		}

		for (auto* feature : _registeredFeatures) {
			const auto& state = feature->GetState();
			try {
				if (state.detail.empty()) {
					L->info(
						"Feature state: {} state={} installed={} desired={}",
						feature->GetName(),
						FeatureRuntimeStateName(state.runtimeState),
						state.installed,
						state.desiredActive);
				} else {
					L->info(
						"Feature state: {} state={} installed={} desired={} detail={}",
						feature->GetName(),
						FeatureRuntimeStateName(state.runtimeState),
						state.installed,
						state.desiredActive,
						std::string_view(state.detail).substr(0, 256));
				}
			} catch (...) {
			}
		}
	}

	void FeatureManager::OnDataLoadedAll()
	{
		DispatchRuntimeCallbacks(*this, "OnDataLoaded", [](Feature& a_feature) {
			a_feature.OnDataLoaded();
		});
	}

	void FeatureManager::OnPostPostLoadAll()
	{
		DispatchRuntimeCallbacks(*this, "OnPostPostLoad", [](Feature& a_feature) {
			a_feature.OnPostPostLoad();
		});
		PresetManager::Get().ResolveAndApplyBootPreset();
	}

	void FeatureManager::OnD3D11ReadyAll(IDXGIAdapter* a_adapter, ID3D11Device* a_device)
	{
		if (_d3d11ReadyDone) {
			return;
		}
		_d3d11ReadyDone = true;
		DispatchRuntimeCallbacks(*this, "OnD3D11Ready", [a_adapter, a_device](Feature& a_feature) {
			a_feature.OnD3D11Ready(a_adapter, a_device);
		});
	}
}
