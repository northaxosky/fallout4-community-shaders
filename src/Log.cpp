#include "Log.h"

#include "Settings/FeatureConfig.h"
#include "Settings/SettingsRegistry.h"
#include "Telemetry/Telemetry.h"
#include "Utils/Hotkey.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace cs::log
{
	namespace
	{
		// %n includes the logger name.
		constexpr const char* kPattern = "[%T.%e] [%=5t] [%L] [%n] %v";
		struct ConfigState
		{
			std::atomic<spdlog::level::level_enum> globalLevel{ *LevelFromString(settings::core::Logging{}.level) };
			std::mutex mutex;
			std::unordered_map<std::string, spdlog::level::level_enum> channelLevels;
			cs::input::Hotkey dumpHotkey = cs::input::Hotkey::Parse(settings::core::Logging{}.dumpHotkey);
		};

		ConfigState& Config()
		{
			static ConfigState state;
			return state;
		}

		spdlog::level::level_enum ConfiguredLevel(std::string_view a_name)
		{
			auto& config = Config();
			std::scoped_lock lock(config.mutex);
			if (const auto it = config.channelLevels.find(std::string(a_name));
				it != config.channelLevels.end()) {
				return it->second;
			}
			return config.globalLevel.load(std::memory_order_relaxed);
		}

		void ApplyConfiguredLevel(spdlog::logger& a_logger)
		{
			a_logger.set_level(ConfiguredLevel(a_logger.name()));
		}

		void ConfigureFromDefault(spdlog::logger& a_logger, const spdlog::logger& a_default)
		{
			auto& sinks = a_logger.sinks();
			sinks.clear();
			const auto& defSinks = a_default.sinks();
			sinks.insert(sinks.end(), defSinks.begin(), defSinks.end());
			a_logger.set_level(a_default.level());
			a_logger.flush_on(a_default.flush_level());
			a_logger.set_pattern(kPattern);
			ApplyConfiguredLevel(a_logger);
		}

		std::string Lowercase(std::string_view a_value)
		{
			std::string lowered(a_value);
			std::transform(lowered.begin(), lowered.end(), lowered.begin(),
				[](unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });
			return lowered;
		}
	}

	spdlog::logger* Get(const char* a_name)
	{
		if (auto existing = spdlog::get(a_name); existing)
			return existing.get();

		auto def = spdlog::default_logger();
		auto logger = std::make_shared<spdlog::logger>(a_name);
		if (def) {
			ConfigureFromDefault(*logger, *def);
		} else {
			ApplyConfiguredLevel(*logger);
		}
		spdlog::register_logger(logger);
		return logger.get();
	}

	void AttachToDefaultLogger()
	{
		auto def = spdlog::default_logger();
		if (!def) return;
		spdlog::apply_all([&](std::shared_ptr<spdlog::logger> a_logger) {
			if (a_logger.get() == def.get()) return;
			if (!a_logger->name().starts_with("cs")) return;
			ConfigureFromDefault(*a_logger, *def);
		});
	}

	void SetGlobalLevel(spdlog::level::level_enum a_level)
	{
		auto& config = Config();
		config.globalLevel.store(a_level, std::memory_order_relaxed);
		spdlog::apply_all([a_level](std::shared_ptr<spdlog::logger> a_logger) {
			a_logger->set_level(a_level);
		});

		std::vector<std::pair<std::string, spdlog::level::level_enum>> overrides;
		{
			std::scoped_lock lock(config.mutex);
			overrides.reserve(config.channelLevels.size());
			for (const auto& entry : config.channelLevels)
				overrides.push_back(entry);
		}
		for (const auto& [name, level] : overrides) {
			if (auto logger = spdlog::get(name); logger)
				logger->set_level(level);
		}
	}

	void SetLevel(const char* a_name, spdlog::level::level_enum a_level)
	{
		auto& config = Config();
		{
			std::scoped_lock lock(config.mutex);
			config.channelLevels.insert_or_assign(a_name, a_level);
		}
		if (auto logger = spdlog::get(a_name); logger)
			logger->set_level(a_level);
	}

	spdlog::level::level_enum GlobalLevel() noexcept
	{
		return Config().globalLevel.load(std::memory_order_relaxed);
	}

	std::optional<spdlog::level::level_enum> LevelFromString(std::string_view a_level)
	{
		const auto lowered = Lowercase(a_level);
		if (lowered == "trace") return spdlog::level::trace;
		if (lowered == "debug") return spdlog::level::debug;
		if (lowered == "info") return spdlog::level::info;
		if (lowered == "warn" || lowered == "warning") return spdlog::level::warn;
		if (lowered == "error" || lowered == "err") return spdlog::level::err;
		if (lowered == "critical") return spdlog::level::critical;
		if (lowered == "off") return spdlog::level::off;
		return std::nullopt;
	}

	std::string_view LevelToString(spdlog::level::level_enum a_level) noexcept
	{
		switch (a_level) {
		case spdlog::level::trace:
			return "trace";
		case spdlog::level::debug:
			return "debug";
		case spdlog::level::info:
			return "info";
		case spdlog::level::warn:
			return "warn";
		case spdlog::level::err:
			return "error";
		case spdlog::level::critical:
			return "critical";
		case spdlog::level::off:
			return "off";
		default:
			return "info";
		}
	}

	std::vector<std::string> ListLoggers()
	{
		std::vector<std::string> names;
		spdlog::apply_all([&names](std::shared_ptr<spdlog::logger> a_logger) {
			const auto& name = a_logger->name();
			if (name.starts_with("cs"))
				names.push_back(name);
		});
		std::sort(names.begin(), names.end());
		return names;
	}

	void ApplyConfigFromToml(const toml::table& a_logging)
	{
		auto* logger = Get("cs.log");
		for (const auto& [key, value] : a_logging) {
			(void)value;
			const bool known = key.str() == "channels" || std::apply([&](const auto&... fields) {
				return ((key.str() == fields.key) || ...);
			}, settings::core::kLogging.fields);
			if (!known)
				logger->warn("Unknown logging key '{}'; ignoring", key.str());
		}

		const settings::core::Logging defaults;
		auto& config = Config();
		{
			std::scoped_lock lock(config.mutex);
			config.channelLevels.clear();
			config.dumpHotkey = cs::input::Hotkey::Parse(defaults.dumpHotkey);
		}

		settings::core::Logging logging;
		if (std::string error; !settings::ParseTable(settings::core::kLogging, a_logging, logging, error)) {
			logger->warn("logging.{}; using logging defaults", error);
			logging = defaults;
		}

		if (const auto level = LevelFromString(logging.level)) {
			SetGlobalLevel(*level);
		} else {
			logger->warn("Invalid logging.level '{}'; using {}", logging.level, defaults.level);
			SetGlobalLevel(*LevelFromString(defaults.level));
		}

		if (const auto* channelsNode = a_logging.get("channels")) {
			if (const auto* channels = channelsNode->as_table()) {
				for (const auto& [key, value] : *channels) {
					const auto levelText = value.value<std::string>();
					if (!levelText) {
						logger->warn("logging.channels.{} must be a string", key.str());
						continue;
					}
					const auto level = LevelFromString(*levelText);
					if (!level) {
						logger->warn("Invalid level '{}' for logging channel {}", *levelText, key.str());
						continue;
					}
					const std::string channel(key.str());
					SetLevel(channel.c_str(), *level);
				}
			} else {
				logger->warn("logging.channels must be a table");
			}
		}

		cs::telemetry::pump::SetEnabled(logging.telemetry);
		cs::telemetry::pump::SetIntervalSeconds(logging.telemetryIntervalSeconds);

		bool validHotkey = false;
		auto hotkey = cs::input::Hotkey::Parse(logging.dumpHotkey, &validHotkey);
		if (validHotkey) {
			std::scoped_lock lock(config.mutex);
			config.dumpHotkey = hotkey;
		} else {
			logger->warn("Invalid logging.dump_hotkey '{}'; using {}", logging.dumpHotkey, defaults.dumpHotkey);
		}
	}

	toml::table ConfigAsToml()
	{
		std::vector<std::pair<std::string, spdlog::level::level_enum>> overrides;
		std::string hotkey;
		{
			auto& config = Config();
			std::scoped_lock lock(config.mutex);
			overrides.reserve(config.channelLevels.size());
			for (const auto& entry : config.channelLevels)
				overrides.push_back(entry);
			hotkey = config.dumpHotkey.ToString();
		}
		std::sort(overrides.begin(), overrides.end(),
			[](const auto& a_lhs, const auto& a_rhs) { return a_lhs.first < a_rhs.first; });
		auto logging = settings::SerializeFull(settings::core::kLogging, settings::core::Logging{
			.level = std::string(LevelToString(GlobalLevel())),
			.telemetry = cs::telemetry::pump::Enabled(),
			.telemetryIntervalSeconds = cs::telemetry::pump::IntervalSeconds(),
			.dumpHotkey = std::move(hotkey)
		});

		toml::table channels;
		for (const auto& [name, level] : overrides)
			channels.insert_or_assign(name, std::string(LevelToString(level)));
		logging.insert_or_assign("channels", std::move(channels));
		return logging;
	}

	bool SaveConfigToToml()
	{
		auto* logger = Get("cs.log");
		const auto result = feature_config::UpdateTopLevelSection("logging", ConfigAsToml());
		if (!result) {
			logger->warn("Failed to save logging configuration: {}", result.error);
			return false;
		}
		return true;
	}

	cs::input::Hotkey GetDumpHotkey()
	{
		auto& config = Config();
		std::scoped_lock lock(config.mutex);
		return config.dumpHotkey;
	}
}
