#pragma once

#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace cs::log
{
	// Lazy-create a named logger that shares the F4SE-default logger's sinks.
	// Idempotent: returns the existing logger if `name` is already registered.
	// Safe to call before F4SE::Init — returns a sinkless fallback that
	// AttachToDefaultLogger() upgrades once the real logger is available.
	spdlog::logger* Get(const char* a_name);

	// Re-sync every cs.* logger's sinks with spdlog::default_logger()'s sinks.
	// Call once from F4SEPlugin_Load AFTER F4SE::Init has installed the real
	// default logger, so file-static loggers created during static-init pick
	// up the file/MSVC sinks.
	void AttachToDefaultLogger();

	// Apply a level to the default logger and every cs.* logger currently
	// registered. Returns immediately if no default logger has been set up
	// yet (called too early during F4SE init).
	void SetGlobalLevel(spdlog::level::level_enum a_level);

	// Apply a level to a single named logger. No-op if the name isn't
	// registered yet.
	void SetLevel(const char* a_name, spdlog::level::level_enum a_level);

	// Snapshot of all registered logger names whose name starts with "cs".
	// Used to populate the per-logger override UI.
	std::vector<std::string> ListLoggers();
}
