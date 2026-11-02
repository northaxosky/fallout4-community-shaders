#include "LODBlending.h"
#include "Render/SharedDataLayout.h"

#include <DearModdingUI/Client.h>

#include <string>
#include <type_traits>
#include <utility>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/ObjectLOD.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

#include "RE/B/BSGeometry.h"
#include "RE/B/BSRenderPass.h"
#include "RE/B/BSShaderProperty.h"
#include "RE/M/Main.h"
#include "RE/N/NiNode.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.lodblending");

		bool ClassifyObjectLOD(cs::engine::PrepassDrawContext& a_draw) noexcept
		{
			return a_draw.InObjectLOD();
		}
	}

	LODBlending* LODBlending::GetSingleton()
	{
		static LODBlending instance;
		return &instance;
	}

	bool LODBlending::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(lod_blending::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(lod_blending::kSchema, _settings);
		return true;
	}

	bool LODBlending::SaveSettings()
	{
		return settings::SaveDelta(lod_blending::kSchema, GetConfigKey(), _settings, *L);
	}

	void LODBlending::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("LODBlending", *this)) {
			FailLoad("LOD Blending shader contribution registration failed.");
			return;
		}
		if (!cs::engine::RegisterPrepassDrawClassifier(*this, cs::engine::PrepassLaneComponent::kX, &ClassifyObjectLOD)) {
			FailLoad("LOD Blending prepass draw classifier installation failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	void LODBlending::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("registrations_ready", _registrationsReady.load(std::memory_order_relaxed));
		cs::engine::WritePrepassLaneTelemetry(a_sink, cs::engine::PrepassLaneComponent::kX, "lod");
	}

	bool LODBlending::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "shader contributions did not all register";
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b6 carries no LOD blending settings";
			return false;
		}
		if (!cs::engine::ValidateShaderInjectionRoutes("LODBlending", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	void LODBlending::WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB& a_fo4)
	{
		if (!_injectionsOperational.load(std::memory_order_acquire))
			return;
		a_fo4.EnabledLODBlending = 1u;
		a_feature.lodBlendingSettings = {
			.LODTerrainBrightness = _settings.LODTerrainBrightness,
			.LODObjectBrightness = _settings.LODObjectBrightness,
			.DisableTerrainVertexColors = _settings.DisableTerrainVertexColors ? 1u : 0u,
			.LODTerrainGamma = _settings.LODTerrainGamma,
			.LODObjectGamma = _settings.LODObjectGamma
		};
	}

	void LODBlending::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(lod_blending::kSchema, _settings);
		if (const ui::Section section{ "lod-brightness", "Brightness" }; section) {
			field(&Settings::LODTerrainBrightness, { .format = "%.2f" });
			field(&Settings::LODObjectBrightness, { .format = "%.2f" });
		}
		if (const ui::Section section{ "lod-gamma", "Gamma" }; section) {
			field(&Settings::LODTerrainGamma, { .format = "%.2f" });
			field(&Settings::LODObjectGamma, { .format = "%.2f" });
		}
		if (const ui::Section section{ "lod-terrain", "Terrain" }; section)
			field(&Settings::DisableTerrainVertexColors);
	}

	void LODBlending::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(LODBlending::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
