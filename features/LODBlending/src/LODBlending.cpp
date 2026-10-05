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

#include "RE/B/BSRenderPass.h"
#include "RE/B/BSShaderMaterial.h"
#include "RE/B/BSShaderProperty.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.lodblending");

		struct ObjectLODMarkers
		{
			bool flag = false;
			bool material = false;
		};

		// FO4 has no technique define for object LOD; the engine marks it on the property flag and the lighting material feature.
		ObjectLODMarkers ReadObjectLODMarkers(RE::BSShaderProperty* a_property) noexcept
		{
			ObjectLODMarkers markers;
			if (!a_property)
				return markers;
			markers.flag = a_property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kLODObjects);
			if (auto* material = a_property->material) {
				const auto feature = material->GetFeature();
				markers.material = feature == RE::BSShaderMaterial::Feature::kLODObjects ||
				                   feature == RE::BSShaderMaterial::Feature::kLODObjectsHD;
			}
			return markers;
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
		if (!cs::engine::RegisterPrepassDrawObserver({ &LODBlending::ClassifyPrepassDraw, &LODBlending::ApplyPrepassDraw })) {
			FailLoad("LOD Blending prepass draw observer installation failed.");
			return;
		}
		_observerInstalled.store(true, std::memory_order_release);
		if (!cs::engine::RegisterPostDeferredPrePass([this] { FinishPrepassFrame(); }, cs::engine::HookPriority::Late))
			L->warn("Object-LOD draw counters unavailable: post-prepass registration failed.");
		_registrationsReady.store(true, std::memory_order_release);
	}

	std::uint32_t LODBlending::ClassifyPrepassDraw(RE::BSRenderPass* a_pass) noexcept
	{
		auto* self = GetSingleton();
		const auto markers = ReadObjectLODMarkers(a_pass ? a_pass->GetShaderProperty() : nullptr);
		const bool lodObject = markers.flag || markers.material;
		++self->_frameCounts.draws;
		self->_frameCounts.lodDraws += lodObject ? 1u : 0u;
		self->_frameCounts.flagDraws += markers.flag ? 1u : 0u;
		self->_frameCounts.materialDraws += markers.material ? 1u : 0u;
		return lodObject ? 1u : 0u;
	}

	void LODBlending::ApplyPrepassDraw(std::uint32_t a_class) noexcept
	{
		cs::render::PublishLODObjectDraw(a_class != 0);
	}

	void LODBlending::FinishPrepassFrame() noexcept
	{
		const auto counts = std::exchange(_frameCounts, PrepassCounts{});
		if (counts.draws == 0)
			return;
		const auto* graphics = cs::engine::GetGraphicsState();
		const auto frame = graphics ? graphics->frameCount : 0u;
		_lastFrame.store(frame, std::memory_order_relaxed);
		_lastDraws.store(counts.draws, std::memory_order_relaxed);
		_lastLodDraws.store(counts.lodDraws, std::memory_order_relaxed);
		_lastFlagDraws.store(counts.flagDraws, std::memory_order_relaxed);
		_lastMaterialDraws.store(counts.materialDraws, std::memory_order_relaxed);
		_sessionDraws.fetch_add(counts.draws, std::memory_order_relaxed);
		_sessionLodDraws.fetch_add(counts.lodDraws, std::memory_order_relaxed);
		if (!_loggedFirstPrepass) {
			_loggedFirstPrepass = true;
			L->info("Object-LOD gate: first prepass frame={} prepass_draws={} lod_draws={} flag_draws={} material_draws={}",
				frame, counts.draws, counts.lodDraws, counts.flagDraws, counts.materialDraws);
		}
		if (!_loggedFirstLOD && counts.lodDraws != 0) {
			_loggedFirstLOD = true;
			L->info("Object-LOD gate: first LOD frame={} prepass_draws={} lod_draws={} flag_draws={} material_draws={}",
				frame, counts.draws, counts.lodDraws, counts.flagDraws, counts.materialDraws);
		}
	}

	void LODBlending::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("observer_installed", _observerInstalled.load(std::memory_order_relaxed))
			.Field("last_frame", _lastFrame.load(std::memory_order_relaxed))
			.Field("prepass_draws", _lastDraws.load(std::memory_order_relaxed))
			.Field("lod_draws", _lastLodDraws.load(std::memory_order_relaxed))
			.Field("lod_flag_draws", _lastFlagDraws.load(std::memory_order_relaxed))
			.Field("lod_material_draws", _lastMaterialDraws.load(std::memory_order_relaxed))
			.Field("session_prepass_draws", _sessionDraws.load(std::memory_order_relaxed))
			.Field("session_lod_draws", _sessionLodDraws.load(std::memory_order_relaxed));
	}

	bool LODBlending::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "shader contributions did not all register";
			_validationDetail = a_error;
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b6 carries no LOD blending settings";
			_validationDetail = a_error;
			return false;
		}
		if (!cs::engine::ValidateShaderInjectionRoutes("LODBlending", a_error)) {
			_validationDetail = a_error;
			return false;
		}
		_validationDetail.clear();
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
