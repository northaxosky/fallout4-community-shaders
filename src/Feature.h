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

		virtual void DrawSettings() {}

		// Reset this feature's persisted settings to in-code defaults. Override + opt-in via
		// HasResettableSettings() == true to have the menu render the shared Reset button.
		// Implementations should also clear derived caches and run SaveSettings().
		virtual void RestoreDefaultSettings() {}

		// Opt-in to the shared menu Reset button. Default false so features that don't carry
		// user-tunable settings (e.g. ShaderCatalog, ShaderReplacement diagnostics) stay clean.
		virtual bool HasResettableSettings() const { return false; }

		// Always-on overlay rendered on top of the game even when the settings menu is closed.
		virtual void DrawOverlay() {}

		// Fired by cs::Streamline once the D3D11 device exists and the SDK is initialized.
		virtual void OnD3D11Ready(IDXGIAdapter* /*adapter*/, ID3D11Device* /*device*/) {}

		// ---- Discovery / menu surface (upstream parity) ------------------------------------
		// Mirrors Skyrim CS Feature.h; no-op defaults so features opt in by overriding.

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

		// ---- Cross-feature preset system (Phase 4) -----------------------------------------
		// Default = opt-out. Features set this true to participate in the global preset library.
		virtual bool ParticipatesInPresets() const { return false; }

		// When true, PresetManager skips this feature during both Stage and Commit. Features should
		// return true while a smoke marker is forcing values, so a preset apply mid-test doesn't
		// silently un-override them.
		virtual bool IsInTestMode() const { return false; }

		// Snake_case key used inside [features.<key>] in preset TOMLs. Default = lowercased GetName()
		// with any character outside [a-z0-9_] replaced by '_'. Multi-word features should override
		// to add explicit underscores (e.g. "screen_space_shadows").
		virtual std::string GetPresetKey() const;

		// Phase 1 of preset apply. Parse a_subtable into feature-owned scratch state. Do NOT mutate
		// live settings here. Return true on success; on failure populate a_err and leave scratch
		// in any state (caller will abort the whole apply).
		virtual bool StageFromPreset(const toml::table& /*a_subtable*/,
									 const PresetApplyContext& /*a_ctx*/,
									 std::string& /*a_err*/) { return true; }

		// Phase 2 of preset apply. Two sub-phases so PresetManager can keep live state and disk
		// consistent across a multi-feature apply:
		//   2a) CommitStagedSwap   - swap scratch into live state ONLY. Must not throw and must
		//       not touch disk. Caller invokes this on every staged feature first.
		//   2b) CommitStagedFinalize - persist to this feature's per-feature TOML and run derived
		//       resource updates (LUT loads, shader rebuilds, dirty flags). May throw; caller logs
		//       and continues so a single feature's I/O failure doesn't leave the rest unsaved.
		// Default CommitStaged() runs both back-to-back for the in-place edit case (slider commit,
		// per-feature DrawSettings) where the cross-feature ordering isn't relevant.
		virtual void CommitStagedSwap() {}
		virtual void CommitStagedFinalize() {}
		void CommitStaged()
		{
			CommitStagedSwap();
			CommitStagedFinalize();
		}

		// Emit current live state into a_subtable for inclusion as [features.<key>] in a saved
		// preset. Implementations may leave a_subtable empty to opt out of the current save.
		virtual void ExportToPreset(toml::table& /*a_subtable*/) {}

	private:
		friend class FeatureManager;
		void SetLoaded(bool a_loaded) noexcept { _loaded = a_loaded; }

		bool _loaded = false;
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

		const std::vector<Feature*>& GetAll() const noexcept { return _loadedFeatures; }

	private:
		FeatureManager() = default;
		std::vector<Feature*> _features;
		std::vector<Feature*> _loadedFeatures;
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
