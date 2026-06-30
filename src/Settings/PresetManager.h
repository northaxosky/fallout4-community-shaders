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

	// Stage context for builtin-vs-user rules, e.g. shipped presets dropping Imagespace weather overrides.
	struct PresetApplyContext
	{
		bool isBuiltin = false;
	};

	// Cross-feature preset library: scans preset dirs and applies Stage/Commit across participants.
	class PresetManager
	{
	public:
		static PresetManager& Get();

		// Scans Builtin/ then user presets; de-dupes by identity and warns on cross-scope shadowing.
		void Refresh();

		const std::vector<PresetMeta>& List() const { return _entries; }

		// Lowercases the input before lookup. Identity is "B:<lcname>" or "U:<lcname>".
		const PresetMeta* FindByIdentity(std::string_view a_identity) const;

		// Case-insensitive match on display name. Used for marker payloads that supply a bare name.
		const PresetMeta* FindByName(std::string_view a_name, bool a_preferUser = true) const;

		// Two-phase apply: stage all feature scratch, then no-throw swap all live state before finalize.
		// Finalize failures leave live state consistent but skip [preset] persistence for next-boot safety.
		bool Apply(const PresetMeta& a_meta, std::string& a_err);

		// Saves live participant state; a_allowOverwrite=false re-checks existence to narrow TOCTOU.
		bool Save(const std::filesystem::path& a_path,
				  std::string_view             a_presetName,
				  std::string&                 a_err,
				  bool                         a_allowOverwrite = false);

		bool Delete(const PresetMeta& a_meta, std::string& a_err) const;

		// After feature init, honors .cs_force_preset first, otherwise auto-loads the active preset.
		void ResolveAndApplyBootPreset();

		// Preset UI state; activeIdentity/name persist to [preset] after successful applies.
		std::string activeIdentity;        // lowercase identity; survives Refresh.
		std::string activeName;            // display name (preserves case); informational.
		bool        autoLoadOnBoot = false;
		std::string pendingComboIdentity;  // ImGui combo binding; may differ from active until Load.
		std::string lastError;             // rendered red below controls when non-empty.
		char        saveAsBuf[64] = {};

		// Persists only the [preset] subtable so Feature.cpp-owned siblings survive.
		bool SaveCoreConfig();

	private:
		PresetManager() = default;

		void LoadCoreConfig();
		std::vector<PresetMeta> _entries;
	};

	// Save As name rules: [A-Za-z0-9_-]{1,64}, not reserved/Builtin, no existing name collision.
	bool ValidatePresetName(std::string_view               a_name,
							const std::vector<PresetMeta>& a_existing,
							std::string&                   a_err);

	// Reads a one-shot marker payload: max 512 bytes, strips UTF-8 BOM/whitespace, removes on success.
	bool ReadTextMarker(const std::filesystem::path& a_path, std::string& a_outPayload);

	// Builds an identity string from a scope ('B' or 'U') and a display name.
	std::string MakePresetIdentity(char a_scope, std::string_view a_name);
}
