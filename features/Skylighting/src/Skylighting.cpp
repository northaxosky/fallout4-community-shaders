#include "Skylighting.h"
#include "Render/SharedDataLayout.h"

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
#include "LogThrottle.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/FrameBuffer.h"
#include "Render/FrameProfiler.h"
#include "Render/OccluderBatch.h"
#include "Render/PrecipitationOcclusion.h"
#include "Render/RenderExtents.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderInjection.h"
#include "Render/ShadowCascades.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "Utils/SphericalHarmonics.h"
#include "World/Sky.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting");
		auto* CaptureLog = cs::log::Get("cs.feature.skylighting.capture");

		constexpr std::uint64_t kSummaryIntervalFrames = 300;
		constexpr std::string_view kProbeUpdatePass = "Skylighting::ProbeUpdate";

		constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;

		constexpr std::array kCascadeSkipNames{ "none", "unavailable", "not_full_sky", "sun_cascades", "copy_target", "disabled" };

		bool ActiveLightReadsShadowVisibility() noexcept
		{
			constexpr auto target = cs::engine::ShaderInjectionTarget::kBsdfLight;
			// Visibility also covers unshadowed passes.
			return cs::engine::ActiveLightHasSunShadowedFamily() ||
			       (cs::engine::ActiveShaderInjectionVariantHasDefine(target, "DIRECTIONAL") &&
					   cs::engine::ActiveShaderInjectionVariantHasDefine(target, "BSDFLIGHT_PS_UNSHADOWED"));
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
		const bool registered = cs::engine::RegisterPostPrecipitationOcclusion(*this, [this] { RenderOcclusion(); });
		CaptureLog->info("hook Precipitation::RenderOcclusionMap detour: {}", registered ? "installed" : "unavailable");
		if (!installed || !registered) {
			FailLoad("the precipitation occlusion capture hooks could not be installed");
			return;
		}

		if (!cs::engine::RegisterFeatureShaderBindings(*this, [this](cs::engine::ShaderReplacementRegistration& registration) {
				if (const auto consumer = ConsumerFor(registration.targetId))
					registration.bind = [this, consumer = *consumer](ID3D11DeviceContext* a_context) { BindConsumer(a_context, consumer); };
			})) {
			FailLoad("Skylighting shader contribution registration failed.");
			return;
		}
		if (!cs::engine::AcquireSunShadowLights<cs::engine::kMaxSunCascades>()) {
			FailLoad("Skylighting shadow history needs the shared sun shadow light data");
			return;
		}
		// FO4: the stock array holds the sun cascades until focus shadows reuse it.
		if (!cs::engine::RegisterPostSunShadowRender(*this, [this] { CopySunCascades(); })) {
			FailLoad("Skylighting shadow history needs the main sun shadow render hook");
			return;
		}
		if (!cs::engine::RegisterPreDeferredComposite(
				*this,
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

	bool Skylighting::ValidateInjectionPrerequisites(std::string& a_error)
	{
		return RequireSharedData(a_error, "b6 carries no probe grid");
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
		case Target::kEffect:
			return Consumer::kEffect;
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
		auto* probes = GetBindableProbeArraySRV();
		if (!a_context || !probes)
			return;
		const bool lighting = a_consumer == Consumer::kLight || a_consumer == Consumer::kTiled;
		// Only the raster sun reads shadow visibility; tiled has no directional lobes.
		const bool shadowVisibility = a_consumer == Consumer::kLight;
		const std::array<ID3D11ShaderResourceView*, 4> views{
			probes,
			lighting ? cs::engine::GetRenderTargetSRV(RE::BSGraphics::RenderTargetID::kGBufferAlbedoSpecular) : nullptr,
			lighting ? cs::engine::GetRenderTargetSRV(RE::BSGraphics::RenderTargetID::kGBufferEmissive) : nullptr,
			shadowVisibility ? _texShadowVisibility->srv.get() : nullptr
		};
		cs::engine::BindInjectionShaderResources(a_context, kProbeArraySlot, shadowVisibility ? 4 : lighting ? 3 :
																											   1,
			views.data());
		if (shadowVisibility && ActiveLightReadsShadowVisibility())
			_probeCounters.shadowVisDraws.fetch_add(1, std::memory_order_relaxed);
		auto& counter = a_consumer == Consumer::kWater     ? _probeCounters.waterDraws :
		                a_consumer == Consumer::kEffect    ? _probeCounters.effectDraws :
		                a_consumer == Consumer::kComposite ? _probeCounters.compositeDraws :
		                a_consumer == Consumer::kLight     ? _probeCounters.lightDraws :
		                                                     _probeCounters.tiledDispatches;
		counter.fetch_add(1, std::memory_order_relaxed);
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

		// FO4: the substrate defines the CS camera and shared-data inputs.
		_probeUpdateCompute = cs::util::RequireComputeShader("Skylighting/UpdateProbes.CS", L"Data\\Shaders\\Skylighting\\UpdateProbesCS.hlsl", { { "SKYLIGHTING_SUN_CASCADES", cs::engine::kSunCascadesDefine } });

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
			cs::util::ShaderDefines defines;
			if (index == 1)
				defines.emplace_back("SKYLIGHTING_UP_VISIBILITY", "1");
			_debugCompute[index] = cs::util::CompileComputeShader(index ? "Skylighting/DebugUp.CS" : "Skylighting/DebugDiffuse.CS", L"Data\\Shaders\\FO4\\Skylighting\\DebugCS.hlsl", defines);
			if (!_debugCompute[index])
				L->warn("Skylighting debug shader {} failed to compile; the fullscreen views are unavailable.", index);
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
		_copyGeneration = 0;
	}

	void Skylighting::CreateOcclusionResources(ID3D11Device* a_device)
	{
		_occlusionMap.Create(a_device, "Skylighting/Occlusion");
		CaptureLog->info("occlusion depth target: {0}x{0} R16_TYPELESS, DSV D16_UNORM, SRV R16_UNORM", cs::engine::kOcclusionMapSize);
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
		auto* texture = cs::render::GetFullscreenDebugTargetSRV();
		if (!_debugFrameReady || !texture)
			return {};
		return { .owner = FullscreenDebugOwner::Skylighting,
			.mode = static_cast<std::uint32_t>(_debugVisualization.load(std::memory_order_acquire)),
			.texture = texture };
	}

	void Skylighting::RenderDebug(ID3D11DeviceContext* a_context)
	{
		_debugFrameReady = false;
		cs::render::InvalidateFullscreenDebugData();
		const auto mode = _debugVisualization.load(std::memory_order_acquire);
		if (mode == DebugVisualization::kOff || !IsEnabled() || !_injectionsOperational.load(std::memory_order_acquire) || !_probesReady.load(std::memory_order_acquire))
			return;
		auto* shader = _debugCompute[mode == DebugVisualization::kUp ? 1 : 0].get();
		auto* normals = cs::engine::GetRenderTargetSRV(RE::BSGraphics::RenderTargetID::kGBufferNormal);
		const auto* graphics = cs::engine::GetGraphicsState();
		if (!shader || !normals || !graphics || !cs::render::IsSharedDataReady())
			return;
		const auto extent = cs::render::GetActiveExtent(graphics->screenWidth, graphics->screenHeight);
		if (!extent.width || !extent.height)
			return;
		auto* target = cs::render::AcquireFullscreenDebugTarget();
		if (!target)
			return;
		cs::engine::ComputeOMScope scope(a_context, 2, 0, 1, 0);
		cs::render::ScopedComputeSharedDataBinding substrate(a_context);
		cs::render::annotation::ScopedEvent event("Skylighting::Debug", false);
		ID3D11ShaderResourceView* srvs[]{ normals, _texProbeArray->srv.get() };
		ID3D11UnorderedAccessView* uavs[]{ target->uav.get() };
		a_context->CSSetShaderResources(0, 2, srvs);
		a_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		a_context->CSSetShader(shader, nullptr, 0);
		a_context->Dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);
		_debugFrameReady = true;
		cs::render::InvalidateFullscreenDebugData();
		_probeCounters.debugFrames.fetch_add(1, std::memory_order_relaxed);
	}

	FeatureDebugTexture Skylighting::GetOcclusionDebugTexture() const
	{
		FeatureDebugTexture texture{ .unavailableText = "Occlusion depth not allocated." };
		if (!_debugPreviewEnabled.load(std::memory_order_acquire) || !_occlusionMap.Ready())
			return texture;
		texture.texture = _occlusionMap.Target().depthSRV;
		texture.width = cs::engine::kOcclusionMapSize;
		texture.height = cs::engine::kOcclusionMapSize;
		return texture;
	}

	void Skylighting::RenderOcclusion()
	{
		const auto state = CaptureFrame();
		// Keyed on anchor frames so skipped frames report; state changes log at once.
		const bool periodic = ++_anchorFrames % kSummaryIntervalFrames == 0;
		const bool changed = _anchorFrames == 1 || state != _loggedState || std::exchange(_summaryPending, false);
		if (changed || periodic)
			LogCaptureSummary(state, changed ? spdlog::level::info : spdlog::level::debug);
	}

	Skylighting::CaptureState Skylighting::CaptureReadiness() const
	{
		if (!IsHealthy() || !IsEnabled() || !_occlusionMap.Ready() || !_probesReady.load(std::memory_order_acquire))
			return CaptureState::kDisabled;
		return cs::engine::IsInterior() ? CaptureState::kInterior : CaptureState::kCaptured;
	}

	Skylighting::CaptureState Skylighting::CaptureFrame()
	{
		const auto readiness = CaptureReadiness();
		if (readiness == CaptureState::kDisabled) {
			if (!IsEnabled())
				_wasEnabled = false;
			_counters.skippedDisabled.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kDisabled;
		}
		// Re-enabling must not show history from before the disabled stretch.
		if (!std::exchange(_wasEnabled, true))
			QueueReset(kResetEnable);
		if (readiness == CaptureState::kInterior) {
			_counters.skippedInterior.fetch_add(1, std::memory_order_relaxed);
			return CaptureState::kInterior;
		}

		ApplyQueuedReset();

		// Clock reads only feed the summary log and telemetry.
		const bool timed = cs::telemetry::pump::Enabled();
		const auto start = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

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
			.batching = { _settings.BatchOccluders, _batchValidationInterval.load(std::memory_order_relaxed) },
			.target = _occlusionMap.Target()
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

		const auto basis = SphericalHarmonics::Scale(SphericalHarmonics::Evaluate(float3(-PrecipitationShaderDirectionF.x, -PrecipitationShaderDirectionF.y, -PrecipitationShaderDirectionF.z)), 4.0f * std::numbers::pi_v<float>);
		OcclusionSHBasis4Pi = { basis.c0, basis.c1[0], basis.c1[1], basis.c1[2] };
		OcclusionTransform = result.matrix;
		_hasOcclusion = true;

		_counters.captures.fetch_add(1, std::memory_order_relaxed);
		_counters.stockTargetRestored.store(result.stockTargetRestored, std::memory_order_relaxed);
		if (timed) {
			const auto cpuMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
			_windowCpuMsSum += cpuMs;
			_windowCpuMsMax = std::max(_windowCpuMsMax, cpuMs);
			++_windowCaptures;
		}
		return CaptureState::kCaptured;
	}

	// Render thread only; one line per window keeps the capture log cheap.
	void Skylighting::LogCaptureSummary(CaptureState a_state, spdlog::level::level_enum a_level)
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

		const auto& timings = cs::engine::GetCaptureTimings();
		const auto windowMs = [&](const std::atomic<cs::engine::CaptureClock::rep>& a_slot, cs::engine::CaptureClock::rep& a_snapshot) {
			const auto now = a_slot.load(std::memory_order_relaxed);
			const auto ms = _windowCaptures ? static_cast<float>(cs::engine::TicksToMs(now - a_snapshot) / _windowCaptures) : 0.0f;
			a_snapshot = now;
			return ms;
		};
		const float captureMs = windowMs(timings.capture, _timingSnapshot);
		_counters.captureMs.store(captureMs, std::memory_order_relaxed);
		const float gatherMs = windowMs(timings.gather, _gatherSnapshot);
		const float passesMs = windowMs(timings.passes, _passesSnapshot);
		const float batchMs = windowMs(timings.batch, _batchSnapshot);
		_counters.batchMs.store(batchMs, std::memory_order_relaxed);
		const auto& batch = cs::engine::GetBatchStats();
		using BatchFallback = cs::engine::BatchFallback;
		const auto fallback = [&](BatchFallback a_reason) { return count(batch.fallback[static_cast<std::size_t>(a_reason)]); };
		_counters.gatherMs.store(gatherMs, std::memory_order_relaxed);
		_counters.passesMs.store(passesMs, std::memory_order_relaxed);

		const auto cascadeSkipped = [&](CascadeSkip a_reason) { return count(_cascadeCounters.skipped[static_cast<std::size_t>(a_reason)]); };
		const auto statusSkipped = [&](cs::engine::SunCascadeStatus a_status) { return count(_cascadeCounters.statusSkipped[static_cast<std::size_t>(a_status)]); };
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
		float probeGpuMs = 0.0f;
		for (const auto& result : cs::render::profiling::GetProfiler().GetResults()) {
			if (result.valid && result.name == kProbeUpdatePass)
				probeGpuMs = result.gpuTimeMs;
		}
		_probeCounters.gpuMs.store(probeGpuMs, std::memory_order_relaxed);
		CaptureLog->log(
			a_level,
			"summary enabled={} state={} anchor_frames={} frame={} captures={} skipped_interior={} skipped_disabled={} skipped_targets={} failed={} "
			"L={:.0f} sh4pi=({:.3f},{:.3f},{:.3f},{:.3f}) quadrant={} "
			"accepted={} delegated={} own_built={} own_new={} own_landscape={} own_not_casting={} own_alpha_blended={} own_other={} stock_only={} "
			"rej_skinned={} rej_flags={} rej_radius={} rej_below_grid={} rej_bsx={} "
			"batched_plain={} batched_merge={} batch_draws={} batch_pool_records={} batch_fallback_technique={} batch_fallback_animated={} batch_fallback_renderer_data={} batch_fallback_buffer={} batch_fallback_structured={} batch_fallback_heap_view={} batch_fallback_pool_full={} batch_fallback_arena_full={} "
			"cpu_ms_avg={:.3f} cpu_ms_max={:.3f} ms_capture={:.3f} ms_gather={:.3f} ms_passes={:.3f} ms_batch={:.3f} stock_ds8_restored={} "
			"mx_ext_x={:.3f} mx_ext_y={:.3f} mx_depth={:.3f} "
			"sun_copies={} sun_copy_skipped_unavailable={} sun_copy_skipped_not_full_sky={} sun_copy_skipped_no_light={} sun_copy_skipped_unsupported_count={} sun_copy_skipped_no_target={} sun_copy_skipped_invalid={} sun_copy_skipped_copy_target={} sun_copy_skipped_disabled={} "
			"cascade_count={} split_end=({:.1f},{:.1f},{:.1f}) "
			"probe_state={} probe_dispatches={} probe_skipped_not_full_sky={} probe_skipped_no_occlusion={} probe_skipped_no_grid={} resets={} resets_load={} resets_rebuild={} resets_enable={} "
			"grid_cell=({},{},{}) array_origin=({},{},{}) valid_margin=({},{},{}) probe_update_gpu_ms={:.3f} water_draws_bound={} effect_draws_bound={} composite_draws_bound={} dflight_draws_bound={} tiled_dispatches_bound={} shadow_vis_draws_bound={}",
			IsEnabled() ? 1 : 0, stateName, _anchorFrames, frameCount, count(_counters.captures), count(_counters.skippedInterior), count(_counters.skippedDisabled),
			count(_counters.skippedTargets), count(_counters.failed),
			occlusionDistance, OcclusionSHBasis4Pi.x, OcclusionSHBasis4Pi.y, OcclusionSHBasis4Pi.z, OcclusionSHBasis4Pi.w, frameCount % 4,
			count(stats.accepted), count(stats.delegated), ownTotal, count(stats.ownNew),
			own(Own::kLandscape), own(Own::kNotCasting), own(Own::kAlphaBlended), own(Own::kOther), count(stats.stockOnly),
			rejected(Reject::kSkinned), rejected(Reject::kFlags), rejected(Reject::kRadius), rejected(Reject::kBelowGrid), rejected(Reject::kBsx),
			count(batch.batched[0]), count(batch.batched[1]), count(batch.draws), batch.poolRecords.load(std::memory_order_relaxed),
			fallback(BatchFallback::kTechnique), fallback(BatchFallback::kAnimated), fallback(BatchFallback::kRendererData), fallback(BatchFallback::kBufferNotReady),
			fallback(BatchFallback::kStructuredNotReady), fallback(BatchFallback::kHeapView), fallback(BatchFallback::kPoolFull), fallback(BatchFallback::kArenaFull),
			cpuMsAverage, _windowCpuMsMax, captureMs, gatherMs, passesMs, batchMs, _counters.stockTargetRestored.load(std::memory_order_relaxed) ? 1 : 0,
			extentX, extentY, depthRange,
			count(_cascadeCounters.copies), cascadeSkipped(CascadeSkip::kUnavailable), cascadeSkipped(CascadeSkip::kNotFullSky), statusSkipped(cs::engine::SunCascadeStatus::kNoLight),
			statusSkipped(cs::engine::SunCascadeStatus::kUnsupportedCount), statusSkipped(cs::engine::SunCascadeStatus::kNoTarget), statusSkipped(cs::engine::SunCascadeStatus::kInvalid), cascadeSkipped(CascadeSkip::kCopyTarget), cascadeSkipped(CascadeSkip::kDisabled),
			_cascadeCounters.count.load(std::memory_order_relaxed), _cascadeCounters.splitEnd[0].load(std::memory_order_relaxed), _cascadeCounters.splitEnd[1].load(std::memory_order_relaxed), _cascadeCounters.splitEnd[2].load(std::memory_order_relaxed),
			probeStateNames[static_cast<std::size_t>(_probeState)], count(_probeCounters.dispatches), count(_probeCounters.skippedNotFullSky), count(_probeCounters.skippedNoOcclusion), count(_probeCounters.skippedNoGrid),
			count(_probeCounters.resets), count(_probeCounters.resetsLoad), count(_probeCounters.resetsRebuild), count(_probeCounters.resetsEnable),
			grid[0], grid[1], grid[2], grid[3], grid[4], grid[5], grid[6], grid[7], grid[8], _probeCounters.gpuMs.load(std::memory_order_relaxed), count(_probeCounters.waterDraws),
			count(_probeCounters.effectDraws), count(_probeCounters.compositeDraws), count(_probeCounters.lightDraws), count(_probeCounters.tiledDispatches), count(_probeCounters.shadowVisDraws));
		_windowCpuMsSum = 0.0;
		_windowCpuMsMax = 0.0f;
		_windowCaptures = 0;
	}

	void Skylighting::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto count = [](const std::atomic<std::uint64_t>& a_value) { return a_value.load(std::memory_order_relaxed); };
		const auto& stats = cs::engine::GetOccluderStats();
		const auto& gather = cs::engine::GetGatherStats();
		const auto& batch = cs::engine::GetBatchStats();
		using BatchFallback = cs::engine::BatchFallback;
		const auto batchFallback = [&](BatchFallback a_reason) { return count(batch.fallback[static_cast<std::size_t>(a_reason)]); };
		using Reject = cs::engine::OccluderReject;
		const auto rejected = [&](Reject a_reason) { return count(stats.rejected[static_cast<std::size_t>(a_reason)]); };
		const auto cascadeSkipped = [&](CascadeSkip a_reason) { return count(_cascadeCounters.skipped[static_cast<std::size_t>(a_reason)]); };
		const auto statusSkipped = [&](cs::engine::SunCascadeStatus a_status) { return count(_cascadeCounters.statusSkipped[static_cast<std::size_t>(a_status)]); };
		a_sink
			.Field("captures", count(_counters.captures))
			.Field("skipped_interior", count(_counters.skippedInterior))
			.Field("skipped_disabled", count(_counters.skippedDisabled))
			.Field("skipped_targets", count(_counters.skippedTargets))
			.Field("failed", count(_counters.failed))
			.Field("capture_cpu_ms_avg", static_cast<double>(_counters.cpuMsAverage.load(std::memory_order_relaxed)))
			.Field("capture_cpu_ms_max", static_cast<double>(_counters.cpuMsMax.load(std::memory_order_relaxed)))
			.Field("stock_ds8_restored", _counters.stockTargetRestored.load(std::memory_order_relaxed))
			.Field("ms_capture", static_cast<double>(_counters.captureMs.load(std::memory_order_relaxed)))
			.Field("ms_gather", static_cast<double>(_counters.gatherMs.load(std::memory_order_relaxed)))
			.Field("ms_passes", static_cast<double>(_counters.passesMs.load(std::memory_order_relaxed)))
			.Field("ms_batch", static_cast<double>(_counters.batchMs.load(std::memory_order_relaxed)))
			.Field("batched_plain", count(batch.batched[static_cast<std::size_t>(cs::engine::BatchClass::kPlain)]))
			.Field("batched_merge", count(batch.batched[static_cast<std::size_t>(cs::engine::BatchClass::kMergeInstanced)]))
			.Field("batch_draws", count(batch.draws))
			.Field("batch_pool_records", static_cast<std::uint64_t>(batch.poolRecords.load(std::memory_order_relaxed)))
			.Field("batch_fallback_technique", batchFallback(BatchFallback::kTechnique))
			.Field("batch_fallback_animated", batchFallback(BatchFallback::kAnimated))
			.Field("batch_fallback_renderer_data", batchFallback(BatchFallback::kRendererData))
			.Field("batch_fallback_buffer", batchFallback(BatchFallback::kBufferNotReady))
			.Field("batch_fallback_structured", batchFallback(BatchFallback::kStructuredNotReady))
			.Field("batch_fallback_heap_view", batchFallback(BatchFallback::kHeapView))
			.Field("batch_fallback_pool_full", batchFallback(BatchFallback::kPoolFull))
			.Field("batch_fallback_arena_full", batchFallback(BatchFallback::kArenaFull))
			.Field("batch_validations", count(batch.validations))
			.Field("batch_validation_failures", count(batch.validationFailures))
			.Field("batch_mismatched_texels", static_cast<std::uint64_t>(batch.mismatchedTexels.load(std::memory_order_relaxed)))
			.Field("batch_max_delta", static_cast<std::uint64_t>(batch.maxDelta.load(std::memory_order_relaxed)))
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
			.Field("gather_roots_tested", count(gather.rootsTested))
			.Field("gather_roots_culled", count(gather.rootsCulled))
			.Field("gather_leaves_fed", count(gather.leavesFed))
			.Field("gather_nodes_pruned", count(gather.nodesPruned))
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
			.Field("effect_draws_bound", count(_probeCounters.effectDraws))
			.Field("composite_draws_bound", count(_probeCounters.compositeDraws))
			.Field("dflight_draws_bound", count(_probeCounters.lightDraws))
			.Field("tiled_dispatches_bound", count(_probeCounters.tiledDispatches))
			.Field("shadow_vis_draws_bound", count(_probeCounters.shadowVisDraws))
			.Field("sun_copies", count(_cascadeCounters.copies))
			.Field("sun_copy_skipped_unavailable", cascadeSkipped(CascadeSkip::kUnavailable))
			.Field("sun_copy_skipped_not_full_sky", cascadeSkipped(CascadeSkip::kNotFullSky))
			.Field("sun_copy_skipped_no_light", statusSkipped(cs::engine::SunCascadeStatus::kNoLight))
			.Field("sun_copy_skipped_unsupported_count", statusSkipped(cs::engine::SunCascadeStatus::kUnsupportedCount))
			.Field("sun_copy_skipped_no_target", statusSkipped(cs::engine::SunCascadeStatus::kNoTarget))
			.Field("sun_copy_skipped_invalid", statusSkipped(cs::engine::SunCascadeStatus::kInvalid))
			.Field("sun_copy_skipped_copy_target", cascadeSkipped(CascadeSkip::kCopyTarget))
			.Field("sun_copy_skipped_disabled", cascadeSkipped(CascadeSkip::kDisabled))
			.Field("cascade_count", static_cast<std::int64_t>(_cascadeCounters.count.load(std::memory_order_relaxed)))
			.Field("split_end_0", static_cast<double>(_cascadeCounters.splitEnd[0].load(std::memory_order_relaxed)))
			.Field("split_end_1", static_cast<double>(_cascadeCounters.splitEnd[1].load(std::memory_order_relaxed)))
			.Field("split_end_2", static_cast<double>(_cascadeCounters.splitEnd[2].load(std::memory_order_relaxed)))
			.Field("cascade_overlap", static_cast<double>(_cascadeCounters.overlap.load(std::memory_order_relaxed)));
		cs::render::profiling::CollectPassTimings(a_sink, "Skylighting::");
	}

	void Skylighting::WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB& a_fo4)
	{
		const auto* graphics = cs::engine::GetGraphicsState();
		if (!graphics)
			return;
		// FO4: packs repeat within a frame and the advance must happen once.
		if (_grid.frame == graphics->frameCount) {
			a_feature.skylightingSettings = _grid.block;
			a_fo4.EnabledSkylighting = 1u;
			return;
		}
		// FO4: the captured world camera replaces Util::GetEyePosition.
		const auto camera = cs::engine::GetCapturedWorldCameraRecord(graphics->frameCount);
		if (!camera)
			return;

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
		const auto& anchor = camera->PosAdjust;
		const auto arrayOrigin = [&](float a_cell, std::size_t a_axis) {
			return (static_cast<std::uint32_t>(static_cast<int>(a_cell)) - kProbeArrayDims[a_axis] / 2) % kProbeArrayDims[a_axis];
		};
		_grid.block = {
			.OcclusionViewProj = OcclusionTransform,
			.OcclusionSHBasis4Pi = OcclusionSHBasis4Pi,
			.PosOffset = { cellOrigin.x - anchor.x, cellOrigin.y - anchor.y, cellOrigin.z - anchor.z, 0.0f },
			.ArrayOrigin = { arrayOrigin(cellID.x, 0), arrayOrigin(cellID.y, 1), arrayOrigin(cellID.z, 2), 0 },
			.ValidMargin = { static_cast<int>(cellIDDiff.x), static_cast<int>(cellIDDiff.y), static_cast<int>(cellIDDiff.z), 0 },
			.MinDiffuseVisibility = _settings.MinDiffuseVisibility,
			.MinSpecularVisibility = _settings.MinSpecularVisibility
		};
		_grid.frame = graphics->frameCount;
		a_feature.skylightingSettings = _grid.block;
		a_fo4.EnabledSkylighting = 1u;
	}

	void Skylighting::ApplyQueuedReset()
	{
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
	}

	void Skylighting::Prepass()
	{
		// FO4 captures occlusion before the world-frame reset is latched.
		if (cs::render::IsHistoryResetFrame()) {
			QueueReset(kResetLoad);
			if (CaptureReadiness() == CaptureState::kCaptured)
				ApplyQueuedReset();
		}
		auto state = ProbeState::kDispatched;
		if (!IsEnabled()) {
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

	bool Skylighting::EnsureCascadeCopy(ID3D11Device* a_device, ID3D11Texture2D* a_source, std::uint32_t a_count)
	{
		D3D11_TEXTURE2D_DESC source{};
		a_source->GetDesc(&source);
		if (source.Format != DXGI_FORMAT_R16_TYPELESS || source.ArraySize != a_count || source.MipLevels != 1 || source.SampleDesc.Count != 1)
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
		if (!IsEnabled())
			return CascadeSkip::kDisabled;
		if (!cs::engine::IsFullSky())
			return CascadeSkip::kNotFullSky;

		const auto& lights = cs::engine::GetSunShadowLightState();
		const auto& snapshot = lights.snapshot;
		_sunStatus = lights.status;
		if (_sunStatus != cs::engine::SunCascadeStatus::kOk)
			return CascadeSkip::kSunCascades;

		if (!EnsureCascadeCopy(device, snapshot.texture, snapshot.count))
			return CascadeSkip::kCopyTarget;
		cs::engine::CopyResourcePreservingOM(a_context, _cascadeCopy.get(), snapshot.texture);
		_copyGeneration = lights.generation;

		_cascadeCounters.count.store(snapshot.count, std::memory_order_relaxed);
		_cascadeCounters.overlap.store(snapshot.overlap, std::memory_order_relaxed);
		for (std::size_t i = 0; i < _cascadeCounters.splitEnd.size(); ++i)
			_cascadeCounters.splitEnd[i].store(snapshot.splitEnd[std::min<std::size_t>(i, snapshot.count - 1)], std::memory_order_relaxed);
		if (!std::exchange(_loggedCascadeDesc, true)) {
			// The cached matrix already carries the cascade sub-rect in its UV scale.
			for (std::size_t i = 0; i < snapshot.count; ++i) {
				const auto& m = snapshot.worldToShadow[i].m;
				const float uScale = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
				const float vScale = std::sqrt(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]);
				const float texel = snapshot.worldUnitsPerTexel[i];
				CaptureLog->info("sun cascade {}: split_end={:.1f} overlap={:.1f} units_per_texel={:.3f} uv_scale_ratio=({:.4f},{:.4f}) viewport=({},{},{},{})",
					i, snapshot.splitEnd[i], snapshot.overlap, texel, uScale * texel * _cascadeDesc.Width, vScale * texel * _cascadeDesc.Height,
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
			const bool forwarded = skip == CascadeSkip::kSunCascades;
			auto& counter = forwarded ? _cascadeCounters.statusSkipped[static_cast<std::size_t>(_sunStatus)] : _cascadeCounters.skipped[static_cast<std::size_t>(skip)];
			counter.fetch_add(1, std::memory_order_relaxed);
			// A skipped frame must not leave stale cascades beside live probe updates.
			_copyGeneration = 0;
		}
		const auto name = skip == CascadeSkip::kSunCascades ? cs::engine::SunCascadeStatusName(_sunStatus) : kCascadeSkipNames[static_cast<std::size_t>(skip)];
		if (std::exchange(_loggedCascadeSkip, name) != name)
			CaptureLog->info("sun cascades: {}", name);
	}

	void Skylighting::DispatchProbeUpdate(ID3D11DeviceContext* a_context)
	{
		cs::engine::ComputeOMScope scope(a_context, 4, 1, 4, 0);
		cs::render::ScopedComputeSharedDataBinding substrate(a_context);

		// FO4: no ESRAM shadow, so t3 repeats the copy; stale copies bind null.
		const auto& lights = cs::engine::GetSunShadowLightState();
		const bool fresh = lights.published && lights.generation == _copyGeneration;
		auto* cascades = fresh ? _cascadeCopySRV.get() : nullptr;
		std::array<ID3D11ShaderResourceView*, 4> srvs = {
			_occlusionMap.Target().depthSRV,
			cascades,
			fresh ? cs::engine::GetSunShadowLightsSRV<cs::engine::kMaxSunCascades>() : nullptr,
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
		const auto field = edit.Bind(skylighting::kSchema, _settings);

		if (const ui::Section section{ "skylighting-visibility", "Visibility" }; section) {
			dmui::ui::Text("%s", "Minimum visibility values. Diffuse darkens objects. Specular removes the sky from reflections.");
			field(&Settings::MinDiffuseVisibility, { .format = "%.2f" });
			field(&Settings::MinSpecularVisibility, { .format = "%.2f" });
		}
		if (const ui::Section section{ "skylighting-performance", "Performance" }; section)
			field(&Settings::BatchOccluders);
		if (const ui::Section section{ "skylighting-rebuild", "Rebuild" }; section) {
			if (dmui::ui::Button("Rebuild Skylighting"))
				QueueReset(kResetRebuild);

			ui::Tooltip("Changes below require rebuilding, a loading screen, or moving away from the current location to apply.");

			// Stored in radians; the slider edits degrees like upstream's SliderAngle.
			field(&Settings::MaxZenith, { .format = "%.0f deg", .flags = dmui::ui::SliderFlags::kAlwaysClamp, .scale = kRadiansToDegrees });
		}
	}

	void Skylighting::DrawDebug()
	{
		// Runtime only: not part of the persisted settings.
		auto interval = _batchValidationInterval.load(std::memory_order_relaxed);
		const auto edit = ui::EditSetting(
			{ "skylighting-batch-validation-interval", { .label = "Batch Validation Interval", .help = "Every Nth capture also renders the batchable occluders through the engine and the batch and logs the texels that differ. 0 is off." } },
			interval, std::uint32_t{ 0 }, settings::Range<std::uint32_t>{ 0, 3600 });
		if (edit.changed)
			_batchValidationInterval.store(interval, std::memory_order_relaxed);
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
