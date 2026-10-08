#include "LODBlending.h"

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

		bool ClassifyObjectLOD(RE::BSRenderPass* a_pass, cs::engine::PrepassBakePath) noexcept
		{
			return cs::engine::IsObjectLODShape(a_pass);
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
		if (!cs::engine::RegisterPrepassDrawClassifier(cs::engine::PrepassLaneComponent::kX, &ClassifyObjectLOD)) {
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

	cs::LODBlendingFeatureData LODBlending::GetCommonBufferData() const
	{
		if (!_injectionsOperational.load(std::memory_order_acquire))
			return {};
		cs::LODBlendingFeatureData data{};
		data.LODTerrainBrightness = _settings.LODTerrainBrightness;
		data.LODObjectBrightness = _settings.LODObjectBrightness;
		data.LODTerrainGamma = _settings.LODTerrainGamma;
		data.LODObjectGamma = _settings.LODObjectGamma;
		data.DisableTerrainVertexColors = _settings.DisableTerrainVertexColors ? 1u : 0u;
		return data;
	}

	void LODBlending::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto draw = [&](const auto& field) {
			auto& value = _settings.*field.member;
			const std::string label = std::string(field.description) + "##" + std::string(field.key);
			if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, bool>) {
				edit.Discrete(dmui::ui::Checkbox(label.c_str(), &value));
				if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone };
					tooltip.Visible()) {
					dmui::ui::Text("%s",
						"Disables vertex coloring on nearby terrain. Best combined with terrain LOD generated in xLODGen with Vertex Color Intensity set to 0.");
				}
			} else {
				const auto range = lod_blending::kSchema.EditRange(field.member);
				edit.Continuous(dmui::ui::SliderScalar(label.c_str(), &value, &range.min, &range.max, "%.2f"));
			}
		};
		const auto& fields = lod_blending::kSchema.fields;
		if (const ui::Section section{ "lod-brightness", "Brightness" }; section) {
			draw(std::get<0>(fields));
			draw(std::get<1>(fields));
		}
		if (const ui::Section section{ "lod-gamma", "Gamma" }; section) {
			draw(std::get<2>(fields));
			draw(std::get<3>(fields));
		}
		if (const ui::Section section{ "lod-terrain", "Terrain" }; section)
			draw(std::get<4>(fields));
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
