#include "LightAuthoring.h"

#include "Settings/FeatureConfig.h"

#include <algorithm>

namespace cs::features::inverse_square_lighting
{
	// FO4: occupied LIGH bits and spot fields require external per-form authoring.
	void AuthoredLight::Apply(const AuthoredLight& a_override)
	{
		if (a_override.inverseSquare)
			inverseSquare = a_override.inverseSquare;
		if (a_override.linear)
			linear = a_override.linear;
		if (a_override.cutoff)
			cutoff = a_override.cutoff;
		if (a_override.size)
			size = a_override.size;
	}

	namespace
	{
		template <class T, class Reader>
		bool ReadOptional(std::string_view a_key, std::optional<T>& a_value,
			Reader a_reader, std::string& a_error)
		{
			T value{};
			const auto status = a_reader(value);
			if (status == feature_config::ScalarReadStatus::kMissing)
				return true;
			if (status != feature_config::ScalarReadStatus::kValid) {
				a_error = "Invalid light field: " + std::string(a_key);
				return false;
			}
			a_value = value;
			return true;
		}
	}

	bool ParseLightDefinitions(const toml::table& a_table,
		std::vector<LightDefinition>& a_definitions, std::string& a_error)
	{
		using namespace feature_config;
		std::vector<LightDefinition> candidate;
		for (const auto& [key, node] : a_table) {
			const bool reference = key.str() == "references";
			if (!reference && key.str() != "lights") {
				a_error = "Unknown light document key: " + std::string(key.str());
				return false;
			}
			const auto* array = node.as_array();
			if (!array) {
				a_error = "Expected [[lights]] or [[references]]";
				return false;
			}
			for (const auto& element : *array) {
				const auto* entry = element.as_table();
				LightDefinition definition;
				definition.reference = reference;
				std::uint64_t id = 0;
				if (!entry ||
					ReadString(*entry, "plugin", definition.identity.plugin) != ScalarReadStatus::kValid ||
					definition.identity.plugin.empty() ||
					ReadUnsignedInteger(*entry, "form_id", id, 1, 0xFFFFFF) != ScalarReadStatus::kValid) {
					a_error = "Each light requires plugin and a nonzero plugin-local form_id";
					return false;
				}
				definition.identity.formID = static_cast<std::uint32_t>(id);
				for (const auto& [field, value] : *entry) {
					(void)value;
					if (field != "plugin" && field != "form_id" && field != "inverse_square" &&
						field != "linear" && field != "cutoff" && field != "size") {
						a_error = "Unknown light field: " + std::string(field.str());
						return false;
					}
				}
				auto& data = definition.data;
				if (!ReadOptional("inverse_square", data.inverseSquare, [&](bool& v) { return ReadBool(*entry, "inverse_square", v); }, a_error) || !ReadOptional("linear", data.linear, [&](bool& v) { return ReadBool(*entry, "linear", v); }, a_error) || !ReadOptional("cutoff", data.cutoff, [&](float& v) { return ReadFloat(*entry, "cutoff", v); }, a_error) || !ReadOptional("size", data.size, [&](float& v) { return ReadFloat(*entry, "size", v); }, a_error))
					return false;
				candidate.push_back(std::move(definition));
			}
		}
		a_definitions = std::move(candidate);
		return true;
	}

	bool LoadLightDefinitions(const std::filesystem::path& a_directory,
		std::vector<LightDefinition>& a_definitions, std::string& a_error)
	{
		std::error_code ec;
		const bool exists = std::filesystem::exists(a_directory, ec);
		if (ec) {
			a_error = ec.message();
			return false;
		}
		if (!exists) {
			a_definitions.clear();
			return true;
		}
		std::vector<std::filesystem::path> files;
		for (const auto& file : std::filesystem::directory_iterator(a_directory, ec))
			if (file.path().extension() == ".toml")
				files.push_back(file.path());
		if (ec) {
			a_error = ec.message();
			return false;
		}
		std::ranges::sort(files);
		std::vector<LightDefinition> candidate;
		for (const auto& path : files) {
			const auto document = feature_config::LoadFile(path);
			std::vector<LightDefinition> entries;
			if (document.status != feature_config::FileLoadStatus::kParsed ||
				!ParseLightDefinitions(document.table, entries, a_error)) {
				a_error = path.string() + ": " +
				          (document.error.empty() ? a_error : document.error);
				return false;
			}
			candidate.insert(candidate.end(), entries.begin(), entries.end());
		}
		a_definitions = std::move(candidate);
		return true;
	}
}
