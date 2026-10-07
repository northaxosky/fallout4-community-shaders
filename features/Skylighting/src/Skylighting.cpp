#include "Skylighting.h"

#include <DearModdingUI/Client.h>
#include <d3d11.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/FrameBuffer.h"
#include "Render/FrameProfiler.h"
#include "Render/PrecipitationOcclusion.h"
#include "Render/RenderExtents.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "Render/ShaderInjection.h"
#include "Render/ShadowCascades.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/Sky.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting");
		auto* CaptureLog = cs::log::Get("cs.feature.skylighting.capture");

		// Same square as the stock precipitation occlusion depth and color targets.
		constexpr std::uint32_t kOcclusionResolution = 512;
		constexpr std::uint64_t kSummaryIntervalFrames = 300;
		constexpr std::string_view kProbeUpdatePass = "Skylighting/ProbeUpdate";

		constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;

		constexpr std::array kCascadeSkipNames{ "none", "unavailable", "not_full_sky", "no_light", "unsupported_count", "no_target", "invalid", "copy_target", "disabled" };

		// Directional families whose soft sun lobes read the probe shadow visibility.
		constexpr std::array kShadowVisFamilies{ "BSDFLIGHT_PS_DIRSPLITS1", "BSDFLIGHT_PS_DIRSPLITS2", "BSDFLIGHT_PS_DIRSPLITS3", "BSDFLIGHT_PS_UNSHADOWED" };

		bool ActiveLightReadsShadowVisibility() noexcept
		{
			const auto* defines = cs::engine::GetActiveShaderInjectionVariantDefines(cs::engine::ShaderInjectionTarget::kBsdfLight);
			if (!defines || !defines->contains("DIRECTIONAL"))
				return false;
			return std::ranges::any_of(kShadowVisFamilies, [&](const char* a_family) { return defines->contains(a_family); });
		}

		struct ProbeFormat
		{
			DXGI_FORMAT format;
			const char* name;
		};
		constexpr std::array kProbeFormats{
			ProbeFormat{ DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT" },
			ProbeFormat{ DXGI_FORMAT_R8_UINT, "R8_UINT" },
			ProbeFormat{ DXGI_FORMAT_R8_UNORM, "R8_UNORM" }
		};
	}

	Skylighting* Skylighting::GetSingleton()
	{
		static Skylighting instance;
		return &instance;
	}

	bool Skylighting::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(skylighting::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(skylighting::kSchema, _settings);
		return true;
	}

	bool Skylighting::SaveSettings()
	{
		return settings::SaveDelta(skylighting::kSchema, GetConfigKey(), _settings, *L);
	}

	void Skylighting::Load()
	{
		bool installed = true;
		for (const auto& hook : cs::engine::InstallOcclusionCaptureHooks()) {
			CaptureLog->info("hook {}: {} ({})", hook.name, hook.installed ? "installed" : "unavailable", hook.detail);
			installed = installed && hook.installed;
		}
		const bool registered = cs::engine::RegisterPostPrecipitationOcclusion([this] { RenderOcclusion(); });
		CaptureLog->info("hook Precipitation::RenderOcclusionMap detour: {}", registered ? "installed" : "unavailable");
		if (!installed || !registered) {
			FailLoad("the precipitation occlusion capture hooks could not be installed");
			return;
		}

		if (!cs::engine::RegisterFeatureShaderBindings("Skylighting", *this, [this](cs::engine::ShaderReplacementRegistration& registration) {
				if (const auto consumer = ConsumerFor(registration.targetId))
					registration.bind = [this, consumer = *consumer](ID3D11DeviceContext* a_context) { BindConsumer(a_context, consumer); };
			})) {
			FailLoad("Skylighting shader contribution registration failed.");
			return;
		}
		// FO4: the stock array holds the sun cascades until focus shadows reuse it.
		if (!cs::engine::RegisterPostSunShadowRender([this] { CopySunCascades(); })) {
			FailLoad("Skylighting shadow history needs the main sun shadow render hook");
			return;
		}
		if (!cs::engine::RegisterPreDeferredComposite(
				[this] {
					if (auto* context = cs::engine::GetImmediateContext())
						RenderDebug(context);
				},
				cs::engine::HookPriority::Early)) {
			FailLoad("Skylighting debug views need a deferred-composite producer");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	bool Skylighting::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the shader contribution did not register";
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b6 carries no probe grid";
			return false;
		}
		// Raster and tiled ambient vary per frame; every route must publish.
		if (!cs::engine::ValidateShaderInjectionRoutes("Skylighting", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	ID3D11ShaderResourceView* Skylighting::GetProbeArraySRV() const noexcept
	{
		return _settings.enabled ? GetBindableProbeArraySRV() : nullptr;
	}

	ID3D11ShaderResourceView* Skylighting::GetBindableProbeArraySRV() const noexcept
	{
		if (!IsHealthy() || !_probesReady.load(std::memory_order_acquire) || !_injectionsOperational.load(std::memory_order_acquire) || !_texProbeArray)
			return nullptr;
		return _texProbeArray->srv.get();
	}

	std::optional<Skylighting::Consumer> Skylighting::ConsumerFor(cs::engine::ShaderInjectionTarget a_target) noexcept
	{
		using Target = cs::engine::ShaderInjectionTarget;
		switch (a_target) {
		case Target::kBsWater:
			return Consumer::kWater;
		case Target::kBsdfComposite:
			return Consumer::kComposite;
		case Target::kBsdfLight:
			return Consumer::kLight;
		case Target::kDfTiledLighting:
			return Consumer::kTiled;
		default:
			return std::nullopt;
		}
	}

	void Skylighting::BindConsumer(ID3D11DeviceContext* a_context, Consumer a_consumer)
	{
		// Disabled keeps the bindings: the neutral b6 block never reads them.
		auto* probes = GetBindableProbeArraySRV();
		if (!a_context || !probes)
			return;
		const bool lighting = a_consumer == Consumer::kLight || a_consumer == Consumer::kTiled;
		// Only the raster sun reads shadow visibility; tiled has no directional lobes.
		const bool shadowVisibility = a_consumer == Consumer::kLight;
		const std::array<ID3D11ShaderResourceView*, 4> views{
			probes,
			lighting ? cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferAlbedo) : nullptr,
			lighting ? cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferEmissive) : nullptr,
			shadowVisibility ? _texShadowVisibility->srv.get() : nullptr
		};
		cs::engine::BindInjectionShaderResources(a_context, kProbeArraySlot, shadowVisibility ? 4 : lighting ? 3 :
																											   1,
			views.data());
		if (shadowVisibility && ActiveLightReadsShadowVisibility())
			_probeCounters.shadowVisDraws.fetch_add(1, std::memory_order_relaxed);
		auto& counter = a_consumer == Consumer::kWater     ? _probeCounters.waterDraws :
		                a_consumer == Consumer::kComposite ? _probeCounters.compositeDraws :
		                a_consumer == Consumer::kLight     ? _probeCounters.lightDraws :
		                                                     _probeCounters.tiledDispatches;
		counter.fetch_add(1, std::memory_order_relaxed);
	}

	void Skylighting::OnLoadingMenuClosed()
	{
		QueueReset(kResetLoad);
	}

	void Skylighting::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		// Typed UAV loads are optional in D3D11 and the probe update needs them.
		for (const auto& [format, name] : kProbeFormats) {
			D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{ format, 0 };
			if (FAILED(a_device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support))) ||
				!(support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) {
				throw std::runtime_error(std::format("the device lacks typed UAV loads for {}", name));
			}
		}
		CreateOcclusionResources(a_device);
		CreateProbeResources(a_device);
		CreateDebugResources();
	}

	void Skylighting::CreateProbeResources(ID3D11Device* a_device)
	{
		D3D11_TEXTURE3D_DESC texDesc{
			.Width = kProbeArrayDims[0],
			.Height = kProbeArrayDims[1],
			.Depth = kProbeArrayDims[2],
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		const auto create = [&](DXGI_FORMAT a_format, std::string_view a_name) {
			texDesc.Format = a_format;
			auto texture = std::make_unique<cs::buffer::Texture3D>(a_device, texDesc, true);
			texture->SetName(a_name);
			return texture;
		};
		_texProbeArray = create(DXGI_FORMAT_R16G16B16A16_FLOAT, "Skylighting/ProbeArray");
		_texAccumFramesArray = create(DXGI_FORMAT_R8_UINT, "Skylighting/AccumFramesArray");
		_texShadowBitmask = create(DXGI_FORMAT_R32_UINT, "Skylighting/ShadowBitmask");
		_texShadowVisibility = create(DXGI_FORMAT_R8_UNORM, "Skylighting/ShadowVisibility");

		{
			D3D11_SAMPLER_DESC samplerDesc = {};
			samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;  // Use comparison filtering
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;               // Address mode (Clamp for shadow maps)
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;  // Comparison function
			samplerDesc.MinLOD = 0;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(a_device->CreateSamplerState(&samplerDesc, _comparisonSampler.put()));
			cs::render::annotation::SetName(_comparisonSampler.get(), "Skylighting/ComparisonSampler");
		}

		{
			const D3D11_BUFFER_DESC bufferDesc{
				.ByteWidth = sizeof(skylighting::DirectionalShadowLightData),
				.Usage = D3D11_USAGE_DYNAMIC,
				.BindFlags = D3D11_BIND_SHADER_RESOURCE,
				.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
				.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
				.StructureByteStride = sizeof(skylighting::DirectionalShadowLightData)
			};
			const skylighting::DirectionalShadowLightData zeroed{};
			const D3D11_SUBRESOURCE_DATA initial{ .pSysMem = &zeroed };
			DX::ThrowIfFailed(a_device->CreateBuffer(&bufferDesc, &initial, _shadowLightsBuffer.put()));
			const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
				.Format = DXGI_FORMAT_UNKNOWN,
				.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
				.Buffer = { .FirstElement = 0, .NumElements = 1 }
			};
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(_shadowLightsBuffer.get(), &srvDesc, _shadowLightsSRV.put()));
			cs::render::annotation::SetName(_shadowLightsBuffer.get(), "Skylighting/ShadowLights");
			cs::render::annotation::SetName(_shadowLightsSRV.get(), "Skylighting/ShadowLights.SRV");
		}

		// FO4: the substrate defines the CS camera and shared-data inputs.
		_probeUpdateCompute.attach(static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(
			L"Data\\Shaders\\Skylighting\\UpdateProbesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" } }, "cs_5_0")));
		if (!_probeUpdateCompute)
			throw std::runtime_error("the probe update compute shader failed to compile");
		cs::render::annotation::SetName(_probeUpdateCompute.get(), "Skylighting/UpdateProbes.CS");

		winrt::com_ptr<ID3D11DeviceContext> context;
		a_device->GetImmediateContext(context.put());
		ResetSkylighting(context.get());
		_probesReady.store(true, std::memory_order_release);
		CaptureLog->info("probe arrays: {}x{}x{} R16G16B16A16_FLOAT, R8_UINT, R32_UINT, R8_UNORM",
			kProbeArrayDims[0], kProbeArrayDims[1], kProbeArrayDims[2]);
	}

	void Skylighting::CreateDebugResources()
	{
		for (std::size_t index = 0; index < _debugCompute.size(); ++index) {
			std::vector<std::pair<const char*, const char*>> defines{ { "FO4CS_SUBSTRATE", "1" } };
			if (index == 1)
				defines.emplace_back("SKYLIGHTING_UP_VISIBILITY", "1");
			_debugCompute[index].attach(static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(
				L"Data\\Shaders\\FO4\\Skylighting\\DebugCS.hlsl", defines, "cs_5_0")));
			if (!_debugCompute[index])
				L->warn("Skylighting debug shader {} failed to compile; the fullscreen views are unavailable.", index);
			else
				cs::render::annotation::SetName(_debugCompute[index].get(), index ? "Skylighting/DebugUp.CS" : "Skylighting/DebugDiffuse.CS");
		}
	}

	void Skylighting::ResetSkylighting(ID3D11DeviceContext* a_context)
	{
		// Unit SH (fully unoccluded), matching Skylighting::UNIT_SH; probes the occlusion map does not reach would otherwise keep the previous location's values
		const float unitSH[4] = { std::sqrt(4.0f * std::numbers::pi_v<float>), 0.0f, 0.0f, 0.0f };
		a_context->ClearUnorderedAccessViewFloat(_texProbeArray->uav.get(), unitSH);

		// ClearUnorderedAccessViewUint always reads four values
		const UINT clr[4] = { 0, 0, 0, 0 };
		a_context->ClearUnorderedAccessViewUint(_texAccumFramesArray->uav.get(), clr);
		// All 32 history bits lit, so a reset does not fade in from black while the history refills
		const UINT litHistory[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
		a_context->ClearUnorderedAccessViewUint(_texShadowBitmask->uav.get(), litHistory);

		float clrf[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		a_context->ClearUnorderedAccessViewFloat(_texShadowVisibility->uav.get(), clrf);

		// Grid bottom is stale until the next in-world buffer update, so don't cull this frame
		probeGridBottomZ = -FLT_MAX;
		// FO4: cascades from the previous location must not feed the fresh history.
		RetireCascades(a_context);
	}

	void Skylighting::CreateOcclusionResources(ID3D11Device* a_device)
	{
		const D3D11_TEXTURE2D_DESC textureDesc{
			.Width = kOcclusionResolution,
			.Height = kOcclusionResolution,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16_TYPELESS,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
		};
		const D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{
			.Format = DXGI_FORMAT_D16_UNORM,
			.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
		const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format = DXGI_FORMAT_R16_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		DX::ThrowIfFailed(a_device->CreateTexture2D(&textureDesc, nullptr, _occlusionTexture.put()));
		DX::ThrowIfFailed(a_device->CreateDepthStencilView(_occlusionTexture.get(), &dsvDesc, _occlusionDSV.put()));
		DX::ThrowIfFailed(a_device->CreateShaderResourceView(_occlusionTexture.get(), &srvDesc, _occlusionSRV.put()));
		cs::render::annotation::SetName(_occlusionTexture.get(), "Skylighting/Occlusion");
		cs::render::annotation::SetName(_occlusionDSV.get(), "Skylighting/Occlusion.DSV");
		cs::render::annotation::SetName(_occlusionSRV.get(), "Skylighting/Occlusion.SRV");
		CaptureLog->info("occlusion depth target: {}x{} R16_TYPELESS, DSV D16_UNORM, SRV R16_UNORM", kOcclusionResolution, kOcclusionResolution);
	}

	std::span<const FeatureDebugView> Skylighting::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "occlusion_depth",
				.label = "Occlusion depth",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const Skylighting&>(a_feature).GetOcclusionDebugTexture();
				} },
			FeatureDebugView{ .id = "probe_diffuse_visibility", .label = "Probe diffuse visibility", .kind = FeatureDebugViewKind::kFullscreen }, FeatureDebugView{ .id = "probe_up_visibility", .label = "Probe up visibility", .kind = FeatureDebugViewKind::kFullscreen }
		};
		return views;
	}

	void Skylighting::SetDebugView(std::string_view a_view) noexcept
	{
		_debugPreviewEnabled.store(a_view == "occlusion_depth", std::memory_order_release);
		_debugVisualization.store(
			a_view == "probe_diffuse_visibility" ? DebugVisualization::kDiffuse :
			a_view == "probe_up_visibility"      ? DebugVisualization::kUp :
												   DebugVisualization::kOff,
			std::memory_order_release);
	}

	FullscreenDebugData Skylighting::GetFullscreenDebugData() const noexcept
	{
		if (!_debugFrameReady || !_debugTexture)
			return {};
		return { .owner = FullscreenDebugOwner::Skylighting,
			.mode = static_cast<std::uint32_t>(_debugVisualization.load(std::memory_order_acquire)),
			.texture = _debugTexture->srv.get() };
	}

	void Skylighting::RenderDebug(ID3D11DeviceContext* a_context)
	{
		_debugFrameReady = false;
		cs::render::InvalidateFullscreenDebugData();
		const auto mode = _debugVisualization.load(std::memory_order_acquire);
		if (mode == DebugVisualization::kOff || !_settings.enabled || !_injectionsOperational.load(std::memory_order_acquire) || !_probesReady.load(std::memory_order_acquire))
			return;
		auto* shader = _debugCompute[mode == DebugVisualization::kUp ? 1 : 0].get();
		auto* normals = cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
		const auto* graphics = cs::engine::GetGraphicsState();
		if (!shader || !normals || !graphics || !cs::render::IsSharedDataReady())
			return;
		const auto extent = cs::render::GetActiveExtent(graphics->screenWidth, graphics->screenHeight);
		if (!extent.width || !extent.height)
			return;
		try {
			if (!_debugTexture || _debugTexture->desc.Width != graphics->screenWidth || _debugTexture->desc.Height != graphics->screenHeight) {
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = graphics->screenWidth;
				desc.Height = graphics->screenHeight;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				desc.SampleDesc.Count = 1;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				auto texture = std::make_unique<cs::buffer::Texture2D>(desc);
				D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Format = desc.Format;
				srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srv.Texture2D.MipLevels = 1;
				texture->CreateSRV(srv);
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.Format = desc.Format;
				uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				texture->CreateUAV(uav);
				texture->SetName("Skylighting/Debug.Texture", "Skylighting/Debug.SRV", "Skylighting/Debug.UAV");
				_debugTexture = std::move(texture);
			}
			cs::engine::ComputeOMScope scope(a_context, 2, 0, 1, 0);
			cs::render::ScopedComputeSharedDataBinding substrate(a_context);
			cs::render::annotation::ScopedEvent event("Skylighting/Debug", false);
			ID3D11ShaderResourceView* srvs[]{ normals, _texProbeArray->srv.get() };
			ID3D11UnorderedAccessView* uavs[]{ _debugTexture->uav.get() };
			a_context->CSSetShaderResources(0, 2, srvs);
			a_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			a_context->CSSetShader(shader, nullptr, 0);
			a_context->Dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);
			_debugFrameReady = true;
			cs::render::InvalidateFullscreenDebugData();
			_probeCounters.debugFrames.fetch_add(1, std::memory_order_relaxed);
		} catch (const std::exception& e) {
			L->warn("Skylighting debug rendering failed: {}", e.what());
		}
	}

	FeatureDebugTexture Skylighting::GetOcclusionDebugTexture() const
	{
		FeatureDebugTexture texture{ .unavailableText = "Occlusion depth not allocated." };
		if (!_debugPreviewEnabled.load(std::memory_order_acquire) || !_occlusionSRV)
			return texture;
		texture.texture = _occlusionSRV.get();
		texture.width = kOcclusionResolution;
		texture.height = kOcclusionResolution;
		return texture;
	}

	void Skylighting::RenderOcclusion()
	{
		const auto state = CaptureFrame();
		// Keyed on anchor frames so skipped frames report; state changes log at once.
		if (++_anchorFrames == 1 || _anchorFrames % kSummaryIntervalFrames == 0 || state != _loggedState || std::exchange(_summaryPending, false))
			LogCaptureSummary(state);
	}

	Skylighting::CaptureState Skylighting::CaptureFrame()
	{
		if (!IsHealthy() || !_occlusionDSV || !_probesReady.load(std::memory_order_acquire)) {
			_counters.skippedDisabled.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kDisabled;
		}
		if (!_settings.enabled) {
			_wasEnabled = false;
			_counters.skippedDisabled.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kDisabled;
		}
		// Re-enabling must not show history from before the disabled stretch.
		if (!std::exchange(_wasEnabled, true))
			QueueReset(kResetEnable);
		if (cs::engine::IsInterior()) {
			_counters.skippedInterior.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kInterior;
		}

		if (const auto reasons = _queuedReset.exchange(0, std::memory_order_acq_rel)) {
			ResetSkylighting(cs::engine::GetImmediateContext());
			_probeCounters.resets.fetch_add(1, std::memory_order_relaxed);
			if (reasons & kResetLoad)
				_probeCounters.resetsLoad.fetch_add(1, std::memory_order_relaxed);
			if (reasons & kResetRebuild)
				_probeCounters.resetsRebuild.fetch_add(1, std::memory_order_relaxed);
			if (reasons & kResetEnable)
				_probeCounters.resetsEnable.fetch_add(1, std::memory_order_relaxed);
			// FO4: capture can fail at load; the update waits for a fresh matrix.
			_hasOcclusion = false;
			_summaryPending = true;
		}

		const auto start = std::chrono::steady_clock::now();

		frameCount++;

		DirectX::XMFLOAT2 vPoint;
		{
			constexpr float rcpRandMax = 1.f / RAND_MAX;
			static int randSeed = std::rand();
			static std::uint32_t randFrameCount = 0;

			// r2 sequence
			vPoint = { randSeed * rcpRandMax + (float)randFrameCount * 0.245122333753f, randSeed * rcpRandMax + (float)randFrameCount * 0.430159709002f };
			vPoint.x -= static_cast<unsigned long long>(vPoint.x);
			vPoint.y -= static_cast<unsigned long long>(vPoint.y);

			randFrameCount++;
			if (randFrameCount == 1000) {
				randFrameCount = 0;
				randSeed = std::rand();
			}

			// disc transformation
			vPoint.x = sqrt(vPoint.x) * sin(_settings.MaxZenith);
			vPoint.y *= 6.28318530718f;

			vPoint = { vPoint.x * cos(vPoint.y), vPoint.x * sin(vPoint.y) };
		}

		DirectX::XMFLOAT3 PrecipitationShaderDirectionF{ -vPoint.x, -vPoint.y, -sqrt(1 - (vPoint.x * vPoint.x + vPoint.y * vPoint.y)) };
		DirectX::XMStoreFloat3(&PrecipitationShaderDirectionF, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&PrecipitationShaderDirectionF)));
		// Rounding past the unit disc would hand the engine a NaN direction.
		if (!std::isfinite(PrecipitationShaderDirectionF.x + PrecipitationShaderDirectionF.y + PrecipitationShaderDirectionF.z)) {
			_counters.failed.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kFailed;
		}

		// FO4: the adapter swaps DS8 and the globals, projects twice, restores.
		const cs::engine::OcclusionCaptureRequest request{
			.boxSize = occlusionDistance,
			.travelDirection = PrecipitationShaderDirectionF,
			.quadrant = frameCount % 4,
			.occluders = {
				.minOccluderRadius = MIN_OCCLUDER_RADIUS,
				.belowGridMargin = OCCLUSION_BELOW_GRID_MARGIN,
				.probeGridBottomZ = probeGridBottomZ },
			.target = { _occlusionTexture.get(), _occlusionDSV.get(), _occlusionSRV.get() }
		};
		cs::engine::OcclusionCaptureResult result;
		const auto status = cs::engine::CaptureOcclusion(request, result);

		if (!std::exchange(_loggedFirstCapture, true))
			CaptureLog->info("stock targets at first capture: {}", cs::engine::DescribeStockOcclusionTargets());
		if (status != cs::engine::OcclusionCaptureStatus::kCaptured) {
			_counters.skippedTargets.fetch_add(1, std::memory_order_relaxed);
			if (!std::exchange(_loggedTargetFailure, true)) {
				CaptureLog->warn("capture skipped: {}", status == cs::engine::OcclusionCaptureStatus::kNoPrecipitation ? "the sky has no precipitation occlusion camera yet" : "a stock occlusion target is not 512x512");
			}
			return CaptureState::kTargets;
		}

		OcclusionDir = { -PrecipitationShaderDirectionF.x, -PrecipitationShaderDirectionF.y, -PrecipitationShaderDirectionF.z, 0 };
		OcclusionTransform = result.matrix;
		_hasOcclusion = true;

		_counters.captures.fetch_add(1, std::memory_order_relaxed);
		_counters.stockTargetRestored.store(result.stockTargetRestored, std::memory_order_relaxed);
		const auto cpuMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		_windowCpuMsSum += cpuMs;
		_windowCpuMsMax = std::max(_windowCpuMsMax, cpuMs);
		++_windowCaptures;
		return CaptureState::kCaptured;
	}

	// Render thread only; one line per window keeps the capture log cheap.
	void Skylighting::LogCaptureSummary(CaptureState a_state)
	{
		_loggedState = a_state;
		const auto& a_matrix = OcclusionTransform;
		constexpr std::array stateNames{ "captured", "interior", "disabled", "targets", "failed" };
		const auto stateName = stateNames[static_cast<std::size_t>(a_state)];
		const float cpuMsAverage = _windowCaptures ? static_cast<float>(_windowCpuMsSum / _windowCaptures) : 0.0f;
		_counters.cpuMsAverage.store(cpuMsAverage, std::memory_order_relaxed);
		_counters.cpuMsMax.store(_windowCpuMsMax, std::memory_order_relaxed);

		// A row's length is the NDC scale, so it recovers the box extent.
		const auto rowLength = [&](std::size_t a_row) {
			return std::sqrt(a_matrix.m[a_row][0] * a_matrix.m[a_row][0] + a_matrix.m[a_row][1] * a_matrix.m[a_row][1] + a_matrix.m[a_row][2] * a_matrix.m[a_row][2]);
		};
		// Zero until the first capture publishes a matrix.
		const auto extent = [&](float a_range, std::size_t a_row) {
			const float length = rowLength(a_row);
			return length > 0.0f ? a_range / length / occlusionDistance : 0.0f;
		};
		const float extentX = extent(2.0f, 0);
		const float extentY = extent(2.0f, 1);
		const float depthRange = extent(1.0f, 2);

		const auto& stats = cs::engine::GetOccluderStats();
		const auto count = [](const std::atomic<std::uint64_t>& a_value) { return a_value.load(std::memory_order_relaxed); };
		using Reject = cs::engine::OccluderReject;
		const auto rejected = [&](Reject a_reason) { return count(stats.rejected[static_cast<std::size_t>(a_reason)]); };
		using Own = cs::engine::OwnBuildReason;
		const auto own = [&](Own a_reason) { return count(stats.ownBuilt[static_cast<std::size_t>(a_reason)]); };
		const auto ownTotal = own(Own::kLandscape) + own(Own::kNotCasting) + own(Own::kAlphaBlended) + own(Own::kOther);

		// Per-capture window milliseconds; the hook parts run inside the capture.
		const auto& timings = cs::engine::GetCaptureTimings();
		const std::array<cs::engine::CaptureClock::rep, 4> ticks{
			timings.capture.load(std::memory_order_relaxed), timings.hookPredicate.load(std::memory_order_relaxed),
			timings.hookStock.load(std::memory_order_relaxed), timings.hookOwn.load(std::memory_order_relaxed)
		};
		std::array<float, 4> stageMs{};
		for (std::size_t i = 0; i < stageMs.size(); ++i) {
			stageMs[i] = _windowCaptures ? static_cast<float>(cs::engine::TicksToMs(ticks[i] - _timingSnapshot[i]) / _windowCaptures) : 0.0f;
			_counters.stageMs[i].store(stageMs[i], std::memory_order_relaxed);
		}
		_timingSnapshot = ticks;

		const auto cascadeSkipped = [&](CascadeSkip a_reason) { return count(_cascadeCounters.skipped[static_cast<std::size_t>(a_reason)]); };
		const auto probeStateNames = std::array{ "pending", "dispatched", "not_full_sky", "no_occlusion", "no_grid", "disabled" };
		const auto cellID = _grid.cellID;
		const auto& block = _grid.block;
		const std::array<std::int32_t, 9> grid{
			static_cast<std::int32_t>(cellID.x), static_cast<std::int32_t>(cellID.y), static_cast<std::int32_t>(cellID.z),
			static_cast<std::int32_t>(block.ArrayOrigin[0]), static_cast<std::int32_t>(block.ArrayOrigin[1]), static_cast<std::int32_t>(block.ArrayOrigin[2]),
			block.ValidMargin[0], block.ValidMargin[1], block.ValidMargin[2]
		};
		for (std::size_t i = 0; i < grid.size(); ++i)
			_probeCounters.grid[i].store(grid[i], std::memory_order_relaxed);
		for (const auto& result : cs::render::profiling::GetProfiler().GetResults()) {
			if (result.valid && result.name == kProbeUpdatePass)
				_probeCounters.gpuMs.store(result.gpuTimeMs, std::memory_order_relaxed);
		}
		CaptureLog->info(
			"summary enabled={} state={} anchor_frames={} frame={} captures={} skipped_interior={} skipped_disabled={} skipped_targets={} failed={} "
			"L={:.0f} dir=({:.3f},{:.3f},{:.3f}) quadrant={} "
			"accepted={} delegated={} own_built={} own_new={} own_landscape={} own_not_casting={} own_alpha_blended={} own_other={} stock_only={} "
			"rej_skinned={} rej_flags={} rej_radius={} rej_below_grid={} rej_bsx={} "
			"cpu_ms_avg={:.3f} cpu_ms_max={:.3f} ms_capture={:.3f} ms_hook_predicate={:.3f} ms_hook_stock={:.3f} ms_hook_own={:.3f} stock_ds8_restored={} "
			"mx_ext_x={:.3f} mx_ext_y={:.3f} mx_depth={:.3f} "
			"sun_copies={} sun_copy_skipped_unavailable={} sun_copy_skipped_not_full_sky={} sun_copy_skipped_no_light={} sun_copy_skipped_unsupported_count={} sun_copy_skipped_no_target={} sun_copy_skipped_invalid={} sun_copy_skipped_copy_target={} sun_copy_skipped_disabled={} "
			"cascade_count={} split_end=({:.1f},{:.1f}) "
			"probe_state={} probe_dispatches={} probe_skipped_not_full_sky={} probe_skipped_no_occlusion={} probe_skipped_no_grid={} resets={} resets_load={} resets_rebuild={} resets_enable={} "
			"grid_cell=({},{},{}) array_origin=({},{},{}) valid_margin=({},{},{}) probe_update_gpu_ms={:.3f} water_draws_bound={} composite_draws_bound={} dflight_draws_bound={} tiled_dispatches_bound={} shadow_vis_draws_bound={}",
			_settings.enabled ? 1 : 0, stateName, _anchorFrames, frameCount, count(_counters.captures), count(_counters.skippedInterior), count(_counters.skippedDisabled),
			count(_counters.skippedTargets), count(_counters.failed),
			occlusionDistance, OcclusionDir.x, OcclusionDir.y, OcclusionDir.z, frameCount % 4,
			count(stats.accepted), count(stats.delegated), ownTotal, count(stats.ownNew),
			own(Own::kLandscape), own(Own::kNotCasting), own(Own::kAlphaBlended), own(Own::kOther), count(stats.stockOnly),
			rejected(Reject::kSkinned), rejected(Reject::kFlags), rejected(Reject::kRadius), rejected(Reject::kBelowGrid), rejected(Reject::kBsx),
			cpuMsAverage, _windowCpuMsMax, stageMs[0], stageMs[1], stageMs[2], stageMs[3], _counters.stockTargetRestored.load(std::memory_order_relaxed) ? 1 : 0,
			extentX, extentY, depthRange,
			count(_cascadeCounters.copies), cascadeSkipped(CascadeSkip::kUnavailable), cascadeSkipped(CascadeSkip::kNotFullSky), cascadeSkipped(CascadeSkip::kNoLight),
			cascadeSkipped(CascadeSkip::kUnsupportedCount), cascadeSkipped(CascadeSkip::kNoTarget), cascadeSkipped(CascadeSkip::kInvalid), cascadeSkipped(CascadeSkip::kCopyTarget), cascadeSkipped(CascadeSkip::kDisabled),
			_cascadeCounters.count.load(std::memory_order_relaxed), _cascadeCounters.splitEnd[0].load(std::memory_order_relaxed), _cascadeCounters.splitEnd[1].load(std::memory_order_relaxed),
			probeStateNames[static_cast<std::size_t>(_probeState)], count(_probeCounters.dispatches), count(_probeCounters.skippedNotFullSky), count(_probeCounters.skippedNoOcclusion), count(_probeCounters.skippedNoGrid),
			count(_probeCounters.resets), count(_probeCounters.resetsLoad), count(_probeCounters.resetsRebuild), count(_probeCounters.resetsEnable),
			grid[0], grid[1], grid[2], grid[3], grid[4], grid[5], grid[6], grid[7], grid[8], _probeCounters.gpuMs.load(std::memory_order_relaxed), count(_probeCounters.waterDraws),
			count(_probeCounters.compositeDraws), count(_probeCounters.lightDraws), count(_probeCounters.tiledDispatches), count(_probeCounters.shadowVisDraws));
		_windowCpuMsSum = 0.0;
		_windowCpuMsMax = 0.0f;
		_windowCaptures = 0;
	}

	void Skylighting::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto count = [](const std::atomic<std::uint64_t>& a_value) { return a_value.load(std::memory_order_relaxed); };
		const auto& stats = cs::engine::GetOccluderStats();
		using Reject = cs::engine::OccluderReject;
		const auto rejected = [&](Reject a_reason) { return count(stats.rejected[static_cast<std::size_t>(a_reason)]); };
		const auto cascadeSkipped = [&](CascadeSkip a_reason) { return count(_cascadeCounters.skipped[static_cast<std::size_t>(a_reason)]); };
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("captures", count(_counters.captures))
			.Field("skipped_interior", count(_counters.skippedInterior))
			.Field("skipped_disabled", count(_counters.skippedDisabled))
			.Field("skipped_targets", count(_counters.skippedTargets))
			.Field("failed", count(_counters.failed))
			.Field("capture_cpu_ms_avg", static_cast<double>(_counters.cpuMsAverage.load(std::memory_order_relaxed)))
			.Field("capture_cpu_ms_max", static_cast<double>(_counters.cpuMsMax.load(std::memory_order_relaxed)))
			.Field("stock_ds8_restored", _counters.stockTargetRestored.load(std::memory_order_relaxed))
			.Field("ms_capture", static_cast<double>(_counters.stageMs[0].load(std::memory_order_relaxed)))
			.Field("ms_hook_predicate", static_cast<double>(_counters.stageMs[1].load(std::memory_order_relaxed)))
			.Field("ms_hook_stock", static_cast<double>(_counters.stageMs[2].load(std::memory_order_relaxed)))
			.Field("ms_hook_own", static_cast<double>(_counters.stageMs[3].load(std::memory_order_relaxed)))
			.Field("occluders_accepted", count(stats.accepted))
			.Field("occluders_delegated", count(stats.delegated))
			.Field("occluders_own_new", count(stats.ownNew))
			.Field("occluders_own_landscape", count(stats.ownBuilt[static_cast<std::size_t>(cs::engine::OwnBuildReason::kLandscape)]))
			.Field("occluders_own_not_casting", count(stats.ownBuilt[static_cast<std::size_t>(cs::engine::OwnBuildReason::kNotCasting)]))
			.Field("occluders_own_alpha_blended", count(stats.ownBuilt[static_cast<std::size_t>(cs::engine::OwnBuildReason::kAlphaBlended)]))
			.Field("occluders_own_other", count(stats.ownBuilt[static_cast<std::size_t>(cs::engine::OwnBuildReason::kOther)]))
			.Field("occluders_stock_only", count(stats.stockOnly))
			.Field("rejected_skinned", rejected(Reject::kSkinned))
			.Field("rejected_flags", rejected(Reject::kFlags))
			.Field("rejected_radius", rejected(Reject::kRadius))
			.Field("rejected_below_grid", rejected(Reject::kBelowGrid))
			.Field("rejected_bsx", rejected(Reject::kBsx))
			.Field("probe_dispatches", count(_probeCounters.dispatches))
			.Field("probe_skipped_not_full_sky", count(_probeCounters.skippedNotFullSky))
			.Field("probe_skipped_no_occlusion", count(_probeCounters.skippedNoOcclusion))
			.Field("probe_skipped_no_grid", count(_probeCounters.skippedNoGrid))
			.Field("resets", count(_probeCounters.resets))
			.Field("resets_load", count(_probeCounters.resetsLoad))
			.Field("resets_rebuild", count(_probeCounters.resetsRebuild))
			.Field("resets_enable", count(_probeCounters.resetsEnable))
			.Field("grid_cell_x", static_cast<std::int64_t>(_probeCounters.grid[0].load(std::memory_order_relaxed)))
			.Field("grid_cell_y", static_cast<std::int64_t>(_probeCounters.grid[1].load(std::memory_order_relaxed)))
			.Field("grid_cell_z", static_cast<std::int64_t>(_probeCounters.grid[2].load(std::memory_order_relaxed)))
			.Field("array_origin_x", static_cast<std::int64_t>(_probeCounters.grid[3].load(std::memory_order_relaxed)))
			.Field("array_origin_y", static_cast<std::int64_t>(_probeCounters.grid[4].load(std::memory_order_relaxed)))
			.Field("array_origin_z", static_cast<std::int64_t>(_probeCounters.grid[5].load(std::memory_order_relaxed)))
			.Field("valid_margin_x", static_cast<std::int64_t>(_probeCounters.grid[6].load(std::memory_order_relaxed)))
			.Field("valid_margin_y", static_cast<std::int64_t>(_probeCounters.grid[7].load(std::memory_order_relaxed)))
			.Field("valid_margin_z", static_cast<std::int64_t>(_probeCounters.grid[8].load(std::memory_order_relaxed)))
			.Field("probe_update_gpu_ms", static_cast<double>(_probeCounters.gpuMs.load(std::memory_order_relaxed)))
			.Field("debug_frames", count(_probeCounters.debugFrames))
			.Field("water_draws_bound", count(_probeCounters.waterDraws))
			.Field("composite_draws_bound", count(_probeCounters.compositeDraws))
			.Field("dflight_draws_bound", count(_probeCounters.lightDraws))
			.Field("tiled_dispatches_bound", count(_probeCounters.tiledDispatches))
			.Field("shadow_vis_draws_bound", count(_probeCounters.shadowVisDraws))
			.Field("sun_copies", count(_cascadeCounters.copies))
			.Field("sun_copy_skipped_unavailable", cascadeSkipped(CascadeSkip::kUnavailable))
			.Field("sun_copy_skipped_not_full_sky", cascadeSkipped(CascadeSkip::kNotFullSky))
			.Field("sun_copy_skipped_no_light", cascadeSkipped(CascadeSkip::kNoLight))
			.Field("sun_copy_skipped_unsupported_count", cascadeSkipped(CascadeSkip::kUnsupportedCount))
			.Field("sun_copy_skipped_no_target", cascadeSkipped(CascadeSkip::kNoTarget))
			.Field("sun_copy_skipped_invalid", cascadeSkipped(CascadeSkip::kInvalid))
			.Field("sun_copy_skipped_copy_target", cascadeSkipped(CascadeSkip::kCopyTarget))
			.Field("sun_copy_skipped_disabled", cascadeSkipped(CascadeSkip::kDisabled))
			.Field("cascade_count", static_cast<std::int64_t>(_cascadeCounters.count.load(std::memory_order_relaxed)))
			.Field("split_end_0", static_cast<double>(_cascadeCounters.splitEnd[0].load(std::memory_order_relaxed)))
			.Field("split_end_1", static_cast<double>(_cascadeCounters.splitEnd[1].load(std::memory_order_relaxed)));
		cs::render::profiling::CollectPassTimings(a_sink, "Skylighting/");
	}

	cs::SkylightingFeatureData Skylighting::GetCommonBufferData()
	{
		const auto* graphics = cs::engine::GetGraphicsState();
		if (!graphics || !_settings.enabled)
			return {};
		// FO4: packs repeat within a frame and the advance must happen once.
		if (_grid.frame == graphics->frameCount)
			return { .shared = _grid.block, .Enabled = 1u };
		// FO4: the captured world camera replaces Util::GetEyePosition.
		const auto camera = cs::engine::GetCapturedWorldCameraRecord(graphics->frameCount);
		if (!camera)
			return {};

		const auto eye = cs::engine::CameraWorldOrigin(*camera);
		const DirectX::XMFLOAT3 cellSize{
			occlusionDistance / kProbeArrayDims[0],
			occlusionDistance / kProbeArrayDims[1],
			occlusionDistance * .5f / kProbeArrayDims[2]
		};
		const DirectX::XMFLOAT3 cellID{ std::round(eye.x / cellSize.x), std::round(eye.y / cellSize.y), std::round(eye.z / cellSize.z) };
		const DirectX::XMFLOAT3 cellOrigin{ cellID.x * cellSize.x, cellID.y * cellSize.y, cellID.z * cellSize.z };
		probeGridBottomZ = cellOrigin.z - cellSize.z * kProbeArrayDims[2] * .5f;
		const DirectX::XMFLOAT3 cellIDDiff{ _grid.prevCellID.x - cellID.x, _grid.prevCellID.y - cellID.y, _grid.prevCellID.z - cellID.z };
		_grid.prevCellID = cellID;
		_grid.cellID = cellID;

		// FO4: model space is world minus the camera position adjustment, not the eye.
		const auto& anchor = camera->CameraPosAdjust;
		const auto arrayOrigin = [&](float a_cell, std::size_t a_axis) {
			return (static_cast<std::uint32_t>(static_cast<int>(a_cell)) - kProbeArrayDims[a_axis] / 2) % kProbeArrayDims[a_axis];
		};
		_grid.block = {
			.OcclusionViewProj = OcclusionTransform,
			.OcclusionDir = OcclusionDir,
			.PosOffset = { cellOrigin.x - anchor.x, cellOrigin.y - anchor.y, cellOrigin.z - anchor.z, 0.0f },
			.ArrayOrigin = { arrayOrigin(cellID.x, 0), arrayOrigin(cellID.y, 1), arrayOrigin(cellID.z, 2), 0 },
			.ValidMargin = { static_cast<int>(cellIDDiff.x), static_cast<int>(cellIDDiff.y), static_cast<int>(cellIDDiff.z), 0 },
			.MinDiffuseVisibility = _settings.MinDiffuseVisibility,
			.MinSpecularVisibility = _settings.MinSpecularVisibility
		};
		_grid.frame = graphics->frameCount;
		return { .shared = _grid.block, .Enabled = 1u };
	}

	void Skylighting::Prepass()
	{
		auto state = ProbeState::kDispatched;
		if (!_settings.enabled) {
			state = ProbeState::kDisabled;
		} else if (!cs::engine::IsFullSky()) {
			_probeCounters.skippedNotFullSky.fetch_add(1, std::memory_order_relaxed);
			state = ProbeState::kNotFullSky;
		} else if (!_hasOcclusion) {
			_probeCounters.skippedNoOcclusion.fetch_add(1, std::memory_order_relaxed);
			state = ProbeState::kNoOcclusion;
		} else if (const auto* graphics = cs::engine::GetGraphicsState(); !graphics || _grid.frame != graphics->frameCount) {
			// FO4: b6 must carry this frame's grid advance.
			_probeCounters.skippedNoGrid.fetch_add(1, std::memory_order_relaxed);
			state = ProbeState::kNoGrid;
		} else if (auto* context = cs::engine::GetImmediateContext()) {
			DispatchProbeUpdate(context);
		} else {
			state = ProbeState::kPending;
		}
		if (std::exchange(_probeState, state) != state)
			LogCaptureSummary(_loggedState);
	}

	void Skylighting::PublishShadowLights(ID3D11DeviceContext* a_context, const skylighting::DirectionalShadowLightData& a_data)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context->Map(_shadowLightsBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			throw std::runtime_error("the Skylighting shadow light buffer could not be mapped");
		std::memcpy(mapped.pData, &a_data, sizeof(a_data));
		a_context->Unmap(_shadowLightsBuffer.get(), 0);
	}

	void Skylighting::RetireCascades(ID3D11DeviceContext* a_context)
	{
		if (!std::exchange(_cascadesPublished, false))
			return;
		PublishShadowLights(a_context, {});
	}

	bool Skylighting::EnsureCascadeCopy(ID3D11Device* a_device, ID3D11Texture2D* a_source)
	{
		D3D11_TEXTURE2D_DESC source{};
		a_source->GetDesc(&source);
		if (source.Format != DXGI_FORMAT_R16_TYPELESS || source.ArraySize != cs::engine::kMaxSunCascades || source.MipLevels != 1 || source.SampleDesc.Count != 1)
			return false;
		if (_cascadeCopy && _cascadeDesc.Width == source.Width && _cascadeDesc.Height == source.Height && _cascadeDesc.ArraySize == source.ArraySize)
			return true;

		_cascadeCopySRV = nullptr;
		_cascadeCopy = nullptr;
		const D3D11_TEXTURE2D_DESC desc{
			.Width = source.Width,
			.Height = source.Height,
			.MipLevels = 1,
			.ArraySize = source.ArraySize,
			.Format = DXGI_FORMAT_R16_TYPELESS,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE
		};
		const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format = DXGI_FORMAT_R16_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
			.Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = desc.ArraySize }
		};
		winrt::com_ptr<ID3D11Texture2D> texture;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (FAILED(a_device->CreateTexture2D(&desc, nullptr, texture.put())) || FAILED(a_device->CreateShaderResourceView(texture.get(), &srvDesc, srv.put())))
			return false;
		cs::render::annotation::SetName(texture.get(), "Skylighting/SunCascades");
		cs::render::annotation::SetName(srv.get(), "Skylighting/SunCascades.SRV");
		_cascadeCopy = std::move(texture);
		_cascadeCopySRV = std::move(srv);
		_cascadeDesc = desc;
		CaptureLog->info("sun cascade copy: {}x{}x{} R16_TYPELESS, SRV R16_UNORM (stock array bind flags {:#x})",
			desc.Width, desc.Height, desc.ArraySize, source.BindFlags);
		return true;
	}

	Skylighting::CascadeSkip Skylighting::PublishSunCascades(ID3D11DeviceContext* a_context)
	{
		auto* device = cs::engine::GetDevice();
		if (!IsHealthy() || !_probesReady.load(std::memory_order_acquire) || !device)
			return CascadeSkip::kUnavailable;
		if (!_settings.enabled)
			return CascadeSkip::kDisabled;
		if (!cs::engine::IsFullSky())
			return CascadeSkip::kNotFullSky;

		cs::engine::SunCascadeSnapshot snapshot;
		switch (cs::engine::TryGetSunCascades(snapshot)) {
		case cs::engine::SunCascadeStatus::kOk:
			break;
		case cs::engine::SunCascadeStatus::kNoLight:
			return CascadeSkip::kNoLight;
		case cs::engine::SunCascadeStatus::kUnsupportedCount:
			return CascadeSkip::kUnsupportedCount;
		case cs::engine::SunCascadeStatus::kNoTarget:
			return CascadeSkip::kNoTarget;
		case cs::engine::SunCascadeStatus::kInvalid:
			return CascadeSkip::kInvalid;
		}
		// The light block and shader fix the cascade count at two.
		if (snapshot.count != cs::engine::kMaxSunCascades)
			return CascadeSkip::kUnsupportedCount;

		skylighting::DirectionalShadowLightData data{};
		for (std::size_t i = 0; i < cs::engine::kMaxSunCascades; ++i) {
			// Row-vector engine matrix; column_major HLSL reads the same bytes transposed.
			data.ShadowProj[i] = snapshot.worldToShadow[i];
			DirectX::XMVECTOR determinant;
			const auto inverse = DirectX::XMMatrixInverse(&determinant, DirectX::XMLoadFloat4x4(&snapshot.worldToShadow[i]));
			const float det = DirectX::XMVectorGetX(determinant);
			if (!std::isfinite(det) || det == 0.0f)
				return CascadeSkip::kInvalid;
			DirectX::XMStoreFloat4x4(&data.InvShadowProj[i], inverse);
		}
		data.EndSplitDistances = { snapshot.splitEnd[0], snapshot.splitEnd[1] };
		// FO4: the engine's overlap constant is untyped and the shader ignores starts.
		data.StartSplitDistances = { 0.0f, snapshot.splitEnd[0] };

		if (!EnsureCascadeCopy(device, snapshot.texture))
			return CascadeSkip::kCopyTarget;
		cs::engine::CopyResourcePreservingOM(a_context, _cascadeCopy.get(), snapshot.texture);
		PublishShadowLights(a_context, data);
		_cascadesPublished = true;

		_cascadeCounters.count.store(snapshot.count, std::memory_order_relaxed);
		for (std::size_t i = 0; i < cs::engine::kMaxSunCascades; ++i)
			_cascadeCounters.splitEnd[i].store(snapshot.splitEnd[i], std::memory_order_relaxed);
		if (!std::exchange(_loggedCascadeDesc, true)) {
			// The cached matrix already carries the cascade sub-rect in its UV scale.
			for (std::size_t i = 0; i < cs::engine::kMaxSunCascades; ++i) {
				const auto& m = snapshot.worldToShadow[i].m;
				const float uScale = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
				const float vScale = std::sqrt(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]);
				const float texel = snapshot.worldUnitsPerTexel[i];
				CaptureLog->info("sun cascade {}: split_end={:.1f} units_per_texel={:.3f} uv_scale_ratio=({:.4f},{:.4f}) viewport=({},{},{},{})",
					i, snapshot.splitEnd[i], texel, uScale * texel * _cascadeDesc.Width, vScale * texel * _cascadeDesc.Height,
					snapshot.viewport[i][0], snapshot.viewport[i][1], snapshot.viewport[i][2], snapshot.viewport[i][3]);
			}
		}
		return CascadeSkip::kNone;
	}

	void Skylighting::CopySunCascades()
	{
		auto* context = cs::engine::GetImmediateContext();
		const auto skip = context ? PublishSunCascades(context) : CascadeSkip::kUnavailable;
		if (skip == CascadeSkip::kNone) {
			_cascadeCounters.copies.fetch_add(1, std::memory_order_relaxed);
		} else {
			_cascadeCounters.skipped[static_cast<std::size_t>(skip)].fetch_add(1, std::memory_order_relaxed);
			// A skipped frame must not leave stale cascades beside live probe updates.
			if (context)
				RetireCascades(context);
		}
		if (std::exchange(_loggedCascadeSkip, skip) != skip)
			CaptureLog->info("sun cascades: {}", kCascadeSkipNames[static_cast<std::size_t>(skip)]);
	}

	void Skylighting::DispatchProbeUpdate(ID3D11DeviceContext* a_context)
	{
		cs::engine::ComputeOMScope scope(a_context, 4, 1, 4, 0);
		cs::render::ScopedComputeSharedDataBinding substrate(a_context);

		// FO4: no ESRAM shadow, so t3 repeats the cascade copy; unpublished stays null.
		auto* cascades = _cascadesPublished ? _cascadeCopySRV.get() : nullptr;
		std::array<ID3D11ShaderResourceView*, 4> srvs = {
			_occlusionSRV.get(),
			cascades,
			_shadowLightsSRV.get(),
			cascades
		};
		std::array<ID3D11UnorderedAccessView*, 4> uavs = {
			_texProbeArray->uav.get(),
			_texAccumFramesArray->uav.get(),
			_texShadowBitmask->uav.get(),
			_texShadowVisibility->uav.get()
		};
		std::array<ID3D11SamplerState*, 1> samplers = {
			_comparisonSampler.get()
		};

		a_context->CSSetSamplers(0, (UINT)samplers.size(), samplers.data());
		a_context->CSSetShaderResources(0, (UINT)srvs.size(), srvs.data());
		a_context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);
		a_context->CSSetShader(_probeUpdateCompute.get(), nullptr, 0);
		{
			cs::render::annotation::ScopedEvent event(kProbeUpdatePass);
			a_context->Dispatch((kProbeArrayDims[0] + 7u) >> 3, (kProbeArrayDims[1] + 7u) >> 3, kProbeArrayDims[2]);
		}
		_probeCounters.dispatches.fetch_add(1, std::memory_order_relaxed);
	}

	void Skylighting::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto& [enabledField, maxZenith, minDiffuse, minSpecular] = skylighting::kSchema.fields;
		const auto labelOf = [](const auto& a_field) {
			return std::string(a_field.description) + "##" + std::string(a_field.key);
		};
		const auto drawVisibility = [&](const auto& a_field) {
			const auto range = skylighting::kSchema.EditRange(a_field.member);
			edit.Continuous(dmui::ui::SliderScalar(labelOf(a_field).c_str(), &(_settings.*a_field.member), &range.min, &range.max, "%.2f"));
		};

		edit.Discrete(dmui::ui::Checkbox("Enabled", &_settings.enabled));
		dmui::ui::Spacing();

		dmui::ui::Text("%s", "Minimum visibility values. Diffuse darkens objects. Specular removes the sky from reflections.");
		drawVisibility(minDiffuse);
		drawVisibility(minSpecular);

		dmui::ui::Separator();

		if (dmui::ui::Button("Rebuild Skylighting"))
			QueueReset(kResetRebuild);

		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone }; tooltip.Visible())
			dmui::ui::Text("%s", "Changes below require rebuilding, a loading screen, or moving away from the current location to apply.");

		// Stored in radians; the slider edits degrees like upstream's SliderAngle.
		const auto range = skylighting::kSchema.EditRange(maxZenith.member);
		const float minDegrees = range.min * kRadiansToDegrees;
		const float maxDegrees = range.max * kRadiansToDegrees;
		float degrees = _settings.MaxZenith * kRadiansToDegrees;
		const bool changed = dmui::ui::SliderScalar(labelOf(maxZenith).c_str(), &degrees, &minDegrees, &maxDegrees, "%.0f deg", dmui::ui::SliderFlags::kAlwaysClamp);
		if (changed)
			_settings.MaxZenith = degrees / kRadiansToDegrees;
		edit.Continuous(changed);
		if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone }; tooltip.Visible())
			dmui::ui::Text("%s", "Smaller angles creates more focused top-down shadow.");

		dmui::ui::Separator();
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void Skylighting::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(Skylighting::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
