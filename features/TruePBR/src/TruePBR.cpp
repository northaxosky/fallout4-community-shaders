#include "TruePBR.h"

#include "MaterialLoadHooks.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/SharedFeatureData.h"
#include "Settings/SettingsPersistence.h"

namespace cs::features
{
	TruePBR* TruePBR::GetSingleton()
	{
		static TruePBR instance;
		return &instance;
	}

	bool TruePBR::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(true_pbr::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		return true;
	}

	void TruePBR::WriteSharedData(render::SharedFeatureDataCB& a_shared, render::FO4SharedDataCB&)
	{
		a_shared.truePBRSettings.VertexAOStrength = _settings.VertexAOStrength;
		a_shared.truePBRSettings.EnableMicroShadows = _settings.EnableMicroShadows;
		a_shared.truePBRSettings.MicroShadowStrength = _settings.MicroShadowStrength;
	}

	void TruePBR::Load()
	{
		if (!true_pbr::InstallMaterialLoadHooks()) {
			FailLoad("True PBR material hooks failed to install.");
			return;
		}
		Log()->info("Installed hooks");
	}

	bool TruePBR::SaveSettings()
	{
		return settings::SaveDelta(true_pbr::kSchema, GetConfigKey(), _settings, *Log());
	}

	void TruePBR::RestoreDefaultSettings()
	{
		_settings = {};
		SaveSettings();
	}

	void TruePBR::DrawSettings()
	{
		using namespace true_pbr;
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(kSchema, _settings);
		if (const ui::Section section{ "true-pbr-global", "Global Settings" }; section) {
			field(&Settings::VertexAOStrength);
			field(&Settings::EnableMicroShadows, { .help = "Occludes direct light with the material's ambient occlusion map, adding contact shadowing to surface detail such as seams, rivets and fabric weave." });
			if (_settings.EnableMicroShadows)
				field(&Settings::MicroShadowStrength, { .help = "How strongly ambient occlusion darkens direct light." });
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(TruePBR::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
