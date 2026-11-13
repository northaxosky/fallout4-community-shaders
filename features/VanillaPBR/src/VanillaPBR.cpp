#include "VanillaPBR.h"

#include "DynamicCubemaps.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/SharedData.h"
#include "Render/SharedDataLayout.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

#include <DearModdingUI/Client.h>

namespace cs::features
{
	namespace
	{
		constexpr std::array kDebugViews{
			FeatureDebugView{ "live", "Live sample (conditioned)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "authored", "Authored sample", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "environment", "Final environment (iblColor)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "live-light", "Live reflection light (ambient only)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "live-occlusion", "Live specular occlusion (SSGI, without native AO)", FeatureDebugViewKind::kFullscreen }
		};

	}

	VanillaPBR* VanillaPBR::GetSingleton()
	{
		static VanillaPBR instance;
		return &instance;
	}

	bool VanillaPBR::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(vanilla_pbr::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(vanilla_pbr::kSchema, _settings);
		return true;
	}

	void VanillaPBR::Load()
	{
		if (!engine::RegisterFeatureShaderBindings(*this)) {
			FailLoad("Vanilla PBR shader contribution registration failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	bool VanillaPBR::ValidateInjectionPrerequisites(std::string& a_error)
	{
		if (!render::IsSharedDataReady()) {
			a_error = "Vanilla PBR requires the shared b7 substrate";
			return false;
		}
		return true;
	}

	void VanillaPBR::WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4)
	{
		a_fo4.VanillaPBREnable = 1u;
		a_fo4.VanillaPBREnableGGX = _settings.EnableGGX;
		a_fo4.VanillaPBREnableEyeSpecialHandling = _settings.EnableEyeSpecialHandling;
		a_fo4.VanillaPBRRoughnessMultiplier = _settings.RoughnessMultiplier;
		a_fo4.VanillaPBRSpecularRoughnessBlend = _settings.SpecularRoughnessBlend;
		a_fo4.VanillaPBRBaseF0Multiplier = _settings.BaseF0Multiplier;
		a_fo4.VanillaPBRMinF0 = _settings.MinF0;
		a_fo4.VanillaPBRSplitScreen = _settings.SplitScreen;
		a_fo4.VanillaPBRSplitPosition = _settings.SplitPosition;
		a_fo4.VanillaPBRSplitMode = static_cast<std::uint32_t>(_settings.splitMode);
		const bool environmentReady = DynamicCubemaps::GetSingleton()->GetSharpReflectionsSRV() != nullptr;
		a_fo4.VanillaPBRLiveEnvironment = _settings.LiveEnvironment && environmentReady;
		_directEnabled = _settings.EnableGGX;
		_liveEnvironmentEnabled = _settings.LiveEnvironment;
		_liveEnvironmentReady = environmentReady;
	}

	bool VanillaPBR::SaveSettings()
	{
		return settings::SaveDelta(vanilla_pbr::kSchema, GetConfigKey(), _settings, *Log());
	}

	void VanillaPBR::RestoreDefaultSettings()
	{
		_settings = {};
		SaveSettings();
	}

	void VanillaPBR::DrawSettings()
	{
		using namespace vanilla_pbr;
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(kSchema, _settings);
		if (const ui::Section section{ "vanilla-pbr-general", "General" }; section) {
			field(&Settings::EnableGGX, { .help = "Replace direct Phong specular with GGX." });
			field(&Settings::EnableEyeSpecialHandling);
			field(&Settings::RoughnessMultiplier);
			field(&Settings::SpecularRoughnessBlend);
			field(&Settings::BaseF0Multiplier);
			field(&Settings::MinF0, { .help = "Minimum dielectric F0." });
			field(&Settings::LiveEnvironment, { .help = "Envmapped materials reflect the live Dynamic Cubemaps world through vanilla's envmap formula instead of their authored cubemap." });
			constexpr std::array splitModes{
				dmui::ChoiceOption<SplitMode>{ SplitMode::kDirect, "Direct", "direct" },
				dmui::ChoiceOption<SplitMode>{ SplitMode::kReflections, "Reflections", "reflections" },
				dmui::ChoiceOption<SplitMode>{ SplitMode::kCombined, "Combined", "combined" }
			};
			field(&Settings::SplitScreen, { .help = "Native on the left; selected Vanilla PBR paths on the right." });
			field.Choice(&Settings::splitMode, splitModes, { .help = "Choose which feature paths use the split-screen side; Reflections is the live environment." });
			field(&Settings::SplitPosition);
		}
	}

	std::span<const FeatureDebugView> VanillaPBR::GetDebugViews() const noexcept { return kDebugViews; }
	void VanillaPBR::SetDebugView(std::string_view a_view) noexcept
	{
		_debugMode = a_view == "live"           ? 1u :
		             a_view == "authored"       ? 2u :
		             a_view == "environment"    ? 3u :
		             a_view == "live-light"     ? 5u :
		             a_view == "live-occlusion" ? 6u :
		                                          0u;
	}
	FullscreenDebugData VanillaPBR::GetFullscreenDebugData() const noexcept
	{
		return { .owner = FullscreenDebugOwner::VanillaPBR, .mode = _debugMode.load() };
	}
	void VanillaPBR::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		a_sink.Field("direct_enabled", _directEnabled.load())
			.Field("live_environment_enabled", _liveEnvironmentEnabled.load())
			.Field("live_environment_ready", _liveEnvironmentReady.load())
			.Field("injection_operational", IsInjectionOperational());
	}
	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(VanillaPBR::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
