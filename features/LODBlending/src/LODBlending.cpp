#include "LODBlending.h"

#include <DearModdingUI/Client.h>

#include <string>
#include <type_traits>
#include <utility>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/SettingsEdit.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
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

		// BTO shapes hang under the land LOD root; terrain LOD there carries kLODLandscape. The kLODObjects property flag
		// and the LOD material features miss BTO shapes, so they cannot gate.
		bool IsObjectLOD(RE::BSRenderPass* a_pass) noexcept
		{
			auto* geometry = a_pass->GetGeometry();
			auto* property = a_pass->GetShaderProperty();
			const auto* landRoot = RE::Main::GetLandLODRoot();
			if (!geometry || !property || !landRoot || property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kLODLandscape))
				return false;
			for (const RE::NiNode* node = geometry->parent; node; node = node->parent) {
				if (node == landRoot)
					return true;
			}
			return false;
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
		if (!cs::engine::RegisterPrepassDrawClassifier(&LODBlending::ClassifyPrepassDraw)) {
			FailLoad("LOD Blending prepass draw classifier installation failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	bool LODBlending::ClassifyPrepassDraw(RE::BSRenderPass* a_pass, cs::engine::PrepassBakePath a_path) noexcept
	{
		const bool lodObject = IsObjectLOD(a_pass);
		auto& counts = GetSingleton()->_bakeCounts[static_cast<std::size_t>(a_path)];
		counts.draws.fetch_add(1, std::memory_order_relaxed);
		counts.lodDraws.fetch_add(lodObject ? 1u : 0u, std::memory_order_relaxed);
		return lodObject;
	}

	void LODBlending::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto& records = _bakeCounts[static_cast<std::size_t>(cs::engine::PrepassBakePath::kCommandBuffer)];
		const auto& immediate = _bakeCounts[static_cast<std::size_t>(cs::engine::PrepassBakePath::kImmediate)];
		const auto lanes = cs::engine::GetPrepassLaneStats();
		a_sink
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("registrations_ready", _registrationsReady.load(std::memory_order_relaxed))
			.Field("records", records.draws.load(std::memory_order_relaxed))
			.Field("lod_records", records.lodDraws.load(std::memory_order_relaxed))
			.Field("immediate_draws", immediate.draws.load(std::memory_order_relaxed))
			.Field("lod_immediate_draws", immediate.lodDraws.load(std::memory_order_relaxed))
			.Field("lanes_baked", lanes.baked)
			.Field("lanes_unavailable", lanes.unavailable);
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
		std::apply([&](const auto&... fields) {
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
			(draw(fields), ...);
		},
			lod_blending::kSchema.fields);
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
