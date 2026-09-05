#include "Feature.h"

#include "Env.h"
#include "Log.h"
#include "Settings/FeatureConfig.h"
#include "Settings/PresetManager.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
	auto* L = cs::log::Get("cs");

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

	bool FeatureManager::PrepareRuntimeCallback(Feature& a_feature, std::string_view ) noexcept
	{
		return a_feature.IsHealthy();
	}

	bool FeatureManager::PrepareMenuCallback(Feature& a_feature, std::string_view a_phase) noexcept
	{
		const auto& state = a_feature.GetState();
		if (!state.installed) {
			return false;
		}
		if (a_feature.IsHealthy()) {
			return PrepareRuntimeCallback(a_feature, a_phase);
		}
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

	bool FeatureManager::ApplyDebugViews(
		std::span<const FeatureDebugSelection> a_selections)
	{
		std::unordered_map<Feature*, std::string_view> selected;
		Feature* fullscreen = nullptr;
		for (const auto& selection : a_selections) {
			if (selection.feature.empty() || selection.view.empty())
				return false;
			const auto featureIt = std::ranges::find(
				_loadedFeatures, selection.feature, &Feature::GetName);
			if (featureIt == _loadedFeatures.end())
				return false;
			const auto views = (*featureIt)->GetDebugViews();
			const auto viewIt = std::ranges::find(
				views, selection.view, &FeatureDebugView::id);
			if (viewIt == views.end() || selected.contains(*featureIt))
				return false;
			if (viewIt->kind == FeatureDebugViewKind::kFullscreen) {
				if (fullscreen)
					return false;
				fullscreen = *featureIt;
			}
			selected.emplace(*featureIt, selection.view);
		}

		for (auto* feature : _registeredFeatures) {
			if (!feature)
				continue;
			const auto selection = selected.find(feature);
			feature->SetDebugView(
				selection == selected.end() ?
					std::string_view{} :
					selection->second);
		}
		return true;
	}

	void FeatureManager::PrepareAll()
	{
		_loadedFeatures.clear();
		for (auto* feature : _registeredFeatures) {
			feature->SetState({});
		}

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

				const bool deactivatedForEnb = cs::env::IsENBLoaded();
				const bool desiredActive = activation.valid && activation.load && !deactivatedForEnb;
				feature->SetState({
					.installed = true,
					.desiredActive = desiredActive,
					.runtimeState = desiredActive ? FeatureRuntimeState::kPending : FeatureRuntimeState::kInactive,
					.detail = deactivatedForEnb
						? "ENB is loaded; this feature is inactive."
						: std::string{}
				});
			} catch (const std::exception& e) {
				failConfiguration(feature, e.what());
			} catch (...) {
				failConfiguration(feature, "Feature configuration threw a non-standard exception");
			}
		}

	}

	void FeatureManager::ActivateAll()
	{
		_loadedFeatures.clear();

		for (auto* feature : _registeredFeatures) {
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

	void FeatureManager::ValidateShaderInjectionsAll()
	{
		for (auto* feature : _loadedFeatures) {
			if (!feature
				|| !PrepareRuntimeCallback(*feature, "ValidateShaderInjections")) {
				continue;
			}

			std::string reason;
			bool valid = false;
			try {
				valid = feature->ValidateShaderInjections(reason);
			} catch (const std::exception& e) {
				reason = e.what();
			} catch (...) {
				reason = "non-standard exception";
			}
			if (valid) {
				continue;
			}

			if (reason.empty()) {
				reason = "shader injection validation failed";
			}
			L->error(
				"Feature {} failed shader-injection validation: {}",
				feature->GetName(),
				reason);
			feature->SetRuntimeState(FeatureRuntimeState::kFailed, std::move(reason));
		}
		FinishRuntimeCallbackPass();
	}
}
