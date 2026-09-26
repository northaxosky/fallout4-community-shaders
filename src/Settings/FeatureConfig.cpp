#include "Settings/FeatureConfig.h"
#include "Settings/SettingsRegistry.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace cs::settings
{
	std::string FormatValue(const Value& a_value)
	{
		return std::visit([](const auto& value) -> std::string {
			using T = std::remove_cvref_t<decltype(value)>;
			if constexpr (std::same_as<T, bool> || std::same_as<T, std::string>) {
				std::ostringstream output;
				output << toml::value{ value };
				return output.str();
			} else {
				std::array<char, 64> buffer{};
				const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
				if (error != std::errc{})
					throw std::runtime_error("Cannot format settings value");
				std::string text(buffer.data(), end);
				if constexpr (std::floating_point<T>) {
					if (text.find_first_of(".eE") == std::string::npos)
						text += ".0";
				}
				return text;
			}
		}, a_value);
	}
}

namespace cs::feature_config
{
	namespace
	{
		struct Store
		{
			std::mutex mutex;
			settings::Registry registry;
			std::filesystem::path path;
			toml::table root;
			std::string readError;
		};

		Store& Config()
		{
			static Store store;
			return store;
		}

		std::string FormatKey(std::string_view a_key)
		{
			if (!a_key.empty() && std::ranges::all_of(a_key, [](char c) {
					return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
						(c >= '0' && c <= '9') || c == '_' || c == '-';
				}))
				return std::string(a_key);
			return settings::FormatValue(std::string(a_key));
		}

		std::string FormatPath(std::span<const std::string> a_path)
		{
			std::string text;
			for (const auto& key : a_path) {
				if (!text.empty())
					text += '.';
				text += FormatKey(key);
			}
			return text;
		}

		std::string FormatNode(const toml::node& a_node)
		{
			if (const auto* table = a_node.as_table()) {
				std::string text = "{ ";
				for (const auto& [key, value] : *table) {
					if (text.size() > 2)
						text += ", ";
					text += FormatKey(key.str()) + " = " + FormatNode(value);
				}
				return text + " }";
			}
			if (const auto* array = a_node.as_array()) {
				std::string text = "[";
				for (const auto& value : *array) {
					if (text.size() > 1)
						text += ", ";
					text += FormatNode(value);
				}
				return text + "]";
			}
			std::ostringstream output;
			output << toml::toml_formatter{ a_node };
			return output.str();
		}

		template <class String>
		const toml::table* FindTable(const toml::table& a_root, std::span<const String> a_path, bool& a_blocked)
		{
			const auto* table = &a_root;
			for (const auto& key : a_path) {
				const auto* node = table->get(key);
				if (!node)
					return nullptr;
				table = node->as_table();
				if (!table) {
					a_blocked = true;
					return nullptr;
				}
			}
			return table;
		}

		template <class String>
		toml::table* EnsureTablePath(toml::table& a_root, std::span<const String> a_path, std::string& a_error)
		{
			auto* current = &a_root;
			for (const auto& key : a_path) {
				auto* existing = current->get(key);
				if (existing && !existing->is_table()) {
					a_error = "Path component '" + std::string(key) + "' is not a table";
					return nullptr;
				}
				if (!existing) {
					current->insert(key, toml::table{});
					existing = current->get(key);
				}
				current = existing->as_table();
			}
			return current;
		}

		const settings::Section* FindSection(const settings::Registry& a_registry, std::span<const std::string> a_path)
		{
			const auto found = std::ranges::find_if(a_registry, [&](const auto& section) {
				return std::ranges::equal(section.path, a_path);
			});
			return found == a_registry.end() ? nullptr : &*found;
		}

		void RenderUnknownTables(
			std::string& a_output, const toml::table& a_table,
			std::vector<std::string>& a_path, const settings::Registry& a_registry)
		{
			if (!a_path.empty()) {
				const bool hasValues = std::ranges::any_of(a_table, [](const auto& entry) {
					return !entry.second.is_table();
				});
				if (hasValues || (a_table.empty() && !FindSection(a_registry, a_path))) {
					a_output += "\n[" + FormatPath(a_path) + "]\n";
					for (const auto& [key, value] : a_table) {
						if (!value.is_table())
							a_output += FormatKey(key.str()) + " = " + FormatNode(value) + "\n";
					}
				}
			}
			for (const auto& [key, value] : a_table) {
				if (const auto* child = value.as_table()) {
					a_path.emplace_back(key.str());
					RenderUnknownTables(a_output, *child, a_path, a_registry);
					a_path.pop_back();
				}
			}
		}

		WriteResult WriteError(const std::filesystem::path& a_path, std::string_view a_detail)
		{
			return { false, "Failed to write configuration file '" + a_path.string() + "': " + std::string(a_detail) };
		}

		WriteResult AtomicWrite(const std::filesystem::path& a_path, std::string_view a_text)
		{
			std::error_code ec;
			const auto parent = a_path.parent_path();
			if (!parent.empty()) {
				std::filesystem::create_directories(parent, ec);
				if (ec)
					return WriteError(a_path, ec.message());
			}
			static std::atomic_uint64_t sequence{ 0 };
			auto temporary = a_path;
			temporary += ".tmp." + std::to_string(GetCurrentProcessId()) + "." +
				std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output.is_open())
					return WriteError(a_path, "unable to open temporary file");
				output << a_text;
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
			if (!MoveFileExW(temporary.c_str(), a_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				const auto moveError = std::error_code(static_cast<int>(GetLastError()), std::system_category());
				std::filesystem::remove(temporary, ec);
				return WriteError(a_path, moveError.message());
			}
			return { .success = true };
		}

		WriteResult WriteIfChanged(const std::filesystem::path& a_path, std::string_view a_text)
		{
			std::ifstream input(a_path, std::ios::binary);
			const std::string existing{ std::istreambuf_iterator<char>{ input }, {} };
			if (!input.bad() && existing == a_text)
				return { .success = true };
			input.close();
			const auto write = AtomicWrite(a_path, a_text);
			if (!write)
				return write;
			const auto verification = LoadFile(a_path);
			if (verification.status != FileLoadStatus::kParsed)
				return WriteError(a_path, "write verification failed: " + verification.error);
			if (verification.table != toml::parse(a_text))
				return WriteError(a_path, "written configuration did not read back as expected");
			return { .success = true };
		}

		bool MutateSection(
			toml::table& a_root, const settings::Registry& a_registry,
			const settings::Section& a_section, const toml::table& a_value, std::string& a_error)
		{
			auto* table = EnsureTablePath(a_root, std::span{ a_section.path }, a_error);
			if (!table)
				return false;
			if (a_section.openMap) {
				*table = a_value;
				return true;
			}
			for (const auto& field : a_section.fields) {
				table->erase(field.key);
				if (const auto* value = a_value.get(field.key)) {
					const auto typed = field.read(*value);
					if (!typed || *typed != field.defaultValue)
						table->insert(field.key, *value);
				}
			}
			for (const auto& child : a_registry) {
				if (child.path.size() != a_section.path.size() + 1 ||
					!std::equal(a_section.path.begin(), a_section.path.end(), child.path.begin()))
					continue;
				if (const auto* value = a_value[child.path.back()].as_table()) {
					if (!MutateSection(a_root, a_registry, child, *value, a_error))
						return false;
				}
			}
			return true;
		}
	}

	std::string RenderDocument(const settings::Registry& a_registry, const toml::table& a_root)
	{
		std::string output = "# FO4 Community Shaders settings. Uncomment a line to change it; the in-game menu writes this file.\n";
		auto remaining = a_root;
		for (const auto& [key, value] : a_root) {
			if (!value.is_table()) {
				output += FormatKey(key.str()) + " = " + FormatNode(value) + "\n";
				remaining.erase(key);
			}
		}
		for (const auto& section : a_registry) {
			bool blocked = false;
			const auto* table = FindTable(a_root, std::span{ section.path }, blocked);
			if (blocked)
				continue;
			output += "\n[" + FormatPath(section.path) + "]\n";
			std::string error;
			auto* rest = EnsureTablePath(remaining, std::span{ section.path }, error);
			if (table) {
				for (const auto& [key, value] : *table) {
					if (std::ranges::find(section.fields, key.str(), &settings::FieldView::key) != section.fields.end())
						continue;
					if (!value.is_table() || section.openMap) {
						output += FormatKey(key.str()) + " = " + FormatNode(value) + "\n";
						rest->erase(key);
					}
				}
			}
			for (const auto& field : section.fields) {
				output += "# " + field.description;
				if (field.timing != settings::ApplyTiming::kImmediate)
					output += " (restart required)";
				output += '\n';
				const auto* value = table ? table->get(field.key) : nullptr;
				if (value) {
					const auto typed = field.read(*value);
					output += FormatKey(field.key) + " = " +
						(typed ? settings::FormatValue(*typed) : FormatNode(*value)) + "\n";
				} else {
					output += "# " + FormatKey(field.key) + " = " + settings::FormatValue(field.defaultValue) + "\n";
				}
				rest->erase(field.key);
			}
		}
		std::vector<std::string> path;
		RenderUnknownTables(output, remaining, path, a_registry);
		return output;
	}

	FileLoadResult LoadFile(const std::filesystem::path& a_path)
	{
		const auto ioError = [&](std::string_view detail) -> FileLoadResult {
			return { FileLoadStatus::kIoError, {}, "Failed to read configuration file '" + a_path.string() + "': " + std::string(detail) };
		};
		std::error_code ec;
		const bool exists = std::filesystem::exists(a_path, ec);
		if (ec)
			return ioError(ec.message());
		if (!exists)
			return {};
		if (!std::filesystem::is_regular_file(a_path, ec))
			return ioError(ec ? ec.message() : "path is not a regular file");
		std::ifstream input(a_path, std::ios::binary);
		if (!input.is_open())
			return ioError("unable to open file");
		std::string contents;
		std::array<char, 4096> buffer{};
		while (true) {
			input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			contents.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
			if (input.bad())
				return ioError("stream read failed");
			if (input.eof())
				break;
			if (input.fail())
				return ioError("stream read failed before end of file");
		}
		try {
			return { FileLoadStatus::kParsed, toml::parse(contents, a_path.string()), {} };
		} catch (const toml::parse_error& error) {
			return { FileLoadStatus::kParseError, {},
				"Failed to parse configuration file '" + a_path.string() + "': " + std::string(error.description()) };
		}
	}

	RefreshResult Initialize(settings::Registry a_registry)
	{
		return InitializeAt(kConfigPath, std::move(a_registry));
	}

	RefreshResult InitializeAt(const std::filesystem::path& a_path, settings::Registry a_registry)
	{
		auto& store = Config();
		std::scoped_lock lock(store.mutex);
		store.registry = std::move(a_registry);
		store.path = a_path;
		const auto loaded = LoadFile(a_path);
		store.readError = loaded.error;
		const auto document = RenderDocument(store.registry, loaded.table);
		store.root = toml::parse(document);
		std::string error = loaded.error;
		if (loaded.status == FileLoadStatus::kMissing || loaded.status == FileLoadStatus::kParsed) {
			const auto written = WriteIfChanged(a_path, document);
			error = written.error;
		}
		return { store.root, loaded.status, std::move(error) };
	}

	toml::table GetRoot()
	{
		auto& store = Config();
		std::scoped_lock lock(store.mutex);
		return store.root;
	}

	std::optional<toml::table> GetFeature(std::string_view a_key)
	{
		const auto root = GetRoot();
		if (const auto* feature = root["features"][a_key].as_table())
			return *feature;
		return std::nullopt;
	}

	WriteResult UpdateOwnedSettingsAt(
		const std::filesystem::path& a_filePath, std::span<const std::string_view> a_path, const toml::table& a_value)
	{
		auto& store = Config();
		std::scoped_lock lock(store.mutex);
		if (a_filePath != store.path)
			return WriteError(a_filePath, "settings store has not been initialized for this path");
		if (!store.readError.empty())
			return WriteError(a_filePath, "writes are disabled until the settings file is repaired and the game restarted: " + store.readError);
		const std::vector<std::string> path(a_path.begin(), a_path.end());
		const auto* section = FindSection(store.registry, path);
		if (!section)
			return WriteError(a_filePath, "unregistered settings section");
		auto candidate = store.root;
		std::string error;
		if (!MutateSection(candidate, store.registry, *section, a_value, error))
			return WriteError(a_filePath, error);
		const auto document = RenderDocument(store.registry, candidate);
		const auto written = WriteIfChanged(a_filePath, document);
		if (written)
			store.root = toml::parse(document);
		return written;
	}

	WriteResult UpdateFeatureOwnedSettings(std::string_view a_featureKey, const toml::table& a_delta)
	{
		const std::array path{ std::string_view("features"), a_featureKey, std::string_view("settings") };
		return UpdateOwnedSettingsAt(kConfigPath, path, a_delta);
	}

	WriteResult UpdateFeatureLoad(std::string_view a_featureKey, bool a_load)
	{
		const std::array path{ std::string_view("features"), a_featureKey };
		return UpdateOwnedSettingsAt(kConfigPath, path, toml::table{ { "load", a_load } });
	}

	WriteResult UpdateTopLevelSection(std::string_view a_section, const toml::table& a_value)
	{
		const std::array path{ a_section };
		return UpdateOwnedSettingsAt(kConfigPath, path, a_value);
	}

	ShaderOwnershipParseResult ParseShaderOwnership(const toml::table& a_root)
	{
		ShaderOwnershipParseResult result;
		result.config.enabled = settings::core::ShaderOwnership{}.enabled;
		result.config.targets.enabled.fill(settings::core::ShaderTarget{}.enabled);
		const auto* ownershipNode = a_root.get("shader_ownership");
		if (!ownershipNode)
			return result;
		const auto fail = [&](std::string error) {
			result.valid = false;
			result.config = {};
			result.error = std::move(error);
			return result;
		};
		const auto* ownership = ownershipNode->as_table();
		if (!ownership)
			return fail("shader_ownership must be a table");
		if (ReadBool(*ownership, "enabled", result.config.enabled) == ScalarReadStatus::kWrongType)
			return fail("shader_ownership.enabled must be a boolean");
		const auto* targetsNode = ownership->get("targets");
		if (!targetsNode)
			return result;
		const auto* targets = targetsNode->as_table();
		if (!targets)
			return fail("shader_ownership.targets must be a table");
		for (const auto& [key, node] : *targets) {
			const auto* target = engine::FindShaderInjectionTarget(key.str());
			if (!target)
				return fail("shader_ownership.targets contains unknown target '" + std::string(key.str()) + "'");
			if (!node.is_boolean())
				return fail("shader_ownership.targets." + std::string(key.str()) + " must be a boolean");
			result.config.targets[target->id] = node.as_boolean()->get();
		}
		return result;
	}

	ScalarReadStatus ReadBool(const toml::table& a_table, std::string_view a_key, bool& a_value)
	{
		const auto* node = a_table.get(a_key);
		if (!node)
			return ScalarReadStatus::kMissing;
		if (!node->is_boolean())
			return ScalarReadStatus::kWrongType;
		a_value = node->as_boolean()->get();
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadSignedInteger(
		const toml::table& a_table, std::string_view a_key, std::int64_t& a_value, std::int64_t a_min, std::int64_t a_max)
	{
		const auto* node = a_table.get(a_key);
		if (!node)
			return ScalarReadStatus::kMissing;
		if (!node->is_integer())
			return ScalarReadStatus::kWrongType;
		const auto value = node->as_integer()->get();
		if (value < a_min || value > a_max)
			return ScalarReadStatus::kOutOfRange;
		a_value = value;
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadUnsignedInteger(
		const toml::table& a_table, std::string_view a_key, std::uint64_t& a_value, std::uint64_t a_min, std::uint64_t a_max)
	{
		const auto* node = a_table.get(a_key);
		if (!node)
			return ScalarReadStatus::kMissing;
		if (!node->is_integer())
			return ScalarReadStatus::kWrongType;
		const auto value = node->as_integer()->get();
		if (value < 0)
			return ScalarReadStatus::kOutOfRange;
		const auto unsignedValue = static_cast<std::uint64_t>(value);
		if (unsignedValue < a_min || unsignedValue > a_max)
			return ScalarReadStatus::kOutOfRange;
		a_value = unsignedValue;
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadFloat(
		const toml::table& a_table, std::string_view a_key, float& a_value, float a_min, float a_max)
	{
		const auto* node = a_table.get(a_key);
		if (!node)
			return ScalarReadStatus::kMissing;
		return ReadFloat(*node, a_value, a_min, a_max);
	}

	ScalarReadStatus ReadFloat(const toml::node& a_node, float& a_value, float a_min, float a_max)
	{
		if (!a_node.is_floating_point() && !a_node.is_integer())
			return ScalarReadStatus::kWrongType;
		const double value = a_node.is_floating_point() ?
			a_node.as_floating_point()->get() : static_cast<double>(a_node.as_integer()->get());
		if (!std::isfinite(value))
			return ScalarReadStatus::kInvalidValue;
		const auto floatValue = static_cast<float>(value);
		if (!std::isfinite(floatValue) || floatValue < a_min || floatValue > a_max)
			return ScalarReadStatus::kOutOfRange;
		a_value = floatValue;
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadDouble(
		const toml::table& a_table, std::string_view a_key, double& a_value, double a_min, double a_max)
	{
		const auto* node = a_table.get(a_key);
		if (!node)
			return ScalarReadStatus::kMissing;
		if (!node->is_floating_point() && !node->is_integer())
			return ScalarReadStatus::kWrongType;
		const double value = node->is_floating_point() ?
			node->as_floating_point()->get() : static_cast<double>(node->as_integer()->get());
		if (!std::isfinite(value))
			return ScalarReadStatus::kInvalidValue;
		if (value < a_min || value > a_max)
			return ScalarReadStatus::kOutOfRange;
		a_value = value;
		return ScalarReadStatus::kValid;
	}

	ScalarReadStatus ReadString(const toml::table& a_table, std::string_view a_key, std::string& a_value)
	{
		const auto* node = a_table.get(a_key);
		if (!node)
			return ScalarReadStatus::kMissing;
		if (!node->is_string())
			return ScalarReadStatus::kWrongType;
		a_value = node->as_string()->get();
		return ScalarReadStatus::kValid;
	}

	ActivationResult ParseActivation(const toml::table& a_table)
	{
		const auto* load = a_table.get("load");
		if (!load)
			return {};
		if (!load->is_boolean())
			return { .load = false, .valid = false };
		return { .load = load->as_boolean()->get(), .valid = true };
	}
}
