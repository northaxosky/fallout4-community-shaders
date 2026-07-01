#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

struct IDXGIAdapter;
struct ID3D11Device;

namespace cs
{
	struct PresetApplyContext;

	class Feature
	{
	public:
		virtual ~Feature() = default;

		virtual std::string_view GetName() const = 0;
		virtual std::vector<std::string_view> GetDependencies() const { return {}; }
		virtual bool IsInstalled() const;

		bool IsLoaded() const noexcept { return _loaded; }

		virtual void Load() {}
		virtual void OnDataLoaded() {}

		// Runs after all features' Load(). Defer here to wrap hooks installed in another feature's Load().
		virtual void OnPostPostLoad() {}

		// A feature may call FailLoad() from within Load() to signal a recoverable failure; the
		// manager then skips it (and anything depending on it) instead of treating it as loaded.
		void FailLoad(std::string a_reason) noexcept
		{
			_loadFailed = true;
			_loadFailureReason = std::move(a_reason);
		}

		virtual void DrawSettings() {}

		// Reset persisted settings and derived caches; opt in via HasResettableSettings().
		virtual void RestoreDefaultSettings() {}

		// Opt-in to the shared Reset button; non-user-tunable features stay clean by default.
		virtual bool HasResettableSettings() const { return false; }

		// Always-on overlay rendered on top of the game even when the settings menu is closed.
		virtual void DrawOverlay() {}

		// Fired once by FeatureManager::OnD3D11ReadyAll after the D3D11 device exists.
		virtual void OnD3D11Ready(IDXGIAdapter* /*adapter*/, ID3D11Device* /*device*/) {}

		// Discovery/menu defaults mirror Skyrim CS; features opt in by overriding.

		// One-line description shown in the menu under the feature name.
		virtual std::string GetFeatureSummary() const { return {}; }

		// External docs / mod-page link surfaced in the menu when present.
		virtual std::optional<std::string> GetFeatureModLink() const { return {}; }

		// Core features cannot be disabled by the user; the menu hides their enabled toggle.
		virtual bool IsCore() const { return false; }

		// Category label used for menu grouping (e.g. "Lighting", "Post-process", "Diagnostics").
		virtual std::string GetCategory() const { return "Misc"; }

		// Hide a feature from the menu entirely (e.g. developer-only tooling).
		virtual bool IsInMenu() const { return true; }

		// Drawn for features that registered but did not load; unwired today (no load-state tracking).
		virtual void DrawUnloadedUI() {}

		// Drawn when a feature failed to load, so the user sees why instead of a silent skip.
		virtual void DrawFailLoadMessage() {}

		// Preset defaults are opt-out; features set this true to join the global library.
		virtual bool ParticipatesInPresets() const { return false; }

		// Presets skip test-mode features so smoke marker overrides are not silently clobbered.
		virtual bool IsInTestMode() const { return false; }

		// Snake_case key for [features.<key>]; multi-word features should override for clarity.
		virtual std::string GetPresetKey() const;

		// Phase 1: parse into scratch only; do NOT mutate live state before the caller commits.
		virtual bool StageFromPreset(const toml::table& /*a_subtable*/,
									 const PresetApplyContext& /*a_ctx*/,
									 std::string& /*a_err*/) { return true; }

		// Phase 2a swaps scratch into live state only; no throw, no I/O, all staged features first.
		// Phase 2b persists TOML and rebuilds derived resources; failures are logged per feature.
		virtual void CommitStagedSwap() {}
		virtual void CommitStagedFinalize() {}
		void CommitStaged()
		{
			CommitStagedSwap();
			CommitStagedFinalize();
		}

		// Emit live state into [features.<key>]; leave empty to opt out of the current save.
		virtual void ExportToPreset(toml::table& /*a_subtable*/) {}

	private:
		friend class FeatureManager;
		void SetLoaded(bool a_loaded) noexcept { _loaded = a_loaded; }
		bool HasLoadFailed() const noexcept { return _loadFailed; }
		const std::string& LoadFailureReason() const noexcept { return _loadFailureReason; }

		bool        _loaded = false;
		bool        _loadFailed = false;
		std::string _loadFailureReason;
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

		void LoadAll();
		void OnDataLoadedAll();
		void OnPostPostLoadAll();

		// Fan out OnD3D11Ready to every loaded feature exactly once, on first D3D11 device
		// creation. Driven from the device-creation hook independently of Streamline init success.
		void OnD3D11ReadyAll(IDXGIAdapter* a_adapter, ID3D11Device* a_device);

		const std::vector<Feature*>& GetAll() const noexcept { return _loadedFeatures; }

	private:
		FeatureManager() = default;
		std::vector<Feature*> _features;
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
