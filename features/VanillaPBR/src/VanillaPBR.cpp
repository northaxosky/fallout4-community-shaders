#include "VanillaPBR.h"

#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/SharedData.h"
#include "Render/SharedDataLayout.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
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
		_directEnabled = _settings.EnableGGX;
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
			field(&Settings::SplitScreen, { .help = "Native on the left; Vanilla PBR on the right." });
			field(&Settings::SplitPosition);
		}
	}

	void VanillaPBR::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		a_sink.Field("direct_enabled", _directEnabled.load())
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
