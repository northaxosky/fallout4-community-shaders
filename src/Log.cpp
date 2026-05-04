#include "Log.h"

#include <algorithm>

namespace cs::log
{
	namespace
	{
		// Pattern includes logger name (%n) so we can see the subsystem in the log
		// without manually prefixing every message. Mirrors F4SE's own pattern
		// otherwise (timestamp, thread id, level).
		constexpr const char* kPattern = "[%T.%e] [%=5t] [%L] [%n] %v";

		void ConfigureFromDefault(spdlog::logger& a_logger, const spdlog::logger& a_default)
		{
			auto& sinks = a_logger.sinks();
			sinks.clear();
			const auto& defSinks = a_default.sinks();
			sinks.insert(sinks.end(), defSinks.begin(), defSinks.end());
			a_logger.set_level(a_default.level());
			a_logger.flush_on(a_default.flush_level());
			a_logger.set_pattern(kPattern);
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
		spdlog::apply_all([a_level](std::shared_ptr<spdlog::logger> a_logger) {
			a_logger->set_level(a_level);
		});
	}

	void SetLevel(const char* a_name, spdlog::level::level_enum a_level)
	{
		if (auto logger = spdlog::get(a_name); logger)
			logger->set_level(a_level);
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
}
