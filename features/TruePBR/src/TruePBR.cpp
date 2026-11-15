#include "TruePBR.h"

#include "Host/HostClient.h"
#include "LoadNotices.h"
#include "MaterialLoadHooks.h"
#include "MaterialRegistry.h"
#include "MaterialTagHooks.h"
#include "MaterialTags.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/SharedFeatureData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

#include <DearModdingUI/Client.h>

#include <format>

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
		// Tag hooks first and the parse last, so a partial install tags nothing.
		if (!true_pbr::InstallMaterialTagHooks() || !true_pbr::InstallMaterialLoadHooks()) {
			FailLoad("True PBR material hooks failed to install.");
			return;
		}
		Log()->info("Installed hooks");
	}

	void TruePBR::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		using namespace true_pbr;
		a_sink
			.Field("records", Records().Count())
			.Field("rejected", Notices().RejectedCount())
			.Field("live_tags", Tags().Count())
			.Field("retag_conflicts", Tags().Conflicts())
			.Field("foreign_material_path", Tags().ForeignPaths());
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

	void TruePBR::DrawDebug()
	{
		using namespace true_pbr;
		dmui::ui::Text("Records: %zu  Live tagged materials: %zu", Records().Count(), Tags().Count());
		dmui::ui::Text("Rejected files: %zu", Notices().RejectedCount());
		if (const auto last = Notices().LastRejection(); !last.empty())
			dmui::ui::Text("Last rejection: %s", last.c_str());
		for (const auto& [id, live] : Tags().LiveByRecord()) {
			if (const auto* record = Records().Get(id))
				dmui::ui::Text("%zu  %s", live, record->name.c_str());
		}
	}

	void TruePBR::DrawSettings()
	{
		using namespace true_pbr;
		auto& client = host::HostClient::Get().Client();
		const auto draw = [&](const std::string& a_text, dmui::TextTone a_tone) {
			if (!dmui::DrawStyledText(client, a_text, { .tone = a_tone, .wrapped = true }))
				Log()->warn("DearModdingUI draw failed: {}", DMUI_ResultToString(client.LastResult()));
		};
		const auto records = Records().Count();
		draw(std::format("{} PBR material files loaded this session.", records), dmui::TextTone::kInherit);
		if (!IsEnabled() && records != 0)
			draw("True PBR is switched off, but loaded PBR materials keep their PBR textures and render through vanilla shading until restart.", dmui::TextTone::kStatusWarning);
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
