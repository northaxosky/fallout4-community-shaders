#include "BuildIdentity.h"
#include "Env.h"
#include "Feature.h"
#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Render/ForwardDraw.h"
#include "Render/FrameAnnotations.h"
#include "Render/FrameBoundary.h"
#include "Render/FrameBuffer.h"
#include "Render/FrameProfiler.h"
#include "Render/ShaderCompileGate.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/SwapChainHook.h"
#include "Render/TemporalPipeline.h"
#include "Settings/FeatureConfig.h"
#include "Settings/SettingsRegistry.h"
#include "Telemetry/Telemetry.h"

namespace
{
	auto* L = cs::log::Get("cs");
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
	// NG F4SE checks the 1.10.980 bits; AE F4SE checks 1.11.137.
	data.UsesAddressLibraryNG(true);
	data.UsesSigScanning(false);
	data.IsLayoutDependent(true);
	data.IsLayoutDependentNG(true);
	data.HasNoStructUse(false);
	data.CompatibleVersions({ F4SE::RUNTIME_LATEST });

	return data;
}();

static void OnBlockedMessage(F4SE::MessagingInterface::Message* a_msg)
{
	if (a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		cs::env::ShowBlockingErrors();
}

static void OnMessage(F4SE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case F4SE::MessagingInterface::kPostPostLoad:
		cs::FeatureManager::Get().OnPostPostLoadAll();
		cs::render::TemporalPipeline::Get().FreezeRequest();
		cs::host::HostClient::Get().DiscoverAndRegister();
		break;
	case F4SE::MessagingInterface::kGameDataReady:
		cs::FeatureManager::Get().OnDataLoadedAll();
		cs::render::frame_annotations::OnDataLoaded();
		cs::render::TemporalPipeline::Get().OnDataLoaded();
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
	auto registry = cs::settings::BuildCoreRegistry();
	for (auto* feature : cs::FeatureManager::Get().GetRegisteredFeatures())
		cs::settings::AddFeatureSections(
			registry, feature->GetConfigKey(), feature->GetSettingsSchema(), feature->GetWeatherProfiles() != nullptr,
			feature->SupportsEnableToggle() ? std::optional{ feature->DefaultEnabled() } : std::nullopt);
	const auto config = cs::feature_config::Initialize(std::move(registry));
	if (!config.error.empty())
		L->error("Settings initialization: {}", config.error);
	for (const auto& warning : config.warnings)
		L->warn("{}", warning);
	toml::table loggingConfig;
	if (const auto* logging = config.root["logging"].as_table()) {
		loggingConfig = *logging;
	}
	cs::log::ApplyConfigFromToml(loggingConfig);
	const auto shaderOwnership =
		cs::feature_config::ParseShaderOwnership(config.root);
	if (!shaderOwnership.valid) {
		L->error(
			"Invalid [shader_ownership] configuration; shader replacements disabled: {}",
			shaderOwnership.error);
	}
	cs::engine::ApplyShaderOwnershipConfig(shaderOwnership.config);
	cs::engine::SetShaderInjectionRuntime(
		REX::FModule::IsRuntimeOG() ? cs::engine::GameRuntime::kOG :
		REX::FModule::IsRuntimeNG() ? cs::engine::GameRuntime::kNG :
									  cs::engine::GameRuntime::kAE);
	cs::env::DetectENB();
	cs::env::DetectPluginConflicts();

	L->info("FO4CommunityShaders v{}.{}.{} loaded (build {}, commit {})",
		Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2],
		CS_BUILD_DESCRIBE, CS_BUILD_GIT_SHA);

	if (cs::env::IsBlocked()) {
		// Returning true keeps F4SE from reporting a failed plugin.
		F4SE::GetMessagingInterface()->RegisterListener(OnBlockedMessage);
		return true;
	}

	cs::engine::InstallShaderSubclassHooks();
	cs::engine::InstallShaderCompileLoadGate();
	cs::render::TemporalPipeline::Get().RegisterCreationRouter();

	auto& featureManager = cs::FeatureManager::Get();
	featureManager.PrepareAll();
	featureManager.ActivateAll();
	cs::Menu::Get().Load();
	cs::engine::InstallFrameBuffer();
	cs::telemetry::Install();
	if (!cs::render::profiling::InstallDeferredCompositePass())
		L->error("DeferredComposite profiler pass registration failed");
	cs::render::forward_draw::Install();
	cs::render::frame_annotations::Install();
	// Registration closes once the boundary installs.
	cs::render::RegisterPostSwap(&cs::engine::StopShaderCompileOnQuit);
	cs::render::InstallFrameBoundary();
	if (!cs::render::InstallSwapChainHook()) {
		L->error("D3D11 swap-chain hook installation failed");
	}

	const auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(OnMessage);

	return true;
}
