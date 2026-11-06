#include "Env.h"

#include "ENB/ENBSeriesAPI.h"
#include "Log.h"
#include "Settings/FeatureKeys.h"

namespace cs::env
{
	namespace
	{
		auto* L = cs::log::Get("cs.env");
		bool g_detected = false;
		bool g_enbLoaded = false;
		bool g_conflictsDetected = false;
		std::vector<std::string> g_conflicts;
		// Feature config key to the reason it is deactivated.
		std::vector<std::pair<std::string_view, std::string>> g_featureConflicts;

		enum class PluginRoot
		{
			kF4SEPlugins,
			kGameDirectory
		};

		struct PluginRule
		{
			std::string_view name;
			std::string_view file;
			std::string_view reason;
			// Optional extra gate on the DLL at the path; null means any copy conflicts.
			bool (*match)(const std::filesystem::path&) = nullptr;
			// Empty blocks all of CS; a feature key deactivates only that feature.
			std::string_view feature = {};
			PluginRoot root = PluginRoot::kF4SEPlugins;
		};

		// doodlum's standalone Upscaling is 1.1.x; jarari's Upscaling Custom is 1.7+.
		bool IsLegacyUpscaling(const std::filesystem::path& a_dll)
		{
			// Maps the DLL without running DllMain or resolving its imports.
			const auto module = ::LoadLibraryExW(a_dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
			const auto* data = module ? reinterpret_cast<const F4SE::PluginVersionData*>(::GetProcAddress(module, "F4SEPlugin_Version")) : nullptr;
			const auto version = data && data->dataVersion == F4SE::PluginVersionData::kVersion ? data->pluginVersion : 0;
			if (module)
				::FreeLibrary(module);
			const bool blocked = version && version < REL::Version{ 1, 2 }.pack();
			L->info("{} reports F4SE plugin version {:08X}; {}", a_dll.filename().string(), version, blocked ? "blocked" : "not blocked");
			return blocked;
		}

		constexpr std::array kIncompatiblePlugins{
			PluginRule{ "Motion Vector Fixes", "MotionVectorFixes.dll", "CS includes Motion Vector Fixes; remove the standalone mod" },
			PluginRule{ "Dynamic Cubemaps F4", "DynamicCubemapF4.dll", "CS includes Dynamic Cubemaps; remove the standalone mod" },
			PluginRule{ "Upscaling (doodlum)", "Upscaling.dll", "CS includes Upscaling and Frame Generation; remove doodlum's standalone Upscaling mod", IsLegacyUpscaling },
		};
		constexpr std::array<PluginRule, 0> kRequiredPlugins{};

		// Both rewrite the tonemap and swap chain that HDR Display owns.
		constexpr std::array kFeatureConflicts{
			PluginRule{ "Isotope", "FO4Isotope.dll", "Isotope owns HDR output; remove it to use HDR Display", nullptr, feature_config::kHDRDisplay },
			PluginRule{ "Isotope", "00FO4Isotope.dll", "Isotope owns HDR output; remove it to use HDR Display", nullptr, feature_config::kHDRDisplay },
			PluginRule{ "RenoDX", "renodx-fallout4.addon64", "RenoDX owns HDR output; remove it to use HDR Display", nullptr, feature_config::kHDRDisplay, PluginRoot::kGameDirectory },
		};

		std::filesystem::path PluginPath(const PluginRule& a_rule)
		{
			const auto root = a_rule.root == PluginRoot::kGameDirectory ? std::filesystem::path(".") : std::filesystem::path("Data/F4SE/Plugins");
			return root / a_rule.file;
		}

		bool PluginExists(const PluginRule& a_rule)
		{
			std::error_code ec;
			return std::filesystem::exists(PluginPath(a_rule), ec);
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
			if (PluginExists(rule) && (!rule.match || rule.match(PluginPath(rule))))
				g_conflicts.push_back(std::format("Incompatible plugin {} detected: {}", rule.name, rule.reason));
		}
		for (const auto& rule : kRequiredPlugins) {
			if (!PluginExists(rule))
				g_conflicts.push_back(std::format("Required plugin {} not found: {}", rule.name, rule.reason));
		}
		for (const auto& conflict : g_conflicts)
			L->error("{}", conflict);
		for (const auto& rule : kFeatureConflicts) {
			if (PluginExists(rule) && (!rule.match || rule.match(PluginPath(rule)))) {
				g_featureConflicts.emplace_back(rule.feature, std::format("{} ({}) is installed: {}.", rule.name, rule.file, rule.reason));
				L->warn("Feature {} deactivated: {}", rule.feature, g_featureConflicts.back().second);
			}
		}
	}

	std::optional<std::string_view> FeatureConflict(std::string_view a_featureKey) noexcept
	{
		for (const auto& [key, reason] : g_featureConflicts) {
			if (key == a_featureKey)
				return reason;
		}
		return std::nullopt;
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
