#include "Settings/FeatureConfig.h"

#include <array>
#include <cmath>
#include <fstream>
#include <system_error>

namespace cs::feature_config
{
	namespace
	{
		std::string PathText(const std::filesystem::path& a_path)
		{
			return a_path.string();
		}

		FileLoadResult IoError(const std::filesystem::path& a_path, std::string_view a_detail)
		{
			return {
				FileLoadStatus::kIoError,
				{},
				"Failed to read configuration file '" + PathText(a_path) + "': " + std::string(a_detail)
			};
		}

		ActivationResult FromLegacy(std::optional<bool> a_legacyIntent)
		{
			if (!a_legacyIntent) {
				return {};
			}

			return {
				.enabled = *a_legacyIntent,
				.valid = true,
				.migrationNeeded = true,
				.source = ActivationIntentSource::kLegacy
			};
		}
	}

	FileLoadResult LoadFile(const std::filesystem::path& a_path)
	{
		std::error_code ec;
		const bool exists = std::filesystem::exists(a_path, ec);
		if (ec) {
			return IoError(a_path, ec.message());
		}
		if (!exists) {
			return {
				FileLoadStatus::kMissing,
				{},
				"Configuration file does not exist: '" + PathText(a_path) + "'"
			};
		}

		const bool regularFile = std::filesystem::is_regular_file(a_path, ec);
		if (ec) {
			return IoError(a_path, ec.message());
		}
		if (!regularFile) {
			return IoError(a_path, "path is not a regular file");
		}

		std::ifstream input(a_path, std::ios::binary);
		if (!input.is_open()) {
			return IoError(a_path, "unable to open file");
		}

		std::string contents;
		std::array<char, 4096> buffer{};
		while (true) {
			input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const auto count = input.gcount();
			if (count > 0) {
				contents.append(buffer.data(), static_cast<std::size_t>(count));
			}
			if (input.bad()) {
				return IoError(a_path, "stream read failed");
			}
			if (input.eof()) {
				break;
			}
			if (input.fail()) {
				return IoError(a_path, "stream read failed before end of file");
			}
		}

		try {
			return {
				FileLoadStatus::kParsed,
				toml::parse(contents, PathText(a_path)),
				{}
			};
		} catch (const toml::parse_error& e) {
			return {
				FileLoadStatus::kParseError,
				{},
				"Failed to parse configuration file '" + PathText(a_path) + "': " + std::string(e.description())
			};
		}
	}

	ScalarReadStatus ReadBool(const toml::table& a_table, std::string_view a_key, bool& a_value)
	{
		const auto* node = a_table.get(a_key);
		if (!node) {
			return ScalarReadStatus::kMissing;
		}
		if (!node->is_boolean()) {
			return ScalarReadStatus::kWrongType;
		}

		a_value = node->as_boolean()->get();
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadSignedInteger(
		const toml::table& a_table,
		std::string_view a_key,
		std::int64_t& a_value,
		std::int64_t a_min,
		std::int64_t a_max)
	{
		const auto* node = a_table.get(a_key);
		if (!node) {
			return ScalarReadStatus::kMissing;
		}
		if (!node->is_integer()) {
			return ScalarReadStatus::kWrongType;
		}

		const auto value = node->as_integer()->get();
		if (value < a_min || value > a_max) {
			return ScalarReadStatus::kOutOfRange;
		}

		a_value = value;
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadUnsignedInteger(
		const toml::table& a_table,
		std::string_view a_key,
		std::uint64_t& a_value,
		std::uint64_t a_min,
		std::uint64_t a_max)
	{
		const auto* node = a_table.get(a_key);
		if (!node) {
			return ScalarReadStatus::kMissing;
		}
		if (!node->is_integer()) {
			return ScalarReadStatus::kWrongType;
		}

		const auto value = node->as_integer()->get();
		if (value < 0) {
			return ScalarReadStatus::kOutOfRange;
		}

		const auto unsignedValue = static_cast<std::uint64_t>(value);
		if (unsignedValue < a_min || unsignedValue > a_max) {
			return ScalarReadStatus::kOutOfRange;
		}

		a_value = unsignedValue;
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadFloat(
		const toml::table& a_table,
		std::string_view a_key,
		float& a_value,
		float a_min,
		float a_max)
	{
		const auto* node = a_table.get(a_key);
		if (!node) {
			return ScalarReadStatus::kMissing;
		}
		return ReadFloat(*node, a_value, a_min, a_max);
	}

	ScalarReadStatus ReadFloat(const toml::node& a_node, float& a_value, float a_min, float a_max)
	{
		if (!a_node.is_floating_point() && !a_node.is_integer()) {
			return ScalarReadStatus::kWrongType;
		}

		const double value = a_node.is_floating_point() ?
			a_node.as_floating_point()->get() :
			static_cast<double>(a_node.as_integer()->get());
		if (!std::isfinite(value)) {
			return ScalarReadStatus::kInvalidValue;
		}

		const auto floatValue = static_cast<float>(value);
		if (!std::isfinite(floatValue)) {
			return ScalarReadStatus::kOutOfRange;
		}
		if (floatValue < a_min || floatValue > a_max) {
			return ScalarReadStatus::kOutOfRange;
		}

		a_value = floatValue;
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadDouble(
		const toml::table& a_table,
		std::string_view a_key,
		double& a_value,
		double a_min,
		double a_max)
	{
		const auto* node = a_table.get(a_key);
		if (!node) {
			return ScalarReadStatus::kMissing;
		}
		if (!node->is_floating_point() && !node->is_integer()) {
			return ScalarReadStatus::kWrongType;
		}

		const double value = node->is_floating_point() ?
			node->as_floating_point()->get() :
			static_cast<double>(node->as_integer()->get());
		if (!std::isfinite(value)) {
			return ScalarReadStatus::kInvalidValue;
		}
		if (value < a_min || value > a_max) {
			return ScalarReadStatus::kOutOfRange;
		}

		a_value = value;
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadString(const toml::table& a_table, std::string_view a_key, std::string& a_value)
	{
		const auto* node = a_table.get(a_key);
		if (!node) {
			return ScalarReadStatus::kMissing;
		}
		if (!node->is_string()) {
			return ScalarReadStatus::kWrongType;
		}

		a_value = node->as_string()->get();
		return ScalarReadStatus::kValid;
	}

	ActivationResult ParseActivation(const toml::table& a_table, std::optional<bool> a_legacyIntent)
	{
		const auto* featureNode = a_table.get("feature");
		if (!featureNode) {
			return FromLegacy(a_legacyIntent);
		}
		if (!featureNode->is_table()) {
			return {
				.enabled = false,
				.valid = false,
				.migrationNeeded = false,
				.source = ActivationIntentSource::kCanonical
			};
		}

		const auto* enabledNode = featureNode->as_table()->get("enabled");
		if (!enabledNode) {
			return FromLegacy(a_legacyIntent);
		}
		if (!enabledNode->is_boolean()) {
			return {
				.enabled = false,
				.valid = false,
				.migrationNeeded = false,
				.source = ActivationIntentSource::kCanonical
			};
		}

		return {
			.enabled = enabledNode->as_boolean()->get(),
			.valid = true,
			.migrationNeeded = false,
			.source = ActivationIntentSource::kCanonical
		};
	}

	MigrationStatus SetActivation(toml::table& a_table, bool a_enabled)
	{
		auto* featureNode = a_table.get("feature");
		if (featureNode && !featureNode->is_table()) {
			return MigrationStatus::kFeatureNodeNotTable;
		}
		if (!featureNode) {
			a_table.insert("feature", toml::table{});
			featureNode = a_table.get("feature");
		}

		auto& feature = *featureNode->as_table();
		const auto* enabledNode = feature.get("enabled");
		if (enabledNode && enabledNode->is_boolean() && enabledNode->as_boolean()->get() == a_enabled) {
			return MigrationStatus::kUnchanged;
		}

		feature.insert_or_assign("enabled", a_enabled);
		return MigrationStatus::kApplied;
	}
}
