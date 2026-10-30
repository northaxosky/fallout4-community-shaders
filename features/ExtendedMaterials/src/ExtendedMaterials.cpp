#include "ExtendedMaterials.h"
#include "Render/SharedDataLayout.h"

#include <DearModdingUI/Client.h>

#include <array>
#include <string>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/ObjectLOD.h"
#include "Render/OverlayShape.h"
#include "Render/PrepassExtraTargets.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSRenderPass.h"
#include "RE/N/NiRTTI.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.extendedmaterials");

		constexpr std::uint32_t kParallaxShadowRTVSlot = 6;
		constexpr std::uint32_t kParallaxShadowPSSlot = 66;

		// Height lives in the _s alpha, so only formats that store alpha can carry it.
		[[nodiscard]] bool HasAlphaChannel(DXGI_FORMAT a_format) noexcept
		{
			switch (a_format) {
			case DXGI_FORMAT_BC2_UNORM:
			case DXGI_FORMAT_BC2_UNORM_SRGB:
			case DXGI_FORMAT_BC3_UNORM:
			case DXGI_FORMAT_BC3_UNORM_SRGB:
			case DXGI_FORMAT_BC7_UNORM:
			case DXGI_FORMAT_BC7_UNORM_SRGB:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				return true;
			default:
				return false;
			}
		}

		// FO4: the _s format stands in for the parallax flag; no LOD or overlays.
		bool IsParallaxEligible(cs::engine::PrepassDrawContext& a_draw) noexcept
		{
			auto* pass = a_draw.pass;
			if (a_draw.InObjectLOD() || cs::engine::IsOverlayShape(pass))
				return false;
			const auto* property = netimmerse_cast<RE::BSLightingShaderProperty*>(pass->GetShaderProperty());
			const auto* material = property ? static_cast<const RE::BSLightingShaderMaterialBase*>(property->material) : nullptr;
			return material && HasAlphaChannel(cs::engine::GetTextureFormat(material->smoothnessSpecMaskTexture.get()));
		}
	}

	ExtendedMaterials* ExtendedMaterials::GetSingleton()
	{
		static ExtendedMaterials instance;
		return &instance;
	}

	bool ExtendedMaterials::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(extended_materials::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(extended_materials::kSchema, _settings);
		return true;
	}

	bool ExtendedMaterials::SaveSettings()
	{
		return settings::SaveDelta(extended_materials::kSchema, GetConfigKey(), _settings, *L);
	}

	bool ExtendedMaterials::ShadowsActive() const noexcept
	{
		return _injectionsOperational.load(std::memory_order_acquire) && IsEnabled() && _settings.EnableShadows &&
		       (_settings.EnableParallax || _settings.EnableTerrain);
	}

	void ExtendedMaterials::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("ExtendedMaterials", *this, cs::engine::AttachPrepassExtraTargets)) {
			FailLoad("Extended Materials shader contribution registration failed.");
			return;
		}
		if (!cs::engine::RegisterPrepassExtraTarget(*this, { .name = "ExtendedMaterials/ParallaxShadow", .rtvSlot = kParallaxShadowRTVSlot, .format = DXGI_FORMAT_R8_UNORM, .clear = { 1, 1, 1, 1 }, .publishSlot = kParallaxShadowPSSlot, .active = [this] { return ShadowsActive(); } })) {
			FailLoad("Extended Materials parallax shadows need the prepass target.");
			return;
		}
		if (!cs::engine::RegisterPrepassDrawClassifier(*this, cs::engine::PrepassLaneComponent::kZ, &IsParallaxEligible)) {
			FailLoad("Extended Materials prepass draw classifier installation failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	void ExtendedMaterials::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("registrations_ready", _registrationsReady.load(std::memory_order_relaxed));
		cs::engine::WritePrepassLaneTelemetry(a_sink, cs::engine::PrepassLaneComponent::kZ, "eligible");
	}

	bool ExtendedMaterials::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "shader contributions did not all register";
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b6 carries no Extended Materials settings";
			return false;
		}
		if (!cs::engine::ValidateShaderInjectionRoutes("ExtendedMaterials", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	void ExtendedMaterials::WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB&)
	{
		if (!_injectionsOperational.load(std::memory_order_acquire))
			return;
		auto& data = a_feature.extendedMaterialSettings;
		data.EnableParallax = _settings.EnableParallax ? 1u : 0u;
		data.EnableTerrainParallax = _settings.EnableTerrain ? 1u : 0u;
		data.EnableHeightBlending = _settings.EnableHeightBlending ? 1u : 0u;
		data.EnableShadows = _settings.EnableShadows ? 1u : 0u;
		data.EnableParallaxWarpingFix = _settings.EnableParallaxWarpingFix ? 1u : 0u;
	}

	void ExtendedMaterials::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto& [parallax, terrain, heightBlending, shadows, warpingFix] = extended_materials::kSchema.fields;
		if (const ui::Section section{ "extended-materials-parallax", "Parallax" }; section) {
			edit.Toggle(parallax, _settings, "Enables parallax on standard meshes made for parallax.");
			edit.Toggle(terrain, _settings,
				"Enables terrain parallax using the alpha channel of each landscape specular (_s) texture. "
				"Layers without a height map stay flat.");
			edit.Toggle(heightBlending, _settings, "Enables landscape texture blending based on parallax. ");
			edit.Toggle(warpingFix, _settings, "Enables a fix reducing parallax scale on curved and smooth normal triangles.");
		}
		if (const ui::Section section{ "extended-materials-shadows", "Approximate Soft Shadows" }; section) {
			edit.Toggle(shadows, _settings, "Enables cheap soft shadows when using parallax. This applies to sunlight.");
		}
	}

	void ExtendedMaterials::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(ExtendedMaterials::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
