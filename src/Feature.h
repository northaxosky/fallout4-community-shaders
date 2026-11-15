#pragma once

#include "DebugView.h"
#include "FeatureCategories.h"
#include "FeatureState.h"
#include "Render/ShaderDefineProvider.h"
#include "Settings/LiveSettings.h"
#include "Settings/SettingsMetadata.h"

#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

struct IDXGIAdapter;
struct ID3D11Device;
namespace cs::render
{
	struct SharedFeatureDataCB;
	struct FO4SharedDataCB;
	struct SharedDataCB;
}

namespace cs
{
	namespace settings
	{
		class SettingsEdit;
	}

	namespace feature_config
	{
		struct ShaderOwnershipConfig;
	}

	namespace weather
	{
		class Profiles;
	}
	namespace telemetry
	{
		class Sink;
	}

	class Feature : public engine::ShaderDefineProvider
	{
	public:
		virtual ~Feature() = default;

		virtual std::string_view GetName() const = 0;
		virtual std::string_view GetDisplayName() const { return GetName(); }
		virtual std::string GetConfigKey() const { return std::string(GetName()); }
		spdlog::logger* Log() const;
		virtual bool IsInstalled() const;

		FeatureState GetState() const
		{
			std::scoped_lock lock(_stateMutex);
			return { _desiredActive, _runtimeState.load(std::memory_order_acquire), _stateDetail };
		}
		bool IsActive() const noexcept
		{
			const auto state = _runtimeState.load(std::memory_order_acquire);
			return state == FeatureRuntimeState::kActive || state == FeatureRuntimeState::kDegraded;
		}
		bool IsDegraded() const noexcept { return _runtimeState.load(std::memory_order_acquire) == FeatureRuntimeState::kDegraded; }
		bool IsHealthy() const noexcept { return _runtimeState.load(std::memory_order_acquire) == FeatureRuntimeState::kActive; }
		bool IsLoaded() const noexcept override { return _loaded; }
		Feature* GetRuntimeOwner() const noexcept override { return const_cast<Feature*>(this); }

		// Framework-owned live switch; opt-outs are always switched on.
		virtual bool SupportsEnableToggle() const { return true; }
		virtual bool DefaultEnabled() const { return true; }
		// Effective state: the switch, unless shader ownership suspends the feature.
		bool IsEnabled() const noexcept
		{
			return IsSwitchedOn() && !_suspended.load(std::memory_order_acquire);
		}
		// The user's switch alone, for UI and persistence.
		bool IsSwitchedOn() const noexcept
		{
			return !SupportsEnableToggle() || _enabled.load(std::memory_order_acquire);
		}
		bool IsSuspended() const noexcept { return _suspended.load(std::memory_order_acquire); }
		// Host-frame thread; readers are on the render path.
		void SetEnabled(bool a_enabled) noexcept;

		// Configure must remain side-effect-free.
		virtual bool Configure(const toml::table&, std::string&) { return true; }
		virtual void Load() {}
		virtual ActivationResult Activate();
		virtual void OnDataLoaded() {}
		// Runs on the UI event thread; queue render-thread work instead of doing it.
		virtual void OnLoadingMenuClosed() {}
		virtual void Prepass() {}
		virtual void WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB&) {}
		// Fills the b5 frame block the host built; the b6 and b8 writers cannot reach it.
		virtual void WriteSharedFrameData(render::SharedDataCB&) {}
		virtual void OnRuntimeQuarantined(std::string_view, std::string_view) noexcept {}
		// Fires after IsEnabled() flips; resources stay alive while disabled.
		virtual void OnEnabledChanged(bool) noexcept {}

		// Defer wrappers until every feature loads.
		virtual void OnPostPostLoad() {}

		// Failed loads skip the feature.
		void FailLoad(std::string a_reason) noexcept
		{
			_loadFailed = true;
			_loadFailureReason = std::move(a_reason);
		}

		virtual void DrawSettings() {}
		// False keeps recorded edits pending for a later flush.
		virtual bool SaveSettings() { return true; }
		bool Persist();
		virtual settings::SettingsBinding GetSettingsBinding() const { return {}; }
		settings::SchemaView GetSettingsSchema() const
		{
			const auto binding = GetSettingsBinding();
			return binding ? binding.schema() : settings::SchemaView{};
		}
		const settings::LiveSettingsAccess& GetLiveSettingsAccess() const noexcept { return _liveSettings; }

		// Persists edits recorded by settings::SettingsEdit, optionally only once an edit completes.
		void FlushSettings(bool a_completedOnly = false);
		virtual std::vector<std::string_view> GetRestartSettings() const { return {}; }
		// Per-weather overrides from the `weather` table; null when absent.
		virtual weather::Profiles* GetWeatherProfiles() { return nullptr; }
		// The CS Editor draws a section with this title; empty opts out.
		virtual std::string_view GetWeatherAnalysisTitle() const { return {}; }
		virtual void DrawWeatherAnalysis() {}

		virtual bool ProducesTelemetry() const { return false; }
		// Telemetry must read only cached or atomic state.
		virtual void CollectTelemetry(telemetry::Sink&) const {}

		// Feature-specific diagnostics drawn in the host's Debug section.
		virtual bool HasDebugUI() const { return false; }
		virtual void DrawDebug() {}

		virtual std::span<const FeatureDebugView> GetDebugViews() const noexcept { return {}; }
		virtual void SetDebugView(std::string_view) noexcept {}
		// Render thread only; the texture is borrowed until the next render callback.
		virtual FullscreenDebugData GetFullscreenDebugData() const noexcept { return {}; }

		virtual void RestoreDefaultSettings() {}
		bool RestoreDefaults();

		bool HasResettableSettings() const { return SupportsEnableToggle() || static_cast<bool>(GetSettingsBinding()); }
		bool HasModifiedSettings() const
		{
			const auto binding = GetSettingsBinding();
			return (binding && binding.modified()) ||
			       (SupportsEnableToggle() && IsSwitchedOn() != DefaultEnabled());
		}

		// Overlays render even while settings are closed.
		virtual void DrawOverlay() {}

		// True while DrawOverlay needs frames.
		virtual bool IsOverlayActive() const { return false; }

		// Fires after D3D11 device creation.
		virtual void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) {}

		// Fires once after shader injections freeze, before the first frame.
		virtual bool ValidateShaderInjections(std::string&) { return true; }
		// Re-derives ownership-dependent state; returns the targets now unowned.
		virtual std::vector<engine::ShaderInjectionTarget> RefreshShaderOwnership() { return {}; }
		// True suspends the whole feature while a registered target is unowned.
		virtual bool FollowsShaderOwnership() const { return false; }

		virtual std::string GetFeatureSummary() const { return {}; }

		virtual std::optional<std::string> GetFeatureModLink() const { return {}; }

		virtual std::string GetCategory() const { return FeatureCategories::kOther; }

		virtual bool IsInMenu() const { return true; }

		// Inactive features defer menu wiring.
		virtual void DrawUnloadedUI() {}

		// Show load failures instead of silently skipping.
		virtual void DrawFailLoadMessage() {}

	protected:
		bool SaveEnabled();
		settings::LiveSettingsAccess _liveSettings;

	private:
		friend class settings::SettingsEdit;
		friend class FeatureManager;
		void SetState(FeatureState a_state)
		{
			std::scoped_lock lock(_stateMutex);
			_desiredActive = a_state.desiredActive;
			_stateDetail = std::move(a_state.detail);
			_runtimeState.store(a_state.runtimeState, std::memory_order_release);
		}
		void SetRuntimeState(FeatureRuntimeState a_state, std::string a_detail = {})
		{
			std::scoped_lock lock(_stateMutex);
			_stateDetail = std::move(a_detail);
			_runtimeState.store(a_state, std::memory_order_release);
		}
		void ApplyActivationResult(const ActivationResult& a_result)
		{
			auto state = GetState();
			state.ApplyActivationResult(a_result);
			SetState(std::move(state));
		}
		// Flips the effective state like SetEnabled, firing OnEnabledChanged.
		void SetSuspended(bool a_suspended) noexcept;
		// Active features carry no detail, so it can hold a neutral note.
		void SetActiveNote(std::string a_note)
		{
			std::scoped_lock lock(_stateMutex);
			if (_runtimeState.load(std::memory_order_acquire) == FeatureRuntimeState::kActive)
				_stateDetail = std::move(a_note);
		}
		bool BeginRuntimeQuarantine() noexcept
		{
			auto expected = FeatureRuntimeState::kActive;
			return _runtimeState.compare_exchange_strong(expected, FeatureRuntimeState::kDegraded, std::memory_order_acq_rel);
		}
		void ComposeEnabledLiveSettings();
		void ResetLoadFailure() noexcept
		{
			_loadFailed = false;
			_loadFailureReason.clear();
		}
		bool HasLoadFailed() const noexcept { return _loadFailed; }
		const std::string& LoadFailureReason() const noexcept { return _loadFailureReason; }

		mutable std::mutex _stateMutex;
		bool _desiredActive = false;
		std::string _stateDetail;
		std::atomic<FeatureRuntimeState> _runtimeState{ FeatureRuntimeState::kPending };
		mutable spdlog::logger* _log = nullptr;
		bool _loadFailed = false;
		bool _loaded = false;
		std::atomic_bool _enabled{ true };
		std::atomic_bool _suspended{ false };
		bool _settingsSavePending = false;
		bool _settingsEditCompleted = false;
		std::string _loadFailureReason;
	};

	// Operational = validated and owned; only ShaderInjectionFeature writes it.
	class InjectionGate
	{
	public:
		bool load(std::memory_order a_order = std::memory_order_acquire) const noexcept
		{
			return _bits.load(a_order) == (kValidated | kOwned);
		}

	private:
		friend class ShaderInjectionFeature;
		static constexpr std::uint8_t kValidated = 1;
		static constexpr std::uint8_t kOwned = 2;
		void Set(std::uint8_t a_bit, bool a_on) noexcept
		{
			if (a_on)
				_bits.fetch_or(a_bit, std::memory_order_acq_rel);
			else
				_bits.fetch_and(static_cast<std::uint8_t>(~a_bit), std::memory_order_acq_rel);
		}
		std::atomic<std::uint8_t> _bits{ 0 };
	};

	// Owns the registration/operational flags and the common injection validation.
	class ShaderInjectionFeature : public Feature
	{
	public:
		// Registered, prerequisites, routes, variants; validated if all pass.
		bool ValidateShaderInjections(std::string& a_error) final;
		bool IsInjectionOperational() const noexcept { return _injectionsOperational.load(std::memory_order_acquire); }
		std::string GetInjectionValidationError() const;
		std::vector<engine::ShaderInjectionTarget> RefreshShaderOwnership() final;
		// Presentation features override this to false; only their injections stop.
		bool FollowsShaderOwnership() const override { return true; }

	protected:
		virtual std::string_view GetRegistrationFailureMessage() const { return "the shader contribution did not register"; }
		// Runs before route validation; the feature's own readiness checks.
		virtual bool ValidateInjectionPrerequisites(std::string&) { return true; }
		// Runs after route validation, once the routes exist.
		virtual bool PrepareInjectionVariants(std::string&) { return true; }
		static bool RequireSharedData(std::string& a_error, std::string_view a_consequence = {});

		std::atomic_bool _registrationsReady{ false };
		// Permanently drops the feature's injections, e.g. after a quarantine.
		void RevokeInjections() noexcept { _injectionsOperational.Set(InjectionGate::kValidated, false); }

		InjectionGate _injectionsOperational;

	private:
		mutable std::mutex _injectionErrorMutex;
		std::string _injectionError;
	};

	template <const engine::ShaderDefineDeclaration& Declaration>
	class ShaderFeature : public ShaderInjectionFeature
	{
	public:
		std::string_view GetShaderDefineName() const override { return Declaration.GetShaderDefineName(); }
		engine::ShaderDefineOptions GetShaderDefineOptions(engine::ShaderInjectionTarget a_target = engine::ShaderInjectionTarget::kCount) const override { return Declaration.GetShaderDefineOptions(a_target, IsFullscreenDebugSelected()); }
		bool HasShaderDefine(engine::ShaderInjectionTarget a_target) const override { return Declaration.HasShaderDefine(a_target); }
		bool RequiresShaderGraphicsPair(engine::ShaderInjectionTarget a_target) const override { return Declaration.RequiresShaderGraphicsPair(a_target); }
		const engine::ShaderDefineProvider* GetShaderDefineDeclaration() const noexcept override { return &Declaration; }
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
		void OnLoadingMenuClosedAll();
		void OnPostPostLoadAll();
		void PrepassAll();

		// Health gate only; disabled features still get callbacks.
		// Callback failures quarantine features without unloading them.
		bool PrepareRuntimeCallback(Feature& a_feature, std::string_view a_phase) noexcept;
		bool PrepareMenuCallback(Feature& a_feature, std::string_view a_phase) noexcept;
		void QuarantineRuntimeCallback(
			Feature& a_feature,
			std::string_view a_phase,
			std::string_view a_reason) noexcept;

		// Health-gates a_fn and quarantines the feature if it throws.
		template <class Fn>
		void RunRuntimeCallback(Feature& a_feature, std::string_view a_phase, Fn&& a_fn) noexcept
		{
			if (!PrepareRuntimeCallback(a_feature, a_phase))
				return;
			try {
				a_fn();
			} catch (const std::exception& e) {
				QuarantineRuntimeCallback(a_feature, a_phase, e.what());
			} catch (...) {
				QuarantineRuntimeCallback(a_feature, a_phase, "non-standard exception");
			}
		}

		std::optional<settings::PreparedLiveSettings> PrepareLiveSettings(
			Feature& a_feature,
			const toml::table& a_values,
			std::string_view a_phase,
			std::string& a_error);
		std::optional<settings::PreparedLiveSettings> PrepareLiveSettingsFromOverrides(
			Feature& a_feature,
			const toml::table* a_overrides,
			const toml::node* a_enabled,
			std::string_view a_phase,
			std::string& a_error);
		// Keeps the feature loaded but marks part of it unavailable.
		void ReportDegraded(Feature& a_feature, std::string a_detail);
		bool SaveAllSettings(std::string& a_error);
		bool LoadAllSettings(std::string& a_error);
		bool ResetAllSettings(std::string& a_error);

		// Notify each feature on first D3D11 device creation.
		void OnD3D11ReadyAll(IDXGIAdapter* a_adapter, ID3D11Device* a_device);

		// Runs after the injection freeze so features can reject a partial delivery path.
		void ValidateShaderInjectionsAll();

		// Sole route for ownership changes; refreshes every feature.
		void ApplyShaderOwnership(const feature_config::ShaderOwnershipConfig& a_config);

		void WriteSharedDataAll(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB& a_fo4);
		void WriteSharedFrameDataAll(render::SharedDataCB& a_shared);

		auto GetAll() const noexcept
		{
			return _loadedFeatures | std::views::filter([](Feature* a_feature) noexcept {
				return a_feature && a_feature->IsHealthy();
			});
		}
		const std::vector<Feature*>& GetRegisteredFeatures() const noexcept { return _registeredFeatures; }
		bool ApplyDebugViews(std::span<const FeatureDebugSelection> a_selections);
		Feature* GetFullscreenDebugFeature() const noexcept
		{
			return _fullscreenDebugFeature.load(std::memory_order_acquire);
		}

	private:
		FeatureManager() = default;
		void RefreshShaderOwnership();
		std::vector<Feature*> _registeredFeatures;
		std::vector<Feature*> _loadedFeatures;
		std::atomic<Feature*> _fullscreenDebugFeature{ nullptr };
		bool _d3d11ReadyDone = false;
		std::string _ownershipSummary;
	};
}

#ifdef TRACY_SUPPORT
#	define CS_DETAIL_CONCAT_INNER(a, b) a##b
#	define CS_DETAIL_CONCAT(a, b) CS_DETAIL_CONCAT_INNER(a, b)
#	define CS_FEATURE_ZONE_IMPL(featurePtr, methodLiteral, id)                                                             \
		const auto CS_DETAIL_CONCAT(csFeatureZoneName_, id) = ::cs::detail::FeatureZoneName((featurePtr), (methodLiteral)); \
		ZoneTransientN(CS_DETAIL_CONCAT(csFeatureZone_, id), CS_DETAIL_CONCAT(csFeatureZoneName_, id).c_str(), true)
#	define CS_FEATURE_ZONE(featurePtr, methodLiteral) CS_FEATURE_ZONE_IMPL(featurePtr, methodLiteral, __COUNTER__)
#else
#	define CS_FEATURE_ZONE(featurePtr, methodLiteral) ((void)0)
#endif
