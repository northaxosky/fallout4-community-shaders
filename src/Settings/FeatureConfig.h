#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

namespace cs::feature_config
{
	enum class FileLoadStatus
	{
		kMissing,
		kParsed,
		kParseError,
		kIoError
	};

	struct FileLoadResult
	{
		FileLoadStatus status{ FileLoadStatus::kMissing };
		toml::table table;
		std::string error;
	};

	FileLoadResult LoadFile(const std::filesystem::path& a_path);

	enum class ScalarReadStatus
	{
		kMissing,
		kValid,
		kWrongType,
		kInvalidValue,
		kOutOfRange
	};

	ScalarReadStatus ReadBool(const toml::table& a_table, std::string_view a_key, bool& a_value);
	ScalarReadStatus ReadSignedInteger(
		const toml::table& a_table,
		std::string_view a_key,
		std::int64_t& a_value,
		std::int64_t a_min = std::numeric_limits<std::int64_t>::min(),
		std::int64_t a_max = std::numeric_limits<std::int64_t>::max());
	ScalarReadStatus ReadUnsignedInteger(
		const toml::table& a_table,
		std::string_view a_key,
		std::uint64_t& a_value,
		std::uint64_t a_min = std::numeric_limits<std::uint64_t>::min(),
		std::uint64_t a_max = std::numeric_limits<std::uint64_t>::max());
	ScalarReadStatus ReadFloat(
		const toml::table& a_table,
		std::string_view a_key,
		float& a_value,
		float a_min = std::numeric_limits<float>::lowest(),
		float a_max = std::numeric_limits<float>::max());
	ScalarReadStatus ReadFloat(
		const toml::node& a_node,
		float& a_value,
		float a_min = std::numeric_limits<float>::lowest(),
		float a_max = std::numeric_limits<float>::max());
	ScalarReadStatus ReadDouble(
		const toml::table& a_table,
		std::string_view a_key,
		double& a_value,
		double a_min = std::numeric_limits<double>::lowest(),
		double a_max = std::numeric_limits<double>::max());
	ScalarReadStatus ReadString(const toml::table& a_table, std::string_view a_key, std::string& a_value);

	struct ActivationResult
	{
		bool load{ false };
		bool valid{ true };
	};

	ActivationResult ParseActivation(const toml::table& a_table);
}
