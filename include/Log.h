#pragma once

#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace cs::log
{
	// Safe to call before F4SE::Init; returns a sinkless fallback until AttachToDefaultLogger fires.
	spdlog::logger* Get(const char* a_name);

	// Call once from F4SEPlugin_Load after F4SE::Init so file-static loggers pick up the real sinks.
	void AttachToDefaultLogger();

	void SetGlobalLevel(spdlog::level::level_enum a_level);
	void SetLevel(const char* a_name, spdlog::level::level_enum a_level);
	std::vector<std::string> ListLoggers();
}
