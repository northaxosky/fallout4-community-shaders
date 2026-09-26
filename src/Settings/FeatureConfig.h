#pragma once

#include "Render/ShaderInjectionTargets.h"
#include "Settings/SettingsMetadata.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

namespace cs::feature_config
{
	inline constexpr char kConfigPath[] =
		"Data\\F4SE\\Plugins\\FO4CommunityShaders\\FO4CommunityShaders.toml";

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

	struct RefreshResult
	{
		toml::table root;
		FileLoadStatus status{ FileLoadStatus::kMissing };
		std::string error;
	};

	std::string RenderDocument(const settings::Registry& a_registry, const toml::table& a_root);
	RefreshResult Initialize(settings::Registry a_registry);
	RefreshResult InitializeAt(const std::filesystem::path& a_path, settings::Registry a_registry);
	toml::table GetRoot();
	std::optional<toml::table> GetFeature(std::string_view a_key);

	struct ShaderOwnershipTargets
	{
		std::array<bool,
			static_cast<std::size_t>(engine::ShaderInjectionTarget::kCount)>
			enabled{};

		bool& operator[](engine::ShaderInjectionTarget a_target) noexcept
		{
			return enabled[static_cast<std::size_t>(a_target)];
		}

		bool operator[](engine::ShaderInjectionTarget a_target) const noexcept
		{
			return enabled[static_cast<std::size_t>(a_target)];
		}
	};

	struct ShaderOwnershipConfig
	{
		bool enabled{ false };
		ShaderOwnershipTargets targets;
	};

	struct ShaderOwnershipParseResult
	{
		ShaderOwnershipConfig config;
		bool valid{ true };
		std::string error;
	};

	ShaderOwnershipParseResult ParseShaderOwnership(const toml::table& a_root);

	struct WriteResult
	{
		bool success{ false };
		std::string error;

		explicit operator bool() const noexcept { return success; }
	};

	WriteResult UpdateOwnedSettingsAt(
		const std::filesystem::path& a_filePath,
		std::span<const std::string_view> a_path,
		const toml::table& a_value);
	WriteResult UpdateFeatureOwnedSettings(
		std::string_view a_featureKey,
		const toml::table& a_delta);
	WriteResult UpdateFeatureLoad(std::string_view a_featureKey, bool a_load);
	WriteResult UpdateTopLevelSection(std::string_view a_section, const toml::table& a_value);

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
