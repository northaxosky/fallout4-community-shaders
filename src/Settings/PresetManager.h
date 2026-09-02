#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cs
{
	struct PresetMeta
	{
		std::string           name;
		std::string           identity;  // "B:" or "U:" plus lowercase name.
		std::filesystem::path path;
		bool                  builtin = false;
	};

	// Built-ins may ignore user weather overrides.
	struct PresetApplyContext
	{
		bool isBuiltin = false;
	};

	class PresetManager
	{
	public:
		static PresetManager& Get();

		// Deduplicate within each preset scope.
		void Refresh();

		const std::vector<PresetMeta>& List() const { return _entries; }

		// Identity is scope prefix plus lowercase name.
		const PresetMeta* FindByIdentity(std::string_view a_identity) const;

		// Marker payloads may use bare names.
		const PresetMeta* FindByName(std::string_view a_name, bool a_preferUser = true) const;

		// Failures preserve live state and prevent preset persistence.
		bool Apply(const PresetMeta& a_meta, std::string& a_err);

		// Recheck existence when overwrites are disabled.
		bool Save(const std::filesystem::path& a_path,
				  std::string_view             a_presetName,
				  std::string&                 a_err,
				  bool                         a_allowOverwrite = false);

		bool Delete(const PresetMeta& a_meta, std::string& a_err) const;

		// Smoke markers override active-preset auto-loading.
		void ResolveAndApplyBootPreset();

		std::string activeIdentity;        // Survives Refresh().
		std::string activeName;
		bool        autoLoadOnBoot = false;
		std::string pendingComboIdentity;  // Selection changes only after Load.
		std::string lastError;
		char        saveAsBuf[64] = {};

		// Preserve sibling config tables while saving presets.
		bool SaveCoreConfig();

	private:
		PresetManager() = default;

		void LoadCoreConfig();
		std::vector<PresetMeta> _entries;
	};

	// Names use 1-64 safe characters and cannot collide or equal "Builtin".
	bool ValidatePresetName(std::string_view               a_name,
							const std::vector<PresetMeta>& a_existing,
							std::string&                   a_err);

	std::string MakePresetIdentity(char a_scope, std::string_view a_name);
}
