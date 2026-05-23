#pragma once

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

		// Always-on overlay rendered on top of the game even when the settings menu is closed.
		virtual void DrawOverlay() {}

		// Fired by cs::Streamline once the D3D11 device exists and the SDK is initialized.
		virtual void OnD3D11Ready(IDXGIAdapter* /*adapter*/, ID3D11Device* /*device*/) {}

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

		// Phase 2 of preset apply. Atomically swap scratch into live state, re-run any derived
		// resource updates (LUT loads, shader rebuilds), and persist to this feature's per-feature
		// TOML. Only called if every staged feature succeeded in StageFromPreset.
		virtual void CommitStaged() {}

		// Emit current live state into a_subtable for inclusion as [features.<key>] in a saved
		// preset. Implementations may leave a_subtable empty to opt out of the current save.
		virtual void ExportToPreset(toml::table& /*a_subtable*/) {}

	private:
		friend class FeatureManager;
		void SetLoaded(bool a_loaded) noexcept { _loaded = a_loaded; }

		bool _loaded = false;
	};

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
