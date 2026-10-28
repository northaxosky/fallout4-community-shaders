#include "Feature.h"

#include "Env.h"
#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
#include "Settings/FeatureKeys.h"
#include "Settings/PresetManager.h"
#include "Settings/SettingsPersistence.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
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
			if (!feature) {
				continue;
			}
			a_manager.RunRuntimeCallback(*feature, a_phase, [&] {
				a_callback(*feature);
			});
		}
	}

	class LoadingMenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent& a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.menuName == RE::LoadingMenu::MENU_NAME && !a_event.opening) {
				cs::render::RequestHistoryReset();
				cs::FeatureManager::Get().OnLoadingMenuClosedAll();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
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

	void Feature::SetEnabled(bool a_enabled) noexcept
	{
		if (_enabled.exchange(a_enabled, std::memory_order_acq_rel) != a_enabled && SupportsEnableToggle())
			OnEnabledChanged(a_enabled);
	}

	bool Feature::SaveEnabled()
	{
		return !SupportsEnableToggle() || settings::SaveEnabled(GetConfigKey(), IsEnabled(), *Log());
	}

	bool Feature::Persist()
	{
		return SaveSettings() && SaveEnabled();
	}

	void Feature::FlushSettings(bool a_completedOnly)
	{
		if (!_settingsSavePending || (a_completedOnly && !_settingsEditCompleted))
			return;
		_settingsEditCompleted = false;
		if (Persist())
			_settingsSavePending = false;
	}

	bool Feature::RestoreDefaults()
	{
		RestoreDefaultSettings();
		if (SupportsEnableToggle()) {
			SetEnabled(DefaultEnabled());
		}
		return Persist();
	}

	void Feature::ComposeEnabledLiveSettings()
	{
		if (!SupportsEnableToggle())
			return;
		_liveSettings = {
			[this, inner = std::move(_liveSettings.snapshot)] {
				auto snapshot = inner ? inner() : toml::table{};
				snapshot.insert_or_assign("enabled", _enabled.load(std::memory_order_acquire));
				return snapshot;
			},
			[this, inner = std::move(_liveSettings.prepare)](
				const toml::table& a_snapshot, std::string& a_error) -> std::optional<settings::PreparedLiveSettings> {
				bool enabled = _enabled.load(std::memory_order_acquire);
				if (!settings::AcceptSetting(feature_config::ReadBool(a_snapshot, "enabled", enabled), "enabled", "boolean", a_error))
					return std::nullopt;
				settings::PreparedLiveSettings prepared;
				if (inner) {
					auto value = inner(a_snapshot, a_error);
					if (!value)
						return std::nullopt;
					prepared = std::move(*value);
				}
				prepared.swap = [this, enabled, featureSwap = std::move(prepared.swap)]() mutable {
					if (featureSwap)
						featureSwap();
					const bool previous = _enabled.load(std::memory_order_acquire);
					SetEnabled(enabled);
					enabled = previous;
				};
				return prepared;
			},
			std::move(_liveSettings.notComparable)
		};
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
		std::ranges::stable_sort(_registeredFeatures, {}, [](const Feature* feature) {
			return std::ranges::find(feature_config::kAllFeatureKeys, feature->GetConfigKey()) -
			       feature_config::kAllFeatureKeys.begin();
		});
	}

	bool FeatureManager::PrepareRuntimeCallback(Feature& a_feature, std::string_view) noexcept
	{
		return a_feature.IsHealthy();
	}

	bool FeatureManager::PrepareMenuCallback(Feature& a_feature, std::string_view a_phase) noexcept
	{
		if (a_feature.IsHealthy()) {
			return PrepareRuntimeCallback(a_feature, a_phase);
		}
		return a_feature._runtimeState.load(std::memory_order_acquire) == FeatureRuntimeState::kInactive;
	}

	void FeatureManager::QuarantineRuntimeCallback(
		Feature& a_feature,
		std::string_view a_phase,
		std::string_view a_reason) noexcept
	{
		if (!a_feature.BeginRuntimeQuarantine()) {
			return;
		}

		a_feature.OnRuntimeQuarantined(a_phase, a_reason);
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

	std::optional<settings::PreparedLiveSettings> FeatureManager::PrepareLiveSettings(
		Feature& a_feature,
		const toml::table& a_values,
		std::string_view a_phase,
		std::string& a_error)
	{
		const auto& access = a_feature.GetLiveSettingsAccess();
		if (!access.prepare) {
			a_error = "feature has no live settings";
			return std::nullopt;
		}
		auto value = access.prepare(a_values, a_error);
		if (value && value->finalize) {
			value->finalize = [this, feature = &a_feature, phase = std::string(a_phase), finalize = std::move(value->finalize)] {
				if (!PrepareRuntimeCallback(*feature, phase))
					return;
				try {
					finalize();
				} catch (const std::exception& error) {
					QuarantineRuntimeCallback(*feature, phase, error.what());
					throw;
				} catch (...) {
					QuarantineRuntimeCallback(*feature, phase, "non-standard exception");
					throw;
				}
			};
		}
		return value;
	}

	std::optional<settings::PreparedLiveSettings> FeatureManager::PrepareLiveSettingsFromOverrides(
		Feature& a_feature,
		const toml::table* a_overrides,
		const toml::node* a_enabled,
		std::string_view a_phase,
		std::string& a_error)
	{
		// The file stores only non-default values, so absent keys mean the default.
		auto values = settings::DefaultValues(a_feature.GetSettingsSchema());
		values.insert_or_assign("enabled", a_feature.DefaultEnabled());
		if (a_enabled)
			values.insert_or_assign("enabled", *a_enabled);
		if (a_overrides) {
			for (const auto& [key, node] : *a_overrides)
				values.insert_or_assign(key, node);
		}
		return PrepareLiveSettings(a_feature, values, a_phase, a_error);
	}

	bool FeatureManager::SaveAllSettings(std::string& a_error)
	{
		constexpr std::string_view phase = "SaveAllSettings";
		std::string failed;
		for (auto* feature : _registeredFeatures) {
			if (!PrepareMenuCallback(*feature, phase))
				continue;
			try {
				if (feature->Persist())
					continue;
			} catch (const std::exception& error) {
				L->error("{} {} threw: {}", phase, feature->GetName(), error.what());
			} catch (...) {
				L->error("{} {} threw a non-standard exception", phase, feature->GetName());
			}
			failed += (failed.empty() ? "" : ", ");
			failed += feature->GetName();
		}
		if (!failed.empty())
			a_error = "failed to save: " + failed;
		return failed.empty();
	}

	bool FeatureManager::LoadAllSettings(std::string& a_error)
	{
		const auto reloaded = feature_config::Reload();
		if (reloaded.status != feature_config::FileLoadStatus::kParsed &&
			reloaded.status != feature_config::FileLoadStatus::kMissing) {
			a_error = reloaded.error;
			return false;
		}
		for (const auto& warning : reloaded.warnings)
			L->warn("{}", warning);
		std::vector<settings::PreparedLiveSettings> prepared;
		for (auto* feature : GetAll()) {
			if (!feature->GetLiveSettingsAccess().prepare)
				continue;
			const auto stored = feature_config::GetFeature(feature->GetConfigKey());
			const auto* enabled = stored ? stored->get("enabled") : nullptr;
			const auto* overrides = stored ? stored->get_as<toml::table>("settings") : nullptr;
			std::string error;
			auto value = PrepareLiveSettingsFromOverrides(
				*feature, overrides, enabled, "LoadAllSettings::Finalize", error);
			if (!value) {
				a_error = std::format("{}: {}", feature->GetName(), error);
				return false;
			}
			prepared.push_back(std::move(*value));
		}
		try {
			settings::ApplyPreparedLiveSettings(prepared);
		} catch (const std::exception& error) {
			a_error = error.what();
			return false;
		} catch (...) {
			a_error = "non-standard exception";
			return false;
		}
		return true;
	}

	bool FeatureManager::ResetAllSettings(std::string& a_error)
	{
		constexpr std::string_view phase = "ResetAllSettings";
		std::string failed;
		for (auto* feature : GetAll()) {
			if (!PrepareRuntimeCallback(*feature, phase))
				continue;
			try {
				if (feature->RestoreDefaults())
					continue;
			} catch (const std::exception& error) {
				a_error = std::format("{}: {}", feature->GetName(), error.what());
				return false;
			} catch (...) {
				a_error = std::format("{}: non-standard exception", feature->GetName());
				return false;
			}
			failed += (failed.empty() ? "" : ", ");
			failed += feature->GetName();
		}
		if (!failed.empty())
			a_error = "failed to save: " + failed;
		return failed.empty();
	}

	bool FeatureManager::ApplyDebugViews(
		std::span<const FeatureDebugSelection> a_selections)
	{
		std::unordered_map<Feature*, std::string_view> selected;
		Feature* fullscreen = nullptr;
		auto healthy = GetAll();
		for (const auto& selection : a_selections) {
			if (selection.feature.empty() || selection.view.empty())
				return false;
			const auto featureIt = std::ranges::find(
				healthy, selection.feature, &Feature::GetName);
			if (featureIt == healthy.end())
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

		_fullscreenDebugFeature.store(nullptr, std::memory_order_release);
		std::vector<engine::ShaderInjectionTarget> changedTargets;
		for (auto* feature : _registeredFeatures) {
			if (!feature)
				continue;
			const auto selection = selected.find(feature);
			feature->SetDebugView(
				selection == selected.end() ?
					std::string_view{} :
					selection->second);
			for (const auto target : feature->SetFullscreenDebugSelected(feature == fullscreen)) {
				if (std::ranges::find(changedTargets, target) == changedTargets.end())
					changedTargets.push_back(target);
			}
		}
		engine::InvalidateNativeShaderVariantCompilations(changedTargets);
		_fullscreenDebugFeature.store(fullscreen, std::memory_order_release);
		return true;
	}

	void FeatureManager::PrepareAll()
	{
		_loadedFeatures.clear();
		for (auto* feature : _registeredFeatures) {
			feature->SetState({});
			feature->_enabled.store(feature->DefaultEnabled(), std::memory_order_release);
		}

		const auto configRoot = feature_config::GetRoot();
		const auto* features = configRoot["features"].as_table();

		const auto failConfiguration = [](Feature* a_feature, std::string a_detail) {
			if (a_detail.empty()) {
				a_detail = "Feature configuration failed";
			}
			a_feature->SetRuntimeState(FeatureRuntimeState::kFailed, a_detail);
			L->error("Feature {} configuration failed: {}", a_feature->GetName(), a_detail);
		};

		for (auto* feature : _registeredFeatures) {
			const auto key = feature->GetConfigKey();
			const auto* featureTable = features->get(key)->as_table();
			const auto activation = feature_config::ParseActivation(*featureTable);
			if (!activation.valid) {
				L->warn("Feature {} configuration load key must be a boolean; treating as false", key);
			}

			std::string error;
			try {
				if (!feature->Configure(*featureTable, error)) {
					failConfiguration(feature, std::move(error));
					continue;
				}

				bool enabled = feature->DefaultEnabled();
				if (feature_config::ReadBool(*featureTable, "enabled", enabled) == feature_config::ScalarReadStatus::kWrongType) {
					L->warn("Feature {} configuration enabled key must be a boolean; using the default", key);
				}
				feature->_enabled.store(enabled, std::memory_order_release);
				feature->ComposeEnabledLiveSettings();

				const bool deactivatedForEnb = cs::env::IsENBLoaded();
				const bool desiredActive = activation.valid && activation.load && !deactivatedForEnb;
				feature->SetState({ .desiredActive = desiredActive,
					.runtimeState = desiredActive ? FeatureRuntimeState::kPending : FeatureRuntimeState::kInactive,
					.detail = deactivatedForEnb ? "ENB is loaded; this feature is inactive." : std::string{} });
			} catch (const std::exception& e) {
				failConfiguration(feature, e.what());
			} catch (...) {
				failConfiguration(feature, "Feature configuration threw a non-standard exception");
			}
		}
	}

	void FeatureManager::PrepassAll()
	{
		DispatchRuntimeCallbacks(*this, "Prepass", [](Feature& a_feature) { a_feature.Prepass(); });
	}

	void FeatureManager::WriteSharedDataAll(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB& a_fo4)
	{
		for (auto* feature : GetAll()) {
			if (feature->IsEnabled())
				feature->WriteSharedData(a_feature, a_fo4);
		}
	}

	void FeatureManager::ActivateAll()
	{
		_loadedFeatures.clear();

		for (auto* feature : _registeredFeatures) {
			const auto& state = feature->GetState();
			if (state.runtimeState == FeatureRuntimeState::kFailed || state.runtimeState == FeatureRuntimeState::kDegraded) {
				continue;
			}
			if (state.runtimeState == FeatureRuntimeState::kInactive) {
				L->info("Feature {} inactive per TOML configuration; skipping", feature->GetName());
				continue;
			}
			const bool wasActive = state.runtimeState == FeatureRuntimeState::kActive;
			if (!state.desiredActive || (!wasActive && state.runtimeState != FeatureRuntimeState::kPending)) {
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
					feature->_loaded = true;
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
						"Feature state: {} state={} desired={} enabled={}",
						feature->GetName(),
						FeatureRuntimeStateName(state.runtimeState),
						state.desiredActive,
						feature->IsEnabled());
				} else {
					L->info(
						"Feature state: {} state={} desired={} enabled={} detail={}",
						feature->GetName(),
						FeatureRuntimeStateName(state.runtimeState),
						state.desiredActive,
						feature->IsEnabled(),
						std::string_view(state.detail).substr(0, 256));
				}
			} catch (...) {
			}
		}
	}

	void FeatureManager::OnDataLoadedAll()
	{
		static LoadingMenuSink loadingMenuSink;
		if (auto* ui = RE::UI::GetSingleton())
			ui->RegisterSink<RE::MenuOpenCloseEvent>(&loadingMenuSink);
		else
			L->warn("UI event source unavailable; loading-menu callbacks are disabled.");
		DispatchRuntimeCallbacks(*this, "OnDataLoaded", [](Feature& a_feature) {
			a_feature.OnDataLoaded();
		});
	}

	void FeatureManager::OnLoadingMenuClosedAll()
	{
		DispatchRuntimeCallbacks(*this, "OnLoadingMenuClosed", [](Feature& a_feature) {
			a_feature.OnLoadingMenuClosed();
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
			if (!feature || !PrepareRuntimeCallback(*feature, "ValidateShaderInjections")) {
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
	}
}
