#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cs
{
	struct PresetMeta
	{
		std::string           name;      // display name (filename stem; preserves original case)
		std::string           identity;  // "B:" or "U:" + lowercase(name); stable case-insensitive key
		std::filesystem::path path;
		bool                  builtin = false;
	};

	// Context passed to Feature::StageFromPreset so features can adjust parsing without each one
	// needing private knowledge of "builtin vs user" rules. Currently only carries the builtin flag
	// (used by Imagespace to drop [weather.overrides] from shipped presets).
	struct PresetApplyContext
	{
		bool isBuiltin = false;
	};

	// Global, cross-feature preset library. Owns Presets/Builtin/ and Presets/ scans, the
	// FO4CommunityShaders.toml [preset] block, and the two-phase apply (Stage / Commit) across
	// every participating feature.
	class PresetManager
	{
	public:
		static PresetManager& Get();

		// Scans Builtin/ then user Presets/ root. Idempotent. Sort: builtin first, alpha by name.
		// De-dupes by identity (builtin wins on collision with a warning); also warns when a
		// builtin and a user preset share a name (cross-scope shadowing).
		void Refresh();

		const std::vector<PresetMeta>& List() const { return _entries; }

		// Lowercases the input before lookup. Identity is "B:<lcname>" or "U:<lcname>".
		const PresetMeta* FindByIdentity(std::string_view a_identity) const;

		// Case-insensitive match on display name. Used for marker payloads that supply a bare name.
		const PresetMeta* FindByName(std::string_view a_name, bool a_preferUser = true) const;

		// Two-phase apply across every [features.<key>] subtable in the preset file:
		//   1) Stage: each matching participating, non-test-mode feature parses into a scratch.
		//      Any hard parse error aborts before any feature commits.
		//   2) Commit: every staged feature swaps scratch into live state and re-saves its own TOML.
		// On success, the [preset] block is updated and persisted. On any error, live state is
		// untouched and the [preset] block is not modified.
		bool Apply(const PresetMeta& a_meta, std::string& a_err);

		// Captures current live state from every participating, non-test-mode feature into
		// [features.<key>] subtables, plus a [meta] block. a_allowOverwrite=false refuses to write
		// if the path already exists (Save As semantics, with a re-check immediately before write
		// to close the validate-then-write TOCTOU window).
		bool Save(const std::filesystem::path& a_path,
				  std::string_view             a_presetName,
				  std::string&                 a_err,
				  bool                         a_allowOverwrite = false);

		bool Delete(const PresetMeta& a_meta, std::string& a_err) const;

		// Reads FO4CommunityShaders.toml [preset] block (active + auto_load_on_boot), refreshes
		// the entry list, honours the .cs_force_preset smoke marker if present, otherwise applies
		// the active preset when auto_load_on_boot is true. Called once at tail of
		// FeatureManager::OnPostPostLoadAll() after every feature has parsed baseline settings and
		// installed its hooks.
		void ResolveAndApplyBootPreset();

		// UI scratch state (rendered by Menu::DrawDefaultUI in the top-level Presets header).
		// activeIdentity and activeName persist into the [preset] block on every successful apply.
		std::string activeIdentity;        // lowercase identity; survives Refresh.
		std::string activeName;            // display name (preserves case); informational.
		bool        autoLoadOnBoot = false;
		std::string pendingComboIdentity;  // ImGui combo binding; may differ from active until Load.
		std::string lastError;             // rendered red below controls when non-empty.
		char        saveAsBuf[64] = {};

		// Persists [preset] block of FO4CommunityShaders.toml. Only mutates the [preset] subtable
		// so sibling blocks ([info], [features]) owned by Feature.cpp are preserved.
		bool SaveCoreConfig();

	private:
		PresetManager() = default;

		void LoadCoreConfig();
		std::vector<PresetMeta> _entries;
	};

	// Validates a candidate preset name for Save As.
	// Rules: [A-Za-z0-9_-]{1,64}; not a Windows reserved device name; not "Builtin";
	// no case-insensitive collision with an existing preset name (builtin OR user).
	bool ValidatePresetName(std::string_view               a_name,
							const std::vector<PresetMeta>& a_existing,
							std::string&                   a_err);

	// Reads a marker file as a single-line text payload. Caps at 512 bytes; strips UTF-8 BOM,
	// ASCII whitespace, CRLF; takes first line only; rejects empty.
	bool ReadTextMarker(const std::filesystem::path& a_path, std::string& a_outPayload);

	// Builds an identity string from a scope ('B' or 'U') and a display name.
	std::string MakePresetIdentity(char a_scope, std::string_view a_name);
}
