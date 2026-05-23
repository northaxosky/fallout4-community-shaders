#pragma once

#include "Imagespace.h"
#include "WeatherProfiles.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cs::features::imagespace
{
	struct PresetMeta
	{
		std::string           name;      // display name (filename stem; preserves original case).
		std::string           identity;  // "B:" or "U:" + lowercase(name); stable case-insensitive key.
		std::filesystem::path path;
		bool                  builtin = false;
	};

	class PresetManager
	{
	public:
		// Scans Builtin/ then user Presets/ root. Missing dirs treated as empty. Dotfiles hidden.
		// Sort: builtin-first then alpha by name. Safe to call repeatedly.
		void Refresh();

		const std::vector<PresetMeta>& List() const { return _entries; }

		// Lowercases the input before lookup. Identity is "B:<lcname>" or "U:<lcname>".
		const PresetMeta* FindByIdentity(std::string_view a_identity) const;

		// Case-insensitive match on display name. Used for marker payloads that supply a bare name.
		// preferUser=true returns the user entry when a builtin and a user preset share a name.
		const PresetMeta* FindByName(std::string_view a_name, bool a_preferUser = true) const;

		// Parses preset TOML into outparams. For builtin presets, [weather.overrides] in the file
		// is dropped (with a warning log); user formID mappings are caller-owned and must not be
		// overwritten by a shipped preset. On parse failure: returns false, outparams unchanged,
		// a_err populated.
		bool Load(const PresetMeta&  a_meta,
				  Imagespace::Settings& a_outSettings,
				  WeatherProfiles&      a_outProfiles,
				  std::string&          a_err) const;

		// Writes settings + profiles (with overrides) and a [meta] block at the given path.
		// a_allowOverwrite=false (Save As default) refuses to write if the path already exists at
		// write-time (caller is expected to have validated the name; the re-check guards against
		// TOCTOU between validate and write). a_allowOverwrite=true is for the Save-active flow
		// that intentionally overwrites the live file.
		bool Save(const std::filesystem::path& a_path,
				  const Imagespace::Settings&  a_settings,
				  const WeatherProfiles&       a_profiles,
				  std::string_view             a_presetName,
				  std::string&                 a_err,
				  bool                         a_allowOverwrite = false) const;

		// Refuses to delete builtin presets.
		bool Delete(const PresetMeta& a_meta, std::string& a_err) const;

	private:
		std::vector<PresetMeta> _entries;
	};

	// Validates a candidate preset name for Save As.
	// Rules: [A-Za-z0-9_-]{1,64}; not a Windows reserved device name; not "Builtin";
	// no case-insensitive collision with an existing preset name (builtin OR user).
	bool ValidatePresetName(std::string_view               a_name,
							const std::vector<PresetMeta>& a_existing,
							std::string&                   a_err);

	// Reads a marker file as a single-line text payload. Caps file at 512 bytes; strips UTF-8 BOM,
	// ASCII whitespace, CRLF; takes first line only; rejects empty.
	// Returns false on missing/empty/oversize/error; true and populates a_outPayload otherwise.
	bool ReadTextMarker(const std::filesystem::path& a_path, std::string& a_outPayload);

	// Builds an identity string from a scope ('B' or 'U') and a display name.
	std::string MakePresetIdentity(char a_scope, std::string_view a_name);
}
