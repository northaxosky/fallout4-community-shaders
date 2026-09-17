#pragma once

#include <spdlog/spdlog.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

namespace cs::input
{
	struct Hotkey;
}

namespace cs::log
{
	// Safe to call before F4SE::Init; returns a sinkless fallback until AttachToDefaultLogger fires.
	spdlog::logger* Get(const char* a_name);

	// Call once from F4SEPlugin_Load after F4SE::Init so file-static loggers pick up the real sinks.
	void AttachToDefaultLogger();

	void SetGlobalLevel(spdlog::level::level_enum a_level);
	void SetLevel(const char* a_name, spdlog::level::level_enum a_level);
	spdlog::level::level_enum GlobalLevel() noexcept;
	std::optional<spdlog::level::level_enum> LevelFromString(std::string_view a_level);
	std::string_view LevelToString(spdlog::level::level_enum a_level) noexcept;
	std::vector<std::string> ListLoggers();

	void ApplyConfigFromToml(const toml::table& a_logging);
	bool SaveConfigToToml();
	toml::table ConfigAsToml();
	cs::input::Hotkey GetDumpHotkey();
}
