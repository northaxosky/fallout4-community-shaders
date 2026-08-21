#include "Env.h"
#include "Feature.h"
#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/SwapChainHook.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

namespace
{
	auto* L = cs::log::Get("cs");

	bool ApplyShaderOwnershipConfig(
		const cs::feature_config::ShaderOwnershipConfig& a_config)
	{
		using cs::engine::ShaderInjectionTarget;

		bool applied = true;
		applied &= cs::engine::SetBaselineShaderOwnership(
			ShaderInjectionTarget::kDeferredPrepass,
			a_config.enabled && a_config.targets.deferredPrepass);
		applied &= cs::engine::SetBaselineShaderOwnership(
			ShaderInjectionTarget::kBsdfLight,
			a_config.enabled && a_config.targets.bsdfLight);
		applied &= cs::engine::SetBaselineShaderOwnership(
			ShaderInjectionTarget::kBsdfComposite,
			a_config.enabled && a_config.targets.bsdfComposite);
		applied &= cs::engine::SetBaselineShaderOwnership(
			ShaderInjectionTarget::kBsSky,
			a_config.enabled && a_config.targets.bsSky);
		applied &= cs::engine::SetBaselineShaderOwnership(
			ShaderInjectionTarget::kBsWater,
			a_config.enabled && a_config.targets.bsWater);
		applied &= cs::engine::SetBaselineShaderOwnership(
			ShaderInjectionTarget::kBsLighting,
			a_config.enabled && a_config.targets.bsLighting);
		return applied;
	}
}

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
	// new runtime checklist: all 14 write_thunk_call sites, including reticleOffsets + literal
	// unknown runtimes fall through to AE and silently apply stale offsets
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
	const auto config = cs::feature_config::Reload();
	if (!config.defaultLoaded) {
		L->error(
			"Unified default configuration unavailable; all features and baseline shader ownership disabled: {}",
			config.defaultError);
	}
	if (!config.userWarning.empty()) {
		L->warn("Ignoring unified user configuration: {}", config.userWarning);
	}
	toml::table loggingConfig;
	if (const auto* logging = config.root["logging"].as_table()) {
		loggingConfig = *logging;
	} else if (config.root.contains("logging")) {
		L->warn("Unified config [logging] must be a table; using logging defaults");
	}
	cs::log::ApplyConfigFromToml(loggingConfig);
	const auto shaderOwnership =
		cs::feature_config::ParseShaderOwnership(config.root);
	if (!shaderOwnership.present) {
		L->warn(
			"Unified config [shader_ownership] is missing; baseline shader ownership disabled");
	} else if (!shaderOwnership.valid) {
		L->error(
			"Invalid [shader_ownership] configuration; baseline shader ownership disabled: {}",
			shaderOwnership.error);
	} else if (!ApplyShaderOwnershipConfig(shaderOwnership.config)) {
		L->error(
			"Baseline shader ownership configuration was rejected; all unclaimed targets remain stock");
	}
	cs::env::DetectENB();

	L->info("FO4CommunityShaders v{}.{}.{} loaded",
		Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);
	L->info("BUILD_DESCRIBE {}", CS_BUILD_DESCRIBE);

	cs::engine::InstallShaderSubclassHooks();

	auto& featureManager = cs::FeatureManager::Get();
	featureManager.PrepareAll();
	featureManager.ActivateAll();
	cs::telemetry::Install();
	cs::render::InstallSwapChainHook();

	const auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(OnMessage);

	return true;
}
