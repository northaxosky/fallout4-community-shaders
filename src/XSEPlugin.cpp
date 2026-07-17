#include "Env.h"
#include "Feature.h"
#include "Log.h"
#include "Render/D3D11Bootstrap.h"
#include "Telemetry/Telemetry.h"

#include <filesystem>

#include <toml++/toml.hpp>

namespace { auto* L = cs::log::Get("cs"); }

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Plugin::NAME.data();
	a_info->version = Plugin::VERSION[0];
	return true;
}

extern "C" DLLEXPORT constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};

	data.PluginVersion(Plugin::VERSION);
	data.PluginName(Plugin::NAME.data());
	data.AuthorName("northaxosky");
	data.UsesAddressLibrary(true);
	data.UsesSigScanning(false);
	data.IsLayoutDependent(true);
	data.HasNoStructUse(false);
	data.CompatibleVersions({ F4SE::RUNTIME_LATEST });

	return data;
}();

static void OnMessage(F4SE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case F4SE::MessagingInterface::kPostPostLoad:
		cs::FeatureManager::Get().OnPostPostLoadAll();
		break;
	case F4SE::MessagingInterface::kGameDataReady:
		cs::FeatureManager::Get().OnDataLoadedAll();
		break;
	default:
		break;
	}
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::InitInfo initInfo{};
	initInfo.trampoline = true;
	initInfo.trampolineSize = 1 << 10;
	F4SE::Init(a_f4se, initInfo);

	cs::log::AttachToDefaultLogger();
	toml::table loggingConfig;
	constexpr const char* globalConfigPath =
		"Data\\F4SE\\Plugins\\FO4CommunityShaders\\FO4CommunityShaders.toml";
	std::error_code configExistsError;
	if (std::filesystem::exists(globalConfigPath, configExistsError) && !configExistsError) {
		try {
			const auto globalConfig = toml::parse_file(globalConfigPath);
			if (const auto* logging = globalConfig["logging"].as_table())
				loggingConfig = *logging;
			else if (globalConfig.contains("logging"))
				L->warn("Global config [logging] must be a table; using logging defaults");
		} catch (const toml::parse_error& e) {
			L->warn("Failed to parse global logging config {} ({}); using defaults",
				globalConfigPath, e.description());
		}
	}
	cs::log::ApplyConfigFromToml(loggingConfig);
	cs::env::DetectENB();

	L->info("FO4CommunityShaders v{}.{}.{} loaded",
		Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);
	L->info("BUILD_DESCRIBE {}", CS_BUILD_DESCRIBE);

	auto& featureManager = cs::FeatureManager::Get();
	featureManager.PrepareAll();
	featureManager.ActivateAll();
	cs::telemetry::Install();
	cs::d3d11::InstallBootstrapHook();

	const auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(OnMessage);

	return true;
}
