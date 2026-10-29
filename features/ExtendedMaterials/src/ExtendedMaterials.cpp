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
#include "Render/FrameBindings.h"
#include "Render/ObjectLOD.h"
#include "Render/OverlayShape.h"
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
		bool IsParallaxEligible(RE::BSRenderPass* a_pass, cs::engine::PrepassBakePath) noexcept
		{
			if (cs::engine::IsObjectLODShape(a_pass) || cs::engine::IsOverlayShape(a_pass))
				return false;
			const auto* property = netimmerse_cast<RE::BSLightingShaderProperty*>(a_pass->GetShaderProperty());
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

	void ExtendedMaterials::Prepass()
	{
		_shadowFrameReady = false;
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) : nullptr;
		if (!context)
			return;
		// Unbind DFLight's SRV before the prepass renders into the target.
		ID3D11ShaderResourceView* none = nullptr;
		cs::engine::BindFrameShaderResources(context, cs::engine::ShaderStage::kPixel, kParallaxShadowPSSlot, 1, &none);
		const auto* graphics = cs::engine::GetGraphicsState();
		if (!graphics || !ShadowsActive())
			return;
		if (!_shadowTexture || _shadowTexture->desc.Width != graphics->screenWidth ||
			_shadowTexture->desc.Height != graphics->screenHeight) {
			try {
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = graphics->screenWidth;
				desc.Height = graphics->screenHeight;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				auto texture = std::make_unique<cs::buffer::Texture2D>(desc);
				texture->CreateSRV({ .Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, .Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 } });
				winrt::com_ptr<ID3D11Device> device;
				context->GetDevice(device.put());
				DX::ThrowIfFailed(device->CreateRenderTargetView(texture->resource.get(), nullptr, texture->rtv.put()));
				texture->SetName("ExtendedMaterials/ParallaxShadow.Texture", "ExtendedMaterials/ParallaxShadow.SRV", "", "ExtendedMaterials/ParallaxShadow.RTV");
				_shadowTexture = std::move(texture);
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Extended Materials parallax shadow target unavailable: {}", e.what());
				return;
			} catch (...) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Extended Materials parallax shadow target unavailable.");
				return;
			}
		}
		const float lit[4]{ 1, 1, 1, 1 };
		context->ClearRenderTargetView(_shadowTexture->rtv.get(), lit);
		_shadowFrameReady = true;
	}

	void ExtendedMaterials::BindShadowTarget(ID3D11DeviceContext* a_context)
	{
		// Only an engine output commit or plugin work can unbind Target6.
		const auto epoch = cs::engine::GetFrameBindingEpoch();
		if (!_shadowFrameReady || epoch == _shadowTargetEpoch)
			return;
		_shadowTargetEpoch = epoch;
		// FO4: SV_Target6 follows the engine's six G-buffer targets, left as bound.
		std::array<ID3D11RenderTargetView*, kParallaxShadowRTVSlot + 1> targets{};
		winrt::com_ptr<ID3D11DepthStencilView> depth;
		a_context->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth.put());
		auto* const bound = targets[kParallaxShadowRTVSlot];
		if (bound != _shadowTexture->rtv.get()) {
			targets[kParallaxShadowRTVSlot] = _shadowTexture->rtv.get();
			a_context->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth.get());
		}
		targets[kParallaxShadowRTVSlot] = bound;
		for (auto* target : targets) {
			if (target)
				target->Release();
		}
	}

	void ExtendedMaterials::PublishShadows(ID3D11DeviceContext* a_context) const
	{
		if (!_shadowFrameReady)
			return;
		std::array<ID3D11RenderTargetView*, kParallaxShadowRTVSlot + 1> targets{};
		winrt::com_ptr<ID3D11DepthStencilView> depth;
		a_context->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth.put());
		if (targets[kParallaxShadowRTVSlot] == _shadowTexture->rtv.get()) {
			targets[kParallaxShadowRTVSlot]->Release();
			targets[kParallaxShadowRTVSlot] = nullptr;
			a_context->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth.get());
		} else if (targets[kParallaxShadowRTVSlot]) {
			targets[kParallaxShadowRTVSlot]->Release();
		}
		for (std::size_t slot = 0; slot < kParallaxShadowRTVSlot; ++slot) {
			if (targets[slot])
				targets[slot]->Release();
		}
		auto* srv = _shadowTexture->srv.get();
		cs::engine::BindFrameShaderResources(a_context, cs::engine::ShaderStage::kPixel, kParallaxShadowPSSlot, 1, &srv);
	}

	void ExtendedMaterials::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("ExtendedMaterials", *this, [this](cs::engine::ShaderReplacementRegistration& registration) {
				if (registration.targetId == cs::engine::ShaderInjectionTarget::kDeferredPrepass)
					registration.bind = [this](ID3D11DeviceContext* a_context) { BindShadowTarget(a_context); };
			})) {
			FailLoad("Extended Materials shader contribution registration failed.");
			return;
		}
		if (!cs::engine::RegisterPostDeferredPrePass(*this, [this] {
				if (auto* rendererData = RE::BSGraphics::GetRendererData())
					PublishShadows(reinterpret_cast<ID3D11DeviceContext*>(rendererData->context));
			})) {
			FailLoad("Extended Materials parallax shadows need the post-prepass hook.");
			return;
		}
		if (!cs::engine::RegisterPrepassDrawClassifier(cs::engine::PrepassLaneComponent::kZ, &IsParallaxEligible)) {
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
