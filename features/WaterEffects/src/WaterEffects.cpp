#include "WaterEffects.h"

#include <DearModdingUI/Client.h>
#include <DirectXTex.h>
#include <d3d11.h>

#include <array>
#include <exception>
#include <format>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderContributions.h"
#include "Render/RenderExtents.h"
#include "Render/RenderHooks.h"
#include "Render/ScopedContextState.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/Water.h"

namespace cs::features
{
	namespace we = cs::features::water_effects;

	namespace
	{
		auto* L = cs::log::Get("cs.feature.watereffects");

		constexpr const wchar_t* kCausticsPath =
			L"Data\\Shaders\\WaterEffects\\watercaustics.dds";

		constexpr std::array<FeatureDebugView, 2> kDebugViews{ { { "water_caustics",
																	 "Caustics multiplier on submerged surfaces",
																	 FeatureDebugViewKind::kFullscreen },
			{ "water_submersion",
				"Depth below the cell water plane",
				FeatureDebugViewKind::kFullscreen } } };

		ID3D11DeviceContext* GetImmediateContext() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			return rendererData ?
			           reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			           nullptr;
		}

		std::string_view DebugVisualizationName(
			WaterEffects::DebugVisualization a_visualization) noexcept
		{
			switch (a_visualization) {
			case WaterEffects::DebugVisualization::kCaustics:
				return "water_caustics";
			case WaterEffects::DebugVisualization::kSubmersion:
				return "water_submersion";
			default:
				return "off";
			}
		}
	}

	WaterEffects* WaterEffects::GetSingleton()
	{
		static WaterEffects instance;
		return &instance;
	}

	std::span<const FeatureDebugView>
	WaterEffects::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}

	void WaterEffects::SetDebugView(std::string_view a_view) noexcept
	{
		auto visualization = DebugVisualization::kOff;
		if (a_view == "water_caustics")
			visualization = DebugVisualization::kCaustics;
		else if (a_view == "water_submersion")
			visualization = DebugVisualization::kSubmersion;
		_debugVisualization.store(visualization, std::memory_order_release);
	}

	bool WaterEffects::Configure(
		const toml::table& a_config,
		std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(we::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(we::kSchema, _settings, [this] { PublishSettings(); });
		if (!a_config.contains("settings"))
			PublishSettings();
		return true;
	}

	void WaterEffects::PublishSettings() noexcept
	{
		_enabled.store(_settings.enabled, std::memory_order_release);
	}

	bool WaterEffects::SaveSettings()
	{
		return settings::SaveDelta(we::kSchema, GetConfigKey(), _settings, *L);
	}

	void WaterEffects::Load()
	{
		PublishSettings();

		// FO4: only owned, validated routes may activate a live contribution.
		if (!cs::engine::RegisterFeatureShaderContributions("WaterEffects", [this](cs::engine::ShaderReplacementRegistration& registration) {
				registration.isReady = [this] {
					return _registrationsReady.load(std::memory_order_acquire) && _resourcesReady.load(std::memory_order_acquire) && cs::render::IsSharedDataReady();
				};
				if (registration.targetId == cs::engine::ShaderInjectionTarget::kBsdfLight) {
					registration.slotClaims.push_back({ .stage = cs::engine::ShaderStage::kPixel,
						.resourceType = cs::engine::ShaderResourceType::kShaderResource,
						.slot = kCausticsPSSlot });
					registration.slotClaims.push_back({ .stage = cs::engine::ShaderStage::kPixel,
						.resourceType = cs::engine::ShaderResourceType::kSampler,
						.slot = kCausticsSamplerPSSlot });
					registration.bind = [this](ID3D11DeviceContext* a_context) { BindCaustics(a_context); };
				}
			})) {
			FailLoad("Water caustics shader contribution registration failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);

		cs::engine::RegisterPreDeferredLightsImpl(
			[] { WaterEffects::GetSingleton()->SaveEngineBindings(); },
			cs::engine::HookPriority::Early);
		cs::engine::RegisterPostDeferredLightsImpl(
			[] { WaterEffects::GetSingleton()->RestoreEngineBindings(); },
			cs::engine::HookPriority::Late);
		if (!cs::engine::RegisterPreDeferredComposite(
				[this] {
					if (auto* context = GetImmediateContext())
						RenderDebug(context);
				},
				cs::engine::HookPriority::Early)) {
			FailLoad(
				"Water caustics debug views need a deferred-composite producer");
			return;
		}
		_renderCallbacksReady.store(true, std::memory_order_release);

		L->info(
			"Water caustics installed: hooks=deferred_lights+deferred_composite, "
			"consumers=BSDFLight+BSDFComposite t{}/s{}+host debug, enabled={}.",
			kCausticsPSSlot,
			kCausticsSamplerPSSlot,
			_settings.enabled);
	}

	bool WaterEffects::BuildCausticsResources(
		ID3D11Device* a_device,
		std::string& a_error)
	{
		a_error.clear();
		_causticsTexture = nullptr;
		if (!a_device) {
			a_error = "no D3D11 device";
			return false;
		}

		DirectX::ScratchImage loaded;
		DirectX::TexMetadata metadata{};
		const auto loadResult = DirectX::LoadFromDDSFile(
			kCausticsPath, DirectX::DDS_FLAGS_NONE, &metadata, loaded);
		if (FAILED(loadResult)) {
			a_error = std::format(
				"could not read Data\\Shaders\\WaterEffects\\watercaustics.dds "
				"(HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(loadResult));
			return false;
		}
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1) {
			a_error = "watercaustics.dds is not a single 2D image";
			return false;
		}

		const auto viewResult = DirectX::CreateShaderResourceView(
			a_device,
			loaded.GetImages(),
			loaded.GetImageCount(),
			metadata,
			_causticsSrv.put());
		if (FAILED(viewResult)) {
			a_error = std::format(
				"could not create the caustics shader resource view "
				"(HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(viewResult));
			return false;
		}
		winrt::com_ptr<ID3D11Resource> causticsResource;
		_causticsSrv->GetResource(causticsResource.put());
		if (!causticsResource ||
			FAILED(causticsResource->QueryInterface(
				IID_PPV_ARGS(_causticsTexture.put())))) {
			a_error = "caustics shader resource view has no texture";
			return false;
		}
		cs::render::annotation::SetName(
			_causticsTexture.get(), "WaterEffects/Caustics.Texture");
		cs::render::annotation::SetName(
			_causticsSrv.get(), "WaterEffects/Caustics.SRV");

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		const auto samplerResult = a_device->CreateSamplerState(
			&samplerDesc, _causticsSampler.put());
		if (FAILED(samplerResult)) {
			a_error = std::format(
				"could not create the caustics sampler (HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(samplerResult));
			return false;
		}
		cs::render::annotation::SetName(
			_causticsSampler.get(), "WaterEffects/Caustics.Sampler");
		return true;
	}

	void WaterEffects::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		std::string error;
		bool built = false;
		try {
			built = BuildCausticsResources(a_device, error);
		} catch (const std::exception& e) {
			error = e.what();
		} catch (...) {
			error = "unknown failure";
		}
		if (!built) {
			_causticsTexture = nullptr;
			_causticsSrv = nullptr;
			_causticsSampler = nullptr;
			SetValidationDetail(error);
			L->error("Water caustics resources failed: {}", error);
			return;
		}
		_resourcesReady.store(true, std::memory_order_release);
		L->info("Water caustics texture and sampler ready.");
		try {
			BuildDebugResources(a_device);
			_debugResourcesReady.store(_debugVS && _debugPS[0] && _debugPS[1], std::memory_order_release);
		} catch (const std::exception& e) {
			L->warn("Water debug resources unavailable: {}", e.what());
		}
	}

	void WaterEffects::BuildDebugResources(ID3D11Device* a_device)
	{
		winrt::com_ptr<ID3D11Device1> device;
		DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(device.put())));
		const auto level = a_device->GetFeatureLevel();
		D3D_FEATURE_LEVEL selected{};
		DX::ThrowIfFailed(device->CreateDeviceContextState(
			0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &selected,
			_debugContextState.put()));
		_debugVS.attach(static_cast<ID3D11VertexShader*>(cs::util::CompileShader(
			L"Data\\Shaders\\FO4\\WaterEffects\\Debug.hlsl", {}, "vs_5_0")));
		for (std::size_t index = 0; index < _debugPS.size(); ++index) {
			std::vector<std::pair<const char*, const char*>> defines{ { "FO4CS_SUBSTRATE", "1" } };
			if (index == 1)
				defines.emplace_back("WATER_SUBMERSION_DEBUG", "1");
			_debugPS[index].attach(static_cast<ID3D11PixelShader*>(cs::util::CompileShader(
				L"Data\\Shaders\\FO4\\WaterEffects\\Debug.hlsl", defines, "ps_5_0")));
		}
		D3D11_RASTERIZER_DESC rasterizer{};
		rasterizer.FillMode = D3D11_FILL_SOLID;
		rasterizer.CullMode = D3D11_CULL_NONE;
		rasterizer.DepthClipEnable = true;
		DX::ThrowIfFailed(a_device->CreateRasterizerState(&rasterizer, _debugRasterizer.put()));
	}

	void WaterEffects::RenderDebug(ID3D11DeviceContext* a_context)
	{
		_debugFrameReady = false;
		cs::render::InvalidateFullscreenDebugData();
		const auto mode = _debugVisualization.load(std::memory_order_acquire);
		if (!CanBind() || mode == DebugVisualization::kOff ||
			!_debugResourcesReady.load(std::memory_order_acquire))
			return;
		if (!cs::render::GetCanonicalSceneDepthSRV()) {
			_debugDepthMissing.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		const auto* graphics = cs::engine::GetGraphicsState();
		if (!graphics)
			return;
		const auto extent = cs::render::GetActiveExtent(graphics->screenWidth, graphics->screenHeight);
		if (!extent.width || !extent.height)
			return;
		try {
			if (!_debugTexture || _debugTexture->desc.Width != graphics->screenWidth ||
				_debugTexture->desc.Height != graphics->screenHeight) {
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = graphics->screenWidth;
				desc.Height = graphics->screenHeight;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				auto texture = std::make_unique<cs::buffer::Texture2D>(desc);
				winrt::com_ptr<ID3D11Device> device;
				a_context->GetDevice(device.put());
				DX::ThrowIfFailed(device->CreateShaderResourceView(
					texture->resource.get(), nullptr, texture->srv.put()));
				DX::ThrowIfFailed(device->CreateRenderTargetView(
					texture->resource.get(), nullptr, texture->rtv.put()));
				texture->SetName("WaterEffects/Debug.Texture", "WaterEffects/Debug.SRV", "", "WaterEffects/Debug.RTV");
				_debugTexture = std::move(texture);
			}
			// FO4: restore the complete context because native bindings are shadow-cached.
			cs::render::ScopedContextState state(a_context, _debugContextState.get());
			if (!state.IsActive())
				return;
			cs::render::annotation::ScopedEvent annotation("WaterEffects/Debug");
			a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			a_context->VSSetShader(_debugVS.get(), nullptr, 0);
			a_context->PSSetShader(_debugPS[mode == DebugVisualization::kSubmersion ? 1 : 0].get(), nullptr, 0);
			a_context->RSSetState(_debugRasterizer.get());
			const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(extent.width),
				static_cast<float>(extent.height), 0, 1 };
			a_context->RSSetViewports(1, &viewport);
			auto* target = _debugTexture->rtv.get();
			a_context->OMSetRenderTargets(1, &target, nullptr);
			const float clear[4]{ 0, 0, 0, 1 };
			a_context->ClearRenderTargetView(target, clear);
			cs::render::BindSharedData(a_context, cs::engine::ShaderStage::kPixel);
			auto* texture = _causticsSrv.get();
			auto* sampler = _causticsSampler.get();
			a_context->PSSetShaderResources(kCausticsPSSlot, 1, &texture);
			a_context->PSSetSamplers(kCausticsSamplerPSSlot, 1, &sampler);
			a_context->Draw(3, 0);
			_debugFrameReady = true;
			cs::render::InvalidateFullscreenDebugData();
			_debugFrames.fetch_add(1, std::memory_order_relaxed);
		} catch (const std::exception& e) {
			L->warn("Water debug rendering failed: {}", e.what());
		}
	}

	void WaterEffects::SetValidationDetail(std::string a_detail)
	{
		const std::lock_guard lock(_validationMutex);
		_validationDetail = std::move(a_detail);
	}

	std::string WaterEffects::GetValidationDetail() const
	{
		const std::lock_guard lock(_validationMutex);
		return _validationDetail;
	}

	bool WaterEffects::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the shader contribution did not register";
			SetValidationDetail(a_error);
			return false;
		}
		if (!_renderCallbacksReady.load(std::memory_order_acquire)) {
			a_error = "the deferred binding scopes did not install";
			SetValidationDetail(a_error);
			return false;
		}
		if (!_resourcesReady.load(std::memory_order_acquire)) {
			const auto detail = GetValidationDetail();
			a_error = detail.empty() ?
			              "the caustics texture is unavailable" :
			              "the caustics texture is unavailable: " + detail;
			SetValidationDetail(a_error);
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error =
				"the shared substrate is unavailable, so b5 carries no water grid";
			SetValidationDetail(a_error);
			return false;
		}

		if (!cs::engine::ValidateShaderInjectionRoutes(
				"WaterEffects", a_error)) {
			SetValidationDetail(a_error);
			return false;
		}

		SetValidationDetail({});
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	bool WaterEffects::CanBind() const noexcept
	{
		return _injectionsOperational.load(std::memory_order_acquire) && _enabled.load(std::memory_order_acquire) && _resourcesReady.load(std::memory_order_acquire) && _causticsSrv && _causticsSampler;
	}

	void WaterEffects::SaveEngineBindings()
	{
		auto* context = GetImmediateContext();
		if (!context)
			return;
		_engineBinding.Save(context, kCausticsPSSlot);
		_engineSamplerBinding.Save(context, kCausticsSamplerPSSlot);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(kCausticsPSSlot, 1, &nullSRV);
		ID3D11SamplerState* nullSampler = nullptr;
		context->PSSetSamplers(kCausticsSamplerPSSlot, 1, &nullSampler);
	}

	void WaterEffects::BindCaustics(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !CanBind())
			return;
		auto* srv = _causticsSrv.get();
		cs::engine::BindInjectionShaderResources(a_context, kCausticsPSSlot, 1, &srv);
		ID3D11SamplerState* sampler = _causticsSampler.get();
		cs::engine::BindInjectionSamplers(a_context, kCausticsSamplerPSSlot, 1, &sampler);
		_binds.fetch_add(1, std::memory_order_relaxed);
	}

	void WaterEffects::RestoreEngineBindings()
	{
		auto* context = GetImmediateContext();
		_engineSamplerBinding.Restore(context);
		_engineBinding.Restore(context);
	}

	FullscreenDebugData WaterEffects::GetFullscreenDebugData() const noexcept
	{
		if (!CanBind() || !_debugFrameReady)
			return {};
		return { .owner = FullscreenDebugOwner::WaterEffects,
			.mode = static_cast<std::uint32_t>(_debugVisualization.load(std::memory_order_acquire)),
			.texture = _debugTexture->srv.get() };
	}

	void WaterEffects::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto water = cs::engine::GetWaterDataStatus();
		const auto lightSnapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto detail = GetValidationDetail();
		a_sink
			.Field("configured_enabled", _enabled.load(std::memory_order_relaxed))
			.Field("has_water", water.cameraCellHeight != cs::engine::kNoWaterHeight)
			.Field(
				"water_height",
				static_cast<double>(water.cameraCellHeight))
			.Field("water_cells", static_cast<std::int64_t>(water.waterCells))
			.Field(
				"debug_mode",
				DebugVisualizationName(
					_debugVisualization.load(std::memory_order_relaxed)))
			.Field(
				"registrations_ready",
				_registrationsReady.load(std::memory_order_relaxed))
			.Field(
				"render_callbacks_ready",
				_renderCallbacksReady.load(std::memory_order_relaxed))
			.Field("resources_ready", _resourcesReady.load(std::memory_order_relaxed))
			.Field("debug_resources_ready", _debugResourcesReady.load(std::memory_order_relaxed))
			.Field("shared_data_ready", cs::render::IsSharedDataReady())
			.Field(
				"injection_operational",
				_injectionsOperational.load(std::memory_order_relaxed))
			.Field("injection_requested", lightSnapshot.requested)
			.Field("injection_published", lightSnapshot.published)
			.Field(
				"injection_publication_error",
				lightSnapshot.publicationError.empty() ?
					"none" :
					lightSnapshot.publicationError)
			.Field("injection_slot_collision", lightSnapshot.slotCollision)
			.Field(
				"caustics_binds",
				static_cast<std::int64_t>(_binds.load(std::memory_order_relaxed)))
			.Field("debug_frames", static_cast<std::int64_t>(_debugFrames.load(std::memory_order_relaxed)))
			.Field(
				"debug_depth_missing",
				static_cast<std::int64_t>(
					_debugDepthMissing.load(std::memory_order_relaxed)))
			.Field(
				"validation_detail",
				detail.empty() ? "operational" : detail);
	}

	void WaterEffects::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		if (edit.Discrete(dmui::ui::Checkbox("Enabled", &_settings.enabled))) {
			PublishSettings();
		}
		dmui::ui::TextDisabled(
			"Upstream ships no caustics tunables; every constant is fixed.");

		if (_injectionsOperational.load(std::memory_order_relaxed)) {
			const auto water = cs::engine::GetWaterDataStatus();
			if (water.cameraCellHeight != cs::engine::kNoWaterHeight) {
				dmui::ui::TextDisabled(
					"Cell water plane: z = %.1f",
					water.cameraCellHeight);
			} else {
				dmui::ui::TextDisabled("Current cell has no water plane.");
			}
		} else {
			const auto detail = GetValidationDetail();
			dmui::ui::TextDisabled(
				"Inactive: %s",
				detail.empty() ?
					"shader delivery path unavailable" :
					detail.c_str());
		}

		Menu::Get().DrawDebugViewSelector(*this);
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone };
			tooltip.Visible()) {
			dmui::ui::Text(
				"%s",
				"Caustics uses the production shader and sampler in an isolated pass. "
				"Submersion shows depth below each cell's water plane.");
		}
	}

	void WaterEffects::RestoreDefaultSettings()
	{
		_settings = Settings{};
		PublishSettings();
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(WaterEffects::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
