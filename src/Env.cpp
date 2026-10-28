#include "Env.h"

#include "ENB/ENBSeriesAPI.h"
#include "Log.h"

namespace cs::env
{
	namespace
	{
		auto* L = cs::log::Get("cs.env");
		bool g_detected = false;
		bool g_enbLoaded = false;
		bool g_conflictsDetected = false;
		std::vector<std::string> g_conflicts;

		struct PluginRule
		{
			std::string_view name;
			std::string_view file;
			std::string_view reason;
		};

		constexpr std::array kIncompatiblePlugins{
			PluginRule{ "Motion Vector Fixes", "MotionVectorFixes.dll", "CS includes Motion Vector Fixes; remove the standalone mod" },
			PluginRule{ "Dynamic Cubemaps F4", "DynamicCubemapF4.dll", "CS includes Dynamic Cubemaps; remove the standalone mod" },
		};
		constexpr std::array<PluginRule, 0> kRequiredPlugins{};

		bool PluginExists(std::string_view a_file)
		{
			std::error_code ec;
			const auto path = std::filesystem::path("Data/F4SE/Plugins") / a_file;
			return std::filesystem::exists(path, ec);
		}
	}

	void DetectENB()
	{
		if (g_detected)
			return;
		g_detected = true;
		g_enbLoaded = ENB_API::RequestENBAPI() != nullptr;
		L->info("ENB {}", g_enbLoaded ? "detected" : "not detected");
	}

	bool IsENBLoaded() noexcept
	{
		return g_enbLoaded;
	}

	void DetectPluginConflicts()
	{
		if (g_conflictsDetected)
			return;
		g_conflictsDetected = true;
		for (const auto& rule : kIncompatiblePlugins) {
			if (PluginExists(rule.file))
				g_conflicts.push_back(std::format("Incompatible plugin {} detected: {}", rule.name, rule.reason));
		}
		for (const auto& rule : kRequiredPlugins) {
			if (!PluginExists(rule.file))
				g_conflicts.push_back(std::format("Required plugin {} not found: {}", rule.name, rule.reason));
		}
		for (const auto& conflict : g_conflicts)
			L->error("{}", conflict);
	}

	bool IsBlocked() noexcept
	{
		return !g_conflicts.empty();
	}

	void ShowBlockingErrors()
	{
		auto* manager = RE::MessageMenuManager::GetSingleton();
		if (!manager)
			return;
		for (const auto& conflict : g_conflicts) {
			const auto body = std::format("{}.\nCommunity Shaders has disabled all hooks and features.", conflict);
			manager->Create("Community Shaders", body.c_str(), nullptr, RE::WARNING_TYPES::kDefault, "$OK");
		}
	}
}
