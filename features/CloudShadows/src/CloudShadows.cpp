#include "CloudShadows.h"

#include <DearModdingUI/Client.h>

#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/FrameBindings.h"
#include "Render/FrameProfiler.h"
#include "Render/FullscreenPass.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/SharedData.h"
#include "Render/SkyReflectionCube.h"
#include "Render/SubstrateSlots.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/Sky.h"

#include "RE/B/BSGeometry.h"
#include "RE/B/BSRenderPass.h"
#include "RE/C/Clouds.h"
#include "RE/N/NiNode.h"
#include "RE/S/Sun.h"
#include "RE/T/TESWeather.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.cloudshadows");

		constexpr const wchar_t* kPreviewPath = L"Data\\Shaders\\FO4\\CubemapPreviewCS.hlsl";
		constexpr const wchar_t* kOverlayPath = L"Data\\Shaders\\FO4\\CloudShadows\\DebugOverlay.hlsl";
		constexpr std::string_view kPreviewUnavailable = "Cloud occlusion preview not populated.";
		// RT0 color, RT1 motion; the capture cube takes the next free slot.
		constexpr std::uint32_t kCaptureTargetSlot = 2;
		constexpr std::uint32_t kTargetSlots = 8;
		// b4 to b7 are the substrate; the overlay's own constants follow.
		constexpr std::uint32_t kOverlayConstantsSlot = 8;
		constexpr std::uint32_t kLayerCubeSlot = 26;
		constexpr std::uint32_t kPaletteSize = 16;
		constexpr std::uint32_t kModeLayers = 3;
		constexpr std::array<float, 3> kSkirtColor{ 0.9f, 0.9f, 0.9f };

		// Adjacent slots land five hue steps apart, with alternating brightness.
		constexpr std::array<float, 3> PaletteColor(std::uint32_t a_slot)
		{
			const float hue = static_cast<float>((a_slot % kPaletteSize) * 5 % kPaletteSize) * 6.0f / kPaletteSize;
			const float value = (a_slot % 2) ? 0.7f : 1.0f;
			const auto channel = [&](float a_n) {
				float k = a_n + hue;
				if (k >= 6.0f)
					k -= 6.0f;
				float m = k < 4.0f - k ? k : 4.0f - k;
				m = m > 1.0f ? 1.0f : (m < 0.0f ? 0.0f : m);
				return value * (1.0f - m);
			};
			return { channel(5.0f), channel(3.0f), channel(1.0f) };
		}

		std::string ToString(const RE::BSFixedString& a_string)
		{
			return a_string.empty() ? std::string() : std::string(a_string.data(), a_string.size());
		}

		// Slot index of a cloud geometry in Clouds.nif order.
		std::optional<std::uint32_t> FindLayerSlot(const RE::BSGeometry* a_geometry) noexcept
		{
			const auto* sky = RE::Sky::GetSingleton();
			const auto* clouds = sky ? sky->clouds : nullptr;
			if (!clouds || !a_geometry)
				return std::nullopt;
			for (std::uint32_t slot = 0; slot < RE::Clouds::kMaxLayers; ++slot) {
				if (clouds->clouds[slot].get() == a_geometry)
					return slot;
			}
			return std::nullopt;
		}

		// An unmatched draw is gray.
		std::array<float, 3> LayerColor(const RE::BSRenderPass* a_pass) noexcept
		{
			const auto slot = a_pass ? FindLayerSlot(a_pass->geometry) : std::nullopt;
			return slot ? PaletteColor(*slot) : std::array<float, 3>{ 0.5f, 0.5f, 0.5f };
		}

		// Clouds.nif's FogRing layer is opaque fill below the horizon, not cloud.
		bool IsHorizonFill(const RE::BSGeometry* a_geometry) noexcept
		{
			const auto* sky = RE::Sky::GetSingleton();
			const auto* clouds = sky ? sky->clouds : nullptr;
			const auto* geometry = a_geometry;
			if (!clouds || !clouds->root || !geometry)
				return false;
			// A layer centered at or below the node origin is below the horizon.
			return geometry->worldBound.center.z <= clouds->root->world.translate.z;
		}
	}

	CloudShadows* CloudShadows::GetSingleton()
	{
		static CloudShadows instance;
		return &instance;
	}

	bool CloudShadows::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(cloud_shadows::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(cloud_shadows::kSchema, _settings);
		return true;
	}

	bool CloudShadows::SaveSettings()
	{
		return settings::SaveDelta(cloud_shadows::kSchema, GetConfigKey(), _settings, *L);
	}

	void CloudShadows::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("CloudShadows", *this, [this](cs::engine::ShaderReplacementRegistration& registration) {
				if (registration.targetId == cs::engine::ShaderInjectionTarget::kBsSky)
					registration.bind = [this](ID3D11DeviceContext* a_context) { BindSkyDraw(a_context); };
			})) {
			FailLoad("Cloud Shadows shader contribution registration failed.");
			return;
		}
		if (!cs::engine::RegisterSkyReflectionCubeConsumer(*this, "CloudShadows", [this] { return IsEnabled() && IsHealthy(); })) {
			FailLoad("Cloud Shadows needs the sky reflection cube");
			return;
		}
		if (!cs::engine::RegisterPostImagespace(*this, [this] { RenderOverlay(); }))
			L->warn("The fullscreen debug views need the post-imagespace hook.");
		_registrationsReady.store(true, std::memory_order_release);
	}

	bool CloudShadows::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the shader contribution or sky reflection cube did not register";
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so the receivers read no sun direction";
			return false;
		}
		if (!cs::engine::ValidateShaderInjectionRoutes("CloudShadows", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	void CloudShadows::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		D3D11_BLEND_DESC blendDesc{};
		auto& target = blendDesc.RenderTarget[0];
		target.BlendEnable = TRUE;
		target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		target.BlendOp = D3D11_BLEND_OP_ADD;
		target.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		DX::ThrowIfFailed(a_device->CreateBlendState(&blendDesc, _blend.put()));
		cs::render::annotation::SetName(_blend.get(), "CloudShadows/Blend");

		_preview.Create(a_device, kPreviewWidth, kPreviewHeight, "CloudShadows/Preview");
		_previewCS.attach(static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(kPreviewPath, { { "SINGLE_CHANNEL", "1" } }, "cs_5_0")));
		if (!_previewCS)
			L->warn("The occlusion preview shader failed to compile.");
		try {
			BuildOverlayResources(a_device);
		} catch (const std::exception& e) {
			L->warn("Fullscreen debug resources unavailable: {}", e.what());
		}
	}

	bool CloudShadows::EnsureCube(ID3D11DeviceContext* a_context)
	{
		auto* engineCube = cs::engine::GetSkyReflectionCubeTexture();
		auto* device = cs::engine::GetDevice();
		if (!engineCube || !device)
			return false;
		if (_cube && _engineCube == engineCube)
			return true;

		D3D11_TEXTURE2D_DESC desc{};
		engineCube->GetDesc(&desc);
		desc.Format = DXGI_FORMAT_R8_UNORM;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

		winrt::com_ptr<ID3D11Texture2D> cube;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, kFaceCount> faces;
		bool created = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, cube.put()));
		if (created) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube = { .MostDetailedMip = 0, .MipLevels = desc.MipLevels };
			created = SUCCEEDED(device->CreateShaderResourceView(cube.get(), &srvDesc, srv.put()));
		}
		for (std::uint32_t face = 0; created && face < kFaceCount; ++face) {
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = desc.Format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtvDesc.Texture2DArray = { .MipSlice = 0, .FirstArraySlice = face, .ArraySize = 1 };
			created = SUCCEEDED(device->CreateRenderTargetView(cube.get(), &rtvDesc, faces[face].put()));
		}
		if (!created) {
			if (!std::exchange(_resourceFailureLogged, true))
				L->error("Cloud occlusion cube creation failed ({}x{} R8_UNORM).", desc.Width, desc.Height);
			return false;
		}

		cs::render::annotation::SetName(cube.get(), "CloudShadows/Occlusion");
		cs::render::annotation::SetName(srv.get(), "CloudShadows/Occlusion.SRV");
		for (std::uint32_t face = 0; face < kFaceCount; ++face)
			cs::render::annotation::SetName(faces[face].get(), std::format("CloudShadows/Occlusion.RTV{}", face));
		_cube = std::move(cube);
		_cubeSRV = std::move(srv);
		_faceRTV = std::move(faces);
		_engineCube = engineCube;
		_clearedSerial = 0;
		// Faces the engine has not rendered yet read as unshadowed.
		ClearAllFaces(a_context);
		_counters.cubeSize.store(desc.Width, std::memory_order_relaxed);
		_counters.cubeMips.store(desc.MipLevels, std::memory_order_relaxed);
		L->info("occlusion cube: {0}x{0} R8_UNORM, {1} mips", desc.Width, desc.MipLevels);
		return true;
	}

	void CloudShadows::ClearFace(ID3D11DeviceContext* a_context, std::uint32_t a_face)
	{
		constexpr float kClear[4]{};
		a_context->ClearRenderTargetView(_faceRTV[a_face].get(), kClear);
	}

	void CloudShadows::ClearAllFaces(ID3D11DeviceContext* a_context)
	{
		for (std::uint32_t face = 0; face < kFaceCount; ++face)
			ClearFace(a_context, face);
	}

	void CloudShadows::ClearWithoutClouds(ID3D11DeviceContext* a_context)
	{
		const auto* sky = RE::Sky::GetSingleton();
		if (!sky || sky->mode.get() != RE::Sky::Mode::kFull || !sky->currentClimate)
			return;
		// Without cloud draws, the last weather's occlusion would stay.
		if (sky->clouds && sky->clouds->numLayers != 0) {
			_clearedWithoutClouds = false;
			return;
		}
		if (_clearedWithoutClouds || !_cube)
			return;
		cs::render::annotation::ScopedEvent timing("CloudShadows/ClearNoClouds");
		ClearAllFaces(a_context);
		_clearedWithoutClouds = true;
		_counters.noCloudClears.fetch_add(1, std::memory_order_relaxed);
	}

	void CloudShadows::BindSkyDraw(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !cs::engine::ActiveShaderInjectionVariantHasDefine(cs::engine::ShaderInjectionTarget::kBsSky, "CLOUDS"))
			return;
		const bool inCube = cs::engine::GetSkyReflectionCubeCapture().has_value();
		// The depth reject reads t17, so every cube cloud draw needs face depth.
		if (inCube) {
			if (auto* depth = cs::engine::GetDepthStencilDepthSRV(RE::BSGraphics::DepthStencilTargetID::kCubemapReflections))
				cs::engine::OverrideInjectionFrameShaderResource(a_context, cloud_shadows_shader::kCloudDepthSlot, depth);
		}
		const bool capturing = IsHealthy() && IsEnabled() && _injectionsOperational.load(std::memory_order_acquire);
		const auto* pass = cs::engine::GetCurrentSkyPass();
		const bool skirt = capturing && inCube && pass && IsHorizonFill(pass->geometry);
		if (skirt)
			_counters.horizonFillSkipped.fetch_add(1, std::memory_order_relaxed);
		// The layers view shows the skirt; occlusion never takes it.
		const bool showSkirt = skirt && _overlayMode.load(std::memory_order_acquire) == kModeLayers;
		if (capturing && (!skirt || showSkirt) && CaptureCloudDraw(a_context, skirt))
			return;
		UnbindExtraTarget(a_context);
	}

	bool CloudShadows::CaptureCloudDraw(ID3D11DeviceContext* a_context, bool a_skirt)
	{
		const auto capture = cs::engine::GetSkyReflectionCubeCapture();
		if (!capture)
			return false;
		if (!EnsureCube(a_context)) {
			_counters.skippedNoCube.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (!cs::engine::GetDepthStencilDepthSRV(RE::BSGraphics::DepthStencilTargetID::kCubemapReflections)) {
			_counters.skippedNoDepth.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		cs::render::annotation::SetMarker("CloudShadows/CloudDraw");
		// The face composite restarts at the first cloud draw of each face render.
		if (std::exchange(_clearedSerial, capture->serial) != capture->serial) {
			ClearFace(a_context, capture->face);
			_counters.facesCleared.fetch_add(1, std::memory_order_relaxed);
		}

		// The layers view adds a fourth output; with it off nothing extra is bound.
		const bool layers = _overlayMode.load(std::memory_order_acquire) == kModeLayers && EnsureLayerCube();
		if (layers) {
			if (std::exchange(_layerClearedSerial, capture->serial) != capture->serial) {
				constexpr float kClear[4]{};
				a_context->ClearRenderTargetView(_layerFaceRTV[capture->face].get(), kClear);
			}
			_counters.layerDraws.fetch_add(1, std::memory_order_relaxed);
		}

		// The draw scope restores the targets, blend state and depth slot.
		cs::engine::CaptureShaderInjectionRenderTargets(a_context);
		cs::engine::CaptureShaderInjectionBlendState(a_context);
		ID3D11RenderTargetView* targets[kCaptureTargetSlot + 2]{};
		a_context->OMGetRenderTargets(kCaptureTargetSlot, targets, nullptr);
		targets[kCaptureTargetSlot] = a_skirt ? nullptr : _faceRTV[capture->face].get();
		targets[kCaptureTargetSlot + 1] = layers ? _layerFaceRTV[capture->face].get() : nullptr;
		// FO4: the face depth is sampled, so it cannot stay bound.
		a_context->OMSetRenderTargets(layers ? kCaptureTargetSlot + 2 : kCaptureTargetSlot + 1, targets, nullptr);
		for (std::uint32_t slot = 0; slot < kCaptureTargetSlot; ++slot) {
			if (targets[slot])
				targets[slot]->Release();
		}
		// The layers cube takes its color from the blend factor.
		const auto layerColor = a_skirt ? kSkirtColor : LayerColor(cs::engine::GetCurrentSkyPass());
		const float blendFactor[4]{ layerColor[0], layerColor[1], layerColor[2], 1.0f };
		a_context->OMSetBlendState(layers ? _layerBlend.get() : _blend.get(), blendFactor, 0xFFFFFFFFu);
		_counters.cloudDraws.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void CloudShadows::UnbindExtraTarget(ID3D11DeviceContext* a_context)
	{
		// The cloud shader declares the capture outputs; no other draw takes them.
		ID3D11RenderTargetView* targets[kTargetSlots]{};
		ID3D11DepthStencilView* depth = nullptr;
		a_context->OMGetRenderTargets(kTargetSlots, targets, &depth);
		if (targets[kCaptureTargetSlot] || targets[kCaptureTargetSlot + 1]) {
			cs::engine::CaptureShaderInjectionRenderTargets(a_context);
			for (auto slot : { kCaptureTargetSlot, kCaptureTargetSlot + 1 }) {
				if (targets[slot])
					targets[slot]->Release();
				targets[slot] = nullptr;
			}
			a_context->OMSetRenderTargets(kTargetSlots, targets, depth);
			_counters.extraTargetsUnbound.fetch_add(1, std::memory_order_relaxed);
		}
		for (auto* target : targets) {
			if (target)
				target->Release();
		}
		if (depth)
			depth->Release();
		if (!cs::engine::GetSkyReflectionCubeCapture())
			_counters.worldCloudDraws.fetch_add(1, std::memory_order_relaxed);
	}

	void CloudShadows::Prepass()
	{
		auto* context = cs::engine::GetImmediateContext();
		if (!context)
			return;
		if (IsEnabled() && _injectionsOperational.load(std::memory_order_acquire) && EnsureCube(context))
			ClearWithoutClouds(context);
		// Disabled, the opacity is zero and the lookup multiplies by one.
		ID3D11ShaderResourceView* const cube = _cubeSRV.get();
		cs::engine::BindFrameShaderResources(context, cs::engine::ShaderStage::kPixel, cloud_shadows_shader::kCloudShadowsSlot, 1, &cube);
		_counters.framesBound.fetch_add(1, std::memory_order_relaxed);
		RenderDebugPreview(context);
	}

	void CloudShadows::BuildOverlayResources(ID3D11Device* a_device)
	{
		_overlayPass.Create(a_device);
		for (std::size_t index = 0; index < _overlayPS.size(); ++index) {
			std::vector<std::pair<const char*, const char*>> defines{ { "FO4CS_SUBSTRATE", "1" }, { "CLOUD_SHADOWS", "1" } };
			if (index == 0)
				defines.emplace_back("CLOUD_SKY_OVERLAY", "1");
			else if (index == 2)
				defines.emplace_back("CLOUD_LAYERS", "1");
			_overlayPS[index].attach(static_cast<ID3D11PixelShader*>(cs::util::CompileShader(kOverlayPath, defines, "ps_5_0")));
		}

		D3D11_BLEND_DESC blendDesc{};
		auto& target = blendDesc.RenderTarget[0];
		target.BlendEnable = TRUE;
		target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		target.BlendOp = D3D11_BLEND_OP_ADD;
		target.SrcBlendAlpha = D3D11_BLEND_ZERO;
		target.DestBlendAlpha = D3D11_BLEND_ONE;
		target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
		DX::ThrowIfFailed(a_device->CreateBlendState(&blendDesc, _overlayBlend.put()));

		D3D11_BUFFER_DESC constants{};
		constants.ByteWidth = sizeof(float) * 4;
		constants.Usage = D3D11_USAGE_DYNAMIC;
		constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		DX::ThrowIfFailed(a_device->CreateBuffer(&constants, nullptr, _overlayConstants.put()));
		_overlayReady.store(_overlayPass.Ready() && _overlayPS[0] && _overlayPS[1] && _overlayPS[2], std::memory_order_release);
	}

	bool CloudShadows::EnsureLayerCube()
	{
		if (_layerCube)
			return true;
		auto* device = cs::engine::GetDevice();
		if (!_cube || !device)
			return false;

		D3D11_TEXTURE2D_DESC desc{};
		_cube->GetDesc(&desc);
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.MipLevels = 1;

		winrt::com_ptr<ID3D11Texture2D> cube;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, kFaceCount> faces;
		bool created = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, cube.put()));
		if (created) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube = { .MostDetailedMip = 0, .MipLevels = 1 };
			created = SUCCEEDED(device->CreateShaderResourceView(cube.get(), &srvDesc, srv.put()));
		}
		for (std::uint32_t face = 0; created && face < kFaceCount; ++face) {
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = desc.Format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtvDesc.Texture2DArray = { .MipSlice = 0, .FirstArraySlice = face, .ArraySize = 1 };
			created = SUCCEEDED(device->CreateRenderTargetView(cube.get(), &rtvDesc, faces[face].put()));
		}

		// RT3 weights the blend-factor color by the broadcast coverage.
		D3D11_BLEND_DESC blendDesc{};
		blendDesc.IndependentBlendEnable = TRUE;
		for (std::uint32_t slot = 0; slot < 4; ++slot) {
			auto& target = blendDesc.RenderTarget[slot];
			target.BlendEnable = TRUE;
			target.SrcBlend = slot == 3 ? D3D11_BLEND_BLEND_FACTOR : D3D11_BLEND_SRC_ALPHA;
			target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			target.BlendOp = D3D11_BLEND_OP_ADD;
			target.SrcBlendAlpha = slot == 3 ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA;
			target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		winrt::com_ptr<ID3D11BlendState> blend;
		if (created)
			created = SUCCEEDED(device->CreateBlendState(&blendDesc, blend.put()));
		if (!created) {
			L->error("Cloud layer debug cube creation failed.");
			return false;
		}

		cs::render::annotation::SetName(cube.get(), "CloudShadows/Layers");
		cs::render::annotation::SetName(srv.get(), "CloudShadows/Layers.SRV");
		for (std::uint32_t face = 0; face < kFaceCount; ++face)
			cs::render::annotation::SetName(faces[face].get(), std::format("CloudShadows/Layers.RTV{}", face));
		_layerCube = std::move(cube);
		_layerCubeSRV = std::move(srv);
		_layerFaceRTV = std::move(faces);
		_layerBlend = std::move(blend);
		_layerClearedSerial = 0;
		L->info("layers debug cube: {0}x{0} R8G8B8A8_UNORM", desc.Width);
		return true;
	}

	void CloudShadows::RefreshLegend()
	{
		constexpr std::uint32_t kRefreshFrames = 30;
		const bool log = _logLegend.exchange(false, std::memory_order_acq_rel);
		if (!log && ++_legendFrame % kRefreshFrames != 0)
			return;
		const auto* sky = RE::Sky::GetSingleton();
		const auto* clouds = sky ? sky->clouds : nullptr;
		const auto* weather = sky ? sky->currentWeather : nullptr;
		std::vector<LegendRow> rows;
		for (std::uint32_t slot = 0; clouds && slot < RE::Clouds::kMaxLayers; ++slot) {
			const auto* geometry = clouds->clouds[slot].get();
			if (!geometry)
				continue;
			LegendRow row;
			row.slot = slot;
			row.name = ToString(geometry->GetName());
			row.texture = weather ? ToString(weather->cloudTexture[slot].textureName) : std::string();
			row.alpha = clouds->alphas[slot];
			row.skirt = IsHorizonFill(geometry);
			row.color = row.skirt ? kSkirtColor : PaletteColor(slot);
			rows.push_back(std::move(row));
		}
		if (log) {
			for (const auto& row : rows) {
				L->info("cloud layer {:2}: {} | texture '{}' | alpha {:.2f} | color ({:.2f}, {:.2f}, {:.2f}){}",
					row.slot, row.name, row.texture, row.alpha, row.color[0], row.color[1], row.color[2],
					row.skirt ? " | horizon skirt, not in the occlusion" : "");
			}
		}
		const std::lock_guard lock(_legendMutex);
		_legend = std::move(rows);
	}

	bool CloudShadows::HasDebugUI() const
	{
		return _overlayMode.load(std::memory_order_acquire) == kModeLayers;
	}

	void CloudShadows::DrawDebug()
	{
		std::vector<LegendRow> rows;
		{
			const std::lock_guard lock(_legendMutex);
			rows = _legend;
		}
		if (rows.empty()) {
			dmui::ui::Text("%s", "No cloud layers are loaded.");
			return;
		}
		dmui::ui::Text("%s", "Slot  Clouds.nif child | weather texture | alpha");
		for (const auto& row : rows) {
			dmui::ui::TextColored(dmui::ui::Vec4{ row.color[0], row.color[1], row.color[2], 1.0f },
				"%2u  %s | %s | %.2f%s", row.slot, row.name.c_str(), row.texture.c_str(), row.alpha,
				row.skirt ? " (horizon skirt, not in the occlusion)" : "");
		}
	}

	void CloudShadows::UpdateOverlayConstants(ID3D11DeviceContext* a_context)
	{
		float sun[4]{};
		sun[3] = cs::engine::TryGetSunDiscDirectionWS(sun[0], sun[1], sun[2]) ? 1.0f : 0.0f;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context->Map(_overlayConstants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return;
		std::memcpy(mapped.pData, sun, sizeof(sun));
		a_context->Unmap(_overlayConstants.get(), 0);
	}

	void CloudShadows::RenderOverlay()
	{
		const auto mode = _overlayMode.load(std::memory_order_acquire);
		if (mode == 0 || !_overlayReady.load(std::memory_order_acquire) || !_injectionsOperational.load(std::memory_order_acquire))
			return;
		auto* context = cs::engine::GetImmediateContext();
		const auto* graphics = cs::engine::GetGraphicsState();
		const auto camera = graphics ? cs::engine::GetCapturedWorldCameraRecord(graphics->frameCount) : std::nullopt;
		auto* frameBuffer = cs::engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		if (mode == kModeLayers)
			RefreshLegend();
		if (!context || !camera || !frameBuffer || !_cubeSRV || (mode == kModeLayers && !_layerCubeSRV)) {
			_counters.overlaySkipped.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		// The output depth matches the final image; the scene depth is render-sized.
		auto* depth = cs::render::UpdateOutputCanonicalDepth(context, *camera);
		winrt::com_ptr<ID3D11Resource> resource;
		frameBuffer->GetResource(resource.put());
		winrt::com_ptr<ID3D11Texture2D> texture;
		if (!depth || !resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.put())))) {
			_counters.overlaySkipped.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);

		UpdateOverlayConstants(context);

		const cs::render::FullscreenPass::Scope pass(_overlayPass, context, frameBuffer, desc.Width, desc.Height);
		if (!pass.IsActive())
			return;
		cs::render::annotation::ScopedEvent timing("CloudShadows/Overlay");
		constexpr float kBlendFactor[4]{};
		context->OMSetBlendState(_overlayBlend.get(), kBlendFactor, 0xFFFFFFFFu);
		cs::render::BindSharedData(context, cs::engine::ShaderStage::kPixel);
		ID3D11ShaderResourceView* const views[]{ depth };
		context->PSSetShaderResources(cloud_shadows_shader::kCloudDepthSlot, 1, views);
		ID3D11ShaderResourceView* const cube = _cubeSRV.get();
		context->PSSetShaderResources(cloud_shadows_shader::kCloudShadowsSlot, 1, &cube);
		ID3D11SamplerState* const sampler = cs::render::GetLinearClampSampler();
		context->PSSetSamplers(cs::render::kLinearClampSamplerSlot, 1, &sampler);
		ID3D11Buffer* const constants = _overlayConstants.get();
		context->PSSetConstantBuffers(kOverlayConstantsSlot, 1, &constants);
		if (mode == kModeLayers) {
			ID3D11ShaderResourceView* const layerCube = _layerCubeSRV.get();
			context->PSSetShaderResources(kLayerCubeSlot, 1, &layerCube);
		}
		pass.Draw(_overlayPS[mode - 1].get());
		_counters.overlayFrames.fetch_add(1, std::memory_order_relaxed);
	}

	void CloudShadows::RenderDebugPreview(ID3D11DeviceContext* a_context)
	{
		if (!_previewSelected.load(std::memory_order_acquire) || !_previewCS || !_cubeSRV)
			return;
		cs::render::annotation::ScopedEvent timing("CloudShadows/Preview");
		_preview.Convert(a_context, _previewCS.get(), _cubeSRV.get(), cs::render::GetLinearClampSampler());
	}

	void CloudShadows::WriteSharedData(cs::render::SharedFeatureDataCB& a_feature, cs::render::FO4SharedDataCB&)
	{
		a_feature.cloudShadowsSettings = { .Opacity = _settings.Opacity };
	}

	std::span<const FeatureDebugView> CloudShadows::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "cloud_occlusion",
				.label = "Cloud occlusion",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const CloudShadows&>(a_feature).GetOcclusionDebugTexture();
				} },
			FeatureDebugView{ .id = "cloud_sky_overlay", .label = "Cloud occlusion over the final image", .kind = FeatureDebugViewKind::kFullscreen }, FeatureDebugView{ .id = "cloud_shadow_term", .label = "Cloud shadow term", .kind = FeatureDebugViewKind::kFullscreen }, FeatureDebugView{ .id = "cloud_layers", .label = "Cloud layers by slot", .kind = FeatureDebugViewKind::kFullscreen }
		};
		return views;
	}

	void CloudShadows::SetDebugView(std::string_view a_view) noexcept
	{
		_preview.Invalidate();
		_previewSelected.store(a_view == "cloud_occlusion", std::memory_order_release);
		const std::uint32_t mode = a_view == "cloud_sky_overlay" ? 1u : a_view == "cloud_shadow_term" ? 2u :
		                                                            a_view == "cloud_layers"          ? kModeLayers :
		                                                                                                0u;
		if (mode == kModeLayers)
			_logLegend.store(true, std::memory_order_release);
		_overlayMode.store(mode, std::memory_order_release);
	}

	FeatureDebugTexture CloudShadows::GetOcclusionDebugTexture() const
	{
		if (!_previewSelected.load(std::memory_order_acquire))
			return { .unavailableText = kPreviewUnavailable };
		return _preview.GetDebugTexture(kPreviewUnavailable);
	}

	void CloudShadows::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto range = cloud_shadows::kSchema.EditRange(&Settings::Opacity);
		edit.Continuous(dmui::ui::SliderScalar("Opacity##Opacity", &_settings.Opacity, &range.min, &range.max, "%.1f"));
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone }; tooltip.Visible())
			dmui::ui::Text("%s", "Higher values make cloud shadows darker.");
	}

	void CloudShadows::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	void CloudShadows::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto count = [](const std::atomic<std::uint64_t>& a_value) { return a_value.load(std::memory_order_relaxed); };
		const auto size = _counters.cubeSize.load(std::memory_order_relaxed);
		a_sink
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("cloud_draws_captured", count(_counters.cloudDraws))
			.Field("faces_cleared", count(_counters.facesCleared))
			.Field("no_cloud_clears", count(_counters.noCloudClears))
			.Field("frames_bound", count(_counters.framesBound))
			.Field("world_cloud_draws", count(_counters.worldCloudDraws))
			.Field("extra_targets_unbound", count(_counters.extraTargetsUnbound))
			.Field("skipped_no_cube", count(_counters.skippedNoCube))
			.Field("skipped_no_depth", count(_counters.skippedNoDepth))
			.Field("horizon_fill_skipped", count(_counters.horizonFillSkipped))
			.Field("overlay_frames", count(_counters.overlayFrames))
			.Field("overlay_skipped", count(_counters.overlaySkipped))
			.Field("layer_draws", count(_counters.layerDraws))
			.Dimensions("cube", size, size)
			.Field("cube_mips", _counters.cubeMips.load(std::memory_order_relaxed));
		cs::render::profiling::CollectPassTimings(a_sink, "CloudShadows/");
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(CloudShadows::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
