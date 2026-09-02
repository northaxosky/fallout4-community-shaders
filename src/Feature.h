#pragma once

#include "DebugView.h"
#include "FeatureCategories.h"
#include "FeatureState.h"
#include "Utils/RestartSettings.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

struct IDXGIAdapter;
struct ID3D11Device;
namespace cs
{
	struct PresetApplyContext;
	namespace telemetry
	{
		class Sink;
	}

	class Feature
	{
	public:
		virtual ~Feature() = default;

		virtual std::string_view GetName() const = 0;
		virtual std::string_view GetDisplayName() const { return GetName(); }
		virtual std::string GetConfigKey() const { return std::string(GetName()); }
		spdlog::logger* Log() const;
		virtual EnbPolicy GetEnbPolicy() const { return EnbPolicy::kRunAnyway; }
		virtual bool IsInstalled() const;

		const FeatureState& GetState() const noexcept { return _state; }
		bool IsActive() const noexcept { return _state.IsActive(); }
		bool IsDegraded() const noexcept { return _state.IsDegraded(); }
		bool IsHealthy() const noexcept { return _state.IsHealthy(); }
		bool IsLoaded() const noexcept { return IsHealthy(); }

		// Configure must remain side-effect-free.
		virtual bool Configure(const toml::table& , std::string& ) { return true; }
		virtual void Load() {}
		virtual ActivationResult Activate();
		virtual void OnDataLoaded() {}

		// Defer wrappers until every feature loads.
		virtual void OnPostPostLoad() {}

		// Failed loads skip the feature.
		void FailLoad(std::string a_reason) noexcept
		{
			_loadFailed = true;
			_loadFailureReason = std::move(a_reason);
		}

		virtual void DrawSettings() {}
		virtual settings::RestartSettingsView GetRestartSettings() const noexcept { return {}; }

		virtual bool ProducesTelemetry() const { return false; }
		// Telemetry must read only cached or atomic state.
		virtual void CollectTelemetry(telemetry::Sink& ) const {}

		virtual std::span<const FeatureDebugView> GetDebugViews() const noexcept { return {}; }
		virtual void SetDebugView(std::string_view ) noexcept {}

		virtual void RestoreDefaultSettings() {}

		virtual bool HasResettableSettings() const { return false; }

		// Overlays render even while settings are closed.
		virtual void DrawOverlay() {}

		// True while DrawOverlay needs frames.
		virtual bool IsOverlayActive() const { return false; }

		// Fires after D3D11 device creation.
		virtual void OnD3D11Ready(IDXGIAdapter* , ID3D11Device* ) {}

		// Fires once after shader injections freeze, before the first frame.
		virtual bool ValidateShaderInjections(std::string& ) { return true; }

		virtual std::string GetFeatureSummary() const { return {}; }

		virtual std::optional<std::string> GetFeatureModLink() const { return {}; }

		virtual std::string GetCategory() const { return FeatureCategories::kMisc; }

		virtual bool IsInMenu() const { return true; }

		// Inactive features defer menu wiring.
		virtual void DrawUnloadedUI() {}

		// Show load failures instead of silently skipping.
		virtual void DrawFailLoadMessage() {}

		// Features join presets by default.
		virtual bool ParticipatesInPresets() const { return false; }

		// Presets skip test-mode features.
		virtual bool IsInTestMode() const { return false; }

		// Multi-word features should override the snake_case key.
		virtual std::string GetPresetKey() const;

		// Staging must not mutate live state.
		virtual bool StageFromPreset(const toml::table& ,
									 const PresetApplyContext& ,
									 std::string& ) { return true; }

		// Swap all staged state before finalization.
		virtual void CommitStagedSwap() noexcept {}
		virtual void CommitStagedFinalize() {}

		// Empty output opts out of saving.
		virtual void ExportToPreset(toml::table& ) {}

	private:
		friend class FeatureManager;
		void SetState(FeatureState a_state) { _state = std::move(a_state); }
		void SetRuntimeState(FeatureRuntimeState a_state, std::string a_detail = {})
		{
			_state.runtimeState = a_state;
			_state.detail = std::move(a_detail);
		}
		void ApplyActivationResult(const ActivationResult& a_result) { _state.ApplyActivationResult(a_result); }
		void SetRuntimeStateOnly(FeatureRuntimeState a_state) noexcept { _state.runtimeState = a_state; }
		void ResetLoadFailure() noexcept
		{
			_loadFailed = false;
			_loadFailureReason.clear();
		}
		bool HasLoadFailed() const noexcept { return _loadFailed; }
		const std::string& LoadFailureReason() const noexcept { return _loadFailureReason; }

		FeatureState            _state;
		mutable spdlog::logger* _log = nullptr;
		bool                    _loadFailed = false;
		std::string             _loadFailureReason;
	};

#ifdef TRACY_SUPPORT
	namespace detail
	{
		inline std::string FeatureZoneName(const Feature* a_feature, std::string_view a_method)
		{
			const auto featureName = a_feature->GetName();
			std::string name;
			name.reserve(featureName.size() + a_method.size() + 1);
			name.append(featureName.data(), featureName.size());
			name.push_back(':');
			name.append(a_method.data(), a_method.size());
			return name;
		}
	}
#endif

	class FeatureManager
	{
	public:
		static FeatureManager& Get();

		void Register(Feature* a_feature);

		void PrepareAll();
		void ActivateAll();
		void OnDataLoadedAll();
		void OnPostPostLoadAll();

		// Callback failures quarantine features without unloading them.
		bool PrepareRuntimeCallback(Feature& a_feature, std::string_view a_phase) noexcept;
		bool PrepareMenuCallback(Feature& a_feature, std::string_view a_phase) noexcept;
		void QuarantineRuntimeCallback(
			Feature& a_feature,
			std::string_view a_phase,
			std::string_view a_reason) noexcept;
		void FinishRuntimeCallbackPass() noexcept;

		// Notify each feature on first D3D11 device creation.
		void OnD3D11ReadyAll(IDXGIAdapter* a_adapter, ID3D11Device* a_device);

		// Runs after the injection freeze so features can reject a partial delivery path.
		void ValidateShaderInjectionsAll();

		const std::vector<Feature*>& GetAll() const noexcept { return _loadedFeatures; }
		const std::vector<Feature*>& GetRegisteredFeatures() const noexcept { return _registeredFeatures; }
		bool ApplyDebugViews(std::span<const FeatureDebugSelection> a_selections);

	private:
		FeatureManager() = default;
		std::vector<Feature*> _registeredFeatures;
		std::vector<Feature*> _loadedFeatures;
		bool                  _d3d11ReadyDone = false;
	};
}

#ifdef TRACY_SUPPORT
#define CS_DETAIL_CONCAT_INNER(a, b) a##b
#define CS_DETAIL_CONCAT(a, b) CS_DETAIL_CONCAT_INNER(a, b)
#define CS_FEATURE_ZONE_IMPL(featurePtr, methodLiteral, id) \
	const auto CS_DETAIL_CONCAT(csFeatureZoneName_, id) = ::cs::detail::FeatureZoneName((featurePtr), (methodLiteral)); \
	ZoneTransientN(CS_DETAIL_CONCAT(csFeatureZone_, id), CS_DETAIL_CONCAT(csFeatureZoneName_, id).c_str(), true)
#define CS_FEATURE_ZONE(featurePtr, methodLiteral) CS_FEATURE_ZONE_IMPL(featurePtr, methodLiteral, __COUNTER__)
#else
#define CS_FEATURE_ZONE(featurePtr, methodLiteral) ((void)0)
#endif
