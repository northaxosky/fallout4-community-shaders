#include "Settings/FeatureConfig.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <mutex>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace cs::feature_config
{
	namespace
	{
		std::mutex& ConfigMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		toml::table& CachedRoot()
		{
			static toml::table root;
			return root;
		}

		bool& CachedDefaultLoaded()
		{
			static bool loaded = false;
			return loaded;
		}

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

		WriteResult WriteError(const std::filesystem::path& a_path, std::string_view a_detail)
		{
			return {
				.success = false,
				.error = "Failed to write configuration file '" + PathText(a_path) + "': " + std::string(a_detail)
			};
		}

		WriteResult ProductionWriteUnavailable()
		{
			std::scoped_lock lock(ConfigMutex());
			if (CachedDefaultLoaded()) {
				return { .success = true };
			}
			return WriteError(kUserConfigPath, "default configuration is unavailable");
		}

		WriteResult AtomicWrite(const std::filesystem::path& a_path, const toml::table& a_table)
		{
			std::error_code ec;
			const auto parent = a_path.parent_path();
			if (!parent.empty()) {
				std::filesystem::create_directories(parent, ec);
				if (ec) {
					return WriteError(a_path, ec.message());
				}
			}

			static std::atomic_uint64_t sequence{ 0 };
			auto temporary = a_path;
			temporary += ".tmp." + std::to_string(GetCurrentProcessId()) + "." +
				std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));

			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output.is_open()) {
					return WriteError(a_path, "unable to open temporary file");
				}
				output << a_table;
				output.flush();
				if (!output.good()) {
					output.close();
					std::filesystem::remove(temporary, ec);
					return WriteError(a_path, "temporary file write failed");
				}
				output.close();
				if (output.fail()) {
					std::filesystem::remove(temporary, ec);
					return WriteError(a_path, "temporary file close failed");
				}
			}

			if (!MoveFileExW(
					temporary.c_str(),
					a_path.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				const auto moveError = std::error_code(
					static_cast<int>(GetLastError()), std::system_category());
				std::filesystem::remove(temporary, ec);
				return WriteError(a_path, moveError.message());
			}

			return { .success = true };
		}

		toml::table* EnsureTablePath(
			toml::table& a_root,
			std::span<const std::string_view> a_path,
			std::string& a_error)
		{
			auto* current = &a_root;
			for (const auto key : a_path) {
				auto* existing = current->get(key);
				if (existing && !existing->is_table()) {
					a_error = "Path component '" + std::string(key) + "' is not a table";
					return nullptr;
				}
				if (!existing) {
					current->insert_or_assign(key, toml::table{});
					existing = current->get(key);
				}
				current = existing->as_table();
			}
			return current;
		}

		template <class Mutator>
		WriteResult UpdateUserFile(const std::filesystem::path& a_path, Mutator&& a_mutator)
		{
			std::scoped_lock lock(ConfigMutex());

			toml::table user;
			auto load = LoadFile(a_path);
			switch (load.status) {
			case FileLoadStatus::kMissing:
				break;
			case FileLoadStatus::kParsed:
				user = std::move(load.table);
				break;
			case FileLoadStatus::kParseError:
			case FileLoadStatus::kIoError:
				return WriteError(a_path, load.error);
			}

			std::string error;
			if (!a_mutator(user, error)) {
				return WriteError(a_path, error);
			}
			return AtomicWrite(a_path, user);
		}

		bool ReadRequiredOwnershipBool(
			const toml::table& a_table,
			std::string_view a_key,
			std::string_view a_path,
			bool& a_value,
			std::string& a_error)
		{
			const auto status = ReadBool(a_table, a_key, a_value);
			if (status == ScalarReadStatus::kValid)
				return true;

			a_error = std::string(a_path) + "." + std::string(a_key)
				+ (status == ScalarReadStatus::kMissing
						? " is required"
						: " must be a boolean");
			return false;
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

	void DeepMerge(toml::table& a_base, const toml::table& a_override)
	{
		for (const auto& [key, overrideNode] : a_override) {
			auto* baseNode = a_base.get(key);
			if (baseNode && baseNode->is_table() && overrideNode.is_table()) {
				DeepMerge(*baseNode->as_table(), *overrideNode.as_table());
			} else {
				a_base.insert_or_assign(key, overrideNode);
			}
		}
	}

	UnifiedLoadResult LoadMergedFiles(
		const std::filesystem::path& a_defaultPath,
		const std::filesystem::path& a_userPath)
	{
		UnifiedLoadResult result;
		auto defaultLoad = LoadFile(a_defaultPath);
		if (defaultLoad.status != FileLoadStatus::kParsed) {
			result.defaultError = std::move(defaultLoad.error);
			return result;
		}

		result.root = std::move(defaultLoad.table);
		result.defaultLoaded = true;

		auto userLoad = LoadFile(a_userPath);
		switch (userLoad.status) {
		case FileLoadStatus::kMissing:
			break;
		case FileLoadStatus::kParsed:
			DeepMerge(result.root, userLoad.table);
			result.userLoaded = true;
			break;
		case FileLoadStatus::kParseError:
		case FileLoadStatus::kIoError:
			result.userWarning = std::move(userLoad.error);
			break;
		}
		return result;
	}

	UnifiedLoadResult Reload()
	{
		auto result = LoadMergedFiles(kDefaultConfigPath, kUserConfigPath);
		{
			std::scoped_lock lock(ConfigMutex());
			CachedRoot() = result.root;
			CachedDefaultLoaded() = result.defaultLoaded;
		}
		return result;
	}

	toml::table GetMergedRoot()
	{
		std::scoped_lock lock(ConfigMutex());
		return CachedRoot();
	}

	std::optional<toml::table> GetFeature(std::string_view a_key)
	{
		std::scoped_lock lock(ConfigMutex());
		const auto* features = CachedRoot()["features"].as_table();
		if (!features) {
			return std::nullopt;
		}
		const auto* feature = features->get(a_key);
		if (!feature || !feature->is_table()) {
			return std::nullopt;
		}
		return *feature->as_table();
	}

	ShaderOwnershipParseResult ParseShaderOwnership(const toml::table& a_root)
	{
		ShaderOwnershipParseResult result;
		const auto* ownershipNode = a_root.get("shader_ownership");
		if (!ownershipNode)
			return result;

		result.present = true;
		const auto* ownership = ownershipNode->as_table();
		if (!ownership) {
			result.valid = false;
			result.error = "shader_ownership must be a table";
			return result;
		}

		if (!ReadRequiredOwnershipBool(
				*ownership,
				"enabled",
				"shader_ownership",
				result.config.enabled,
				result.error)) {
			result.valid = false;
			result.config = {};
			return result;
		}

		const auto* targetsNode = ownership->get("targets");
		const auto* targets = targetsNode ? targetsNode->as_table() : nullptr;
		if (!targets) {
			result.valid = false;
			result.config = {};
			result.error = targetsNode
				? "shader_ownership.targets must be a table"
				: "shader_ownership.targets is required";
			return result;
		}

		constexpr std::array<std::string_view, 6> targetKeys{
			"deferred_prepass",
			"bssky",
			"bswater",
			"bslighting",
			"bsdf_light",
			"bsdf_composite"
		};
		for (const auto& [key, node] : *targets) {
			(void)node;
			if (std::ranges::find(targetKeys, key.str()) == targetKeys.end()) {
				result.valid = false;
				result.config = {};
				result.error = "shader_ownership.targets contains unknown target '"
					+ std::string(key.str()) + "'";
				return result;
			}
		}

		const auto readTarget = [&](std::string_view a_key, bool& a_value) {
			return ReadRequiredOwnershipBool(
				*targets,
				a_key,
				"shader_ownership.targets",
				a_value,
				result.error);
		};
		if (!readTarget("deferred_prepass", result.config.targets.deferredPrepass)
			|| !readTarget("bssky", result.config.targets.bsSky)
			|| !readTarget("bswater", result.config.targets.bsWater)
			|| !readTarget("bslighting", result.config.targets.bsLighting)
			|| !readTarget("bsdf_light", result.config.targets.bsdfLight)
			|| !readTarget("bsdf_composite", result.config.targets.bsdfComposite)) {
			result.valid = false;
			result.config = {};
		}
		return result;
	}

	WriteResult UpdateUserTableAt(
		const std::filesystem::path& a_userPath,
		std::span<const std::string_view> a_path,
		const toml::table& a_value)
	{
		if (a_path.empty()) {
			return WriteError(a_userPath, "update path is empty");
		}

		return UpdateUserFile(a_userPath, [&](toml::table& a_user, std::string& a_error) {
			const auto parentPath = a_path.first(a_path.size() - 1);
			auto* parent = EnsureTablePath(a_user, parentPath, a_error);
			if (!parent) {
				return false;
			}
			parent->insert_or_assign(a_path.back(), a_value);
			return true;
		});
	}

	WriteResult UpdateFeatureSettings(std::string_view a_featureKey, const toml::table& a_settings)
	{
		if (const auto available = ProductionWriteUnavailable(); !available) {
			return available;
		}
		const std::array path{ std::string_view("features"), a_featureKey, std::string_view("settings") };
		return UpdateUserTableAt(kUserConfigPath, path, a_settings);
	}

	WriteResult UpdateFeature(std::string_view a_featureKey, const toml::table& a_feature)
	{
		if (const auto available = ProductionWriteUnavailable(); !available) {
			return available;
		}
		const std::array path{ std::string_view("features"), a_featureKey };
		return UpdateUserFile(kUserConfigPath, [&](toml::table& a_user, std::string& a_error) {
			auto* feature = EnsureTablePath(a_user, path, a_error);
			if (!feature) {
				return false;
			}
			for (const auto& [key, node] : a_feature) {
				feature->insert_or_assign(key, node);
			}
			return true;
		});
	}

	WriteResult UpdateFeatureLoad(std::string_view a_featureKey, bool a_load)
	{
		if (const auto available = ProductionWriteUnavailable(); !available) {
			return available;
		}
		const std::array path{ std::string_view("features"), a_featureKey };
		return UpdateUserFile(kUserConfigPath, [&](toml::table& a_user, std::string& a_error) {
			auto* feature = EnsureTablePath(a_user, path, a_error);
			if (!feature) {
				return false;
			}
			feature->insert_or_assign("load", a_load);
			return true;
		});
	}

	WriteResult UpdateTopLevelSection(std::string_view a_section, const toml::table& a_value)
	{
		if (const auto available = ProductionWriteUnavailable(); !available) {
			return available;
		}
		const std::array path{ a_section };
		return UpdateUserTableAt(kUserConfigPath, path, a_value);
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

	ActivationResult ParseActivation(const toml::table& a_table)
	{
		const auto* loadNode = a_table.get("load");
		if (!loadNode) {
			return {};
		}
		if (!loadNode->is_boolean()) {
			return {
				.load = false,
				.valid = false,
				.present = true
			};
		}

		return {
			.load = loadNode->as_boolean()->get(),
			.valid = true,
			.present = true
		};
	}
}
