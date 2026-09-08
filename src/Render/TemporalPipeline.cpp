#include "Render/TemporalPipeline.h"
#include "Render/TemporalDevicePolicy.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <format>
#include <mutex>
#include <numbers>
#include <utility>

#include "Feature.h"
#include "FidelityFX.h"
#include "FrameGeneration.h"
#include "DX12SwapChain.h"
#include "PresentationProviders.h"
#include "Log.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/RendererContext.h"
#include "Render/TemporalPipelineAnchors.h"
#include "Render/TemporalRenderer.h"
#include "SuperResolutionProviders.h"
#include "Streamline.h"
#include "Upscaling.h"
#include "XeSS.h"

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.temporalpipeline");

		bool IsCallSiteTargeting(
			std::uintptr_t a_site,
			std::uintptr_t a_expectedTarget) noexcept
		{
			if (!a_site || !a_expectedTarget) {
				return false;
			}
			const auto* bytes =
				reinterpret_cast<const std::uint8_t*>(a_site);
			if (bytes[0] != 0xE8) {
				return false;
			}
			std::int32_t displacement = 0;
			std::memcpy(&displacement, bytes + 1, sizeof(displacement));
			return a_site + 5 + static_cast<std::intptr_t>(displacement) ==
				a_expectedTarget;
		}

		struct MainLoop_WindowsMessageLoop
		{
			static void thunk(RE::Main* a_main)
			{
				auto& pipeline = TemporalPipeline::Get();
				pipeline.BeginMainLoopFrame();
				func(a_main);
				pipeline.BeginSimulation();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct OnIdle_Swap
		{
			static void thunk(RE::Main* a_main)
			{
				TemporalPipeline::Get().EndSimulationAndBeginRenderSubmit();
				func(a_main);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		double GetRefreshRate(HWND a_window)
		{
			const HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
			MONITORINFOEXW monitorInfo{};
			monitorInfo.cbSize = sizeof(monitorInfo);
			if (!GetMonitorInfoW(monitor, &monitorInfo)) {
				return 0.0;
			}
			DEVMODEW mode{};
			mode.dmSize = sizeof(mode);
			if (!EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
				return 0.0;
			}
			return mode.dmDisplayFrequency > 1
				? static_cast<double>(mode.dmDisplayFrequency)
				: 0.0;
		}

		temporal::SuperResolutionMethod ToCore(
			features::Upscaling::UpscaleMethod a_method) noexcept
		{
			using Source = features::Upscaling::UpscaleMethod;
			using Target = temporal::SuperResolutionMethod;
			switch (a_method) {
			case Source::kNONE:
				return Target::kNone;
			case Source::kTAA:
				return Target::kTAA;
			case Source::kFSR:
				return Target::kFSR3;
			case Source::kDLSS:
				return Target::kDLSS;
			case Source::kXeSS:
				return Target::kXeSS;
			}
			return Target::kNone;
		}

		temporal::FrameGenerationMethod ToCore(
			features::FrameGeneration::Method a_method) noexcept
		{
			using Source = features::FrameGeneration::Method;
			using Target = temporal::FrameGenerationMethod;
			switch (a_method) {
			case Source::kOff:
				return Target::kOff;
			case Source::kFSR3:
				return Target::kFSR3;
			case Source::kDLSSG:
				return Target::kDLSSG;
			case Source::kXeSS:
				return Target::kXeSS;
			}
			return Target::kOff;
		}
	}

	struct TemporalPipeline::Impl
	{
		static constexpr std::size_t kTraceCapacity = 64;

		mutable std::mutex mutex;
		TemporalRenderer renderer;
		temporal::UpscalingSettings requestedRenderSettings;
		bool rendererEligible = false;
		bool frameGenerationQuarantined = false;
		temporal::TopologyState topology;
		temporal::DisplayState display;
		temporal::ResetEpochs resetEpochs;
		temporal::FailureDomain failureDomain = temporal::FailureDomain::kNone;
		std::string failure;
		std::atomic<TemporalCreationState> creationState{ TemporalCreationState::kUnregistered };
		std::atomic_bool requestFrozen{ false };
		std::atomic_bool d3d11Ready{ false };
		std::atomic_bool frameGenerationEnabled{ false };
		std::atomic_bool allowFrameGenerationInMenus{ false };
		std::atomic_uint64_t configurationRevision{ 1 };
		temporal::LatencyTimeline latency;
		std::atomic_bool latencyHooksInstalled{ false };
		std::atomic_bool latencySdkActive{ false };
		std::atomic_uint64_t generatedFrames{ 0 };
		std::atomic_bool generatedFrameCountAvailable{ false };
		std::atomic_bool inputsCaptured{ false };
		std::atomic_bool hudlessCapturePending{ false };
		std::atomic_bool alphaConditioned{ false };
		std::atomic_uint64_t conditionedCaptures{ 0 };
		std::atomic_uint64_t rawCaptures{ 0 };
		std::atomic_uint64_t frameGenerationDispatches{ 0 };
		std::atomic_uint64_t frameGenerationFailures{ 0 };
		std::array<temporal::FrameTransaction, 2> frames;
		std::uint32_t currentFrameSlot = 0;
		std::array<TemporalTraceEntry, kTraceCapacity> trace;
		std::uint64_t traceSequence = 0;
		std::size_t traceCount = 0;
		bool detailedTracing = false;
		features::Streamline streamline;
		features::FidelityFX fidelityFX;
		features::XeSSSuperResolution xess;
		features::StreamlineSuperResolution dlssProvider{ streamline };
		features::FidelityFXSuperResolution fsrProvider{ fidelityFX };
		features::FidelityFXPresentation fidelityFXPresentation{ fidelityFX };
		features::StreamlinePresentation streamlinePresentation{ streamline };
		features::XeSSPresentation xessPresentation;
		temporal::IFrameGenerationProvider* activePresentation = nullptr;
		features::DX12SwapChain swapChain;

		void Trace(
			TemporalTraceEvent a_event,
			const temporal::FrameTransaction& a_frame,
			HRESULT a_result = S_OK) noexcept
		{
			if (!detailedTracing) {
				return;
			}
			const auto sequence = ++traceSequence;
			trace[(sequence - 1) % trace.size()] = {
				.sequence = sequence,
				.realFrame = a_frame.Identity().realFrame,
				.engineFrame = a_frame.Identity().engineFrame,
				.slot = a_frame.Identity().slot,
				.event = a_event,
				.result = a_result
			};
			traceCount = std::min(traceCount + 1, trace.size());
		}
	};

	TemporalPipeline& TemporalPipeline::Get()
	{
		static auto* instance = new TemporalPipeline();
		return *instance;
	}

	TemporalPipeline::TemporalPipeline() :
		_impl(std::make_unique<Impl>())
	{}

	TemporalPipeline::~TemporalPipeline() = default;

	bool TemporalPipeline::InstallLatencyHooks() noexcept
	{
		using namespace temporal_anchors;

		if (_impl->latencyHooksInstalled.load(std::memory_order_acquire)) {
			return true;
		}
		try {
			const auto runtimeIndex =
				static_cast<std::size_t>(REX::FModule::GetRuntimeIndex());
			const auto messageLoopSite =
				REL::ID({
					kMainLoopAnchor[0],
					kMainLoopAnchor[1],
					kMainLoopAnchor[2] })
					.address() +
				kMainLoopMessageLoopCall[runtimeIndex];
			const auto messageLoopTarget =
				REL::ID({
					kWindowsMessageLoop[0],
					kWindowsMessageLoop[1],
					kWindowsMessageLoop[2] })
					.address();
			const auto swapSite =
				REL::ID({ kOnIdle[0], kOnIdle[1], kOnIdle[2] }).address() +
				kOnIdleSwapCall[runtimeIndex];
			const auto swapTarget =
				REL::ID({ kSwap[0], kSwap[1], kSwap[2] }).address();
			if (!IsCallSiteTargeting(messageLoopSite, messageLoopTarget) ||
				!IsCallSiteTargeting(swapSite, swapTarget)) {
				PostFailure(
					temporal::FailureDomain::kEngine,
					"Normal main-loop latency callsites did not match their validated targets.");
				return false;
			}

			stl::write_thunk_call<MainLoop_WindowsMessageLoop>(
				messageLoopSite);
			stl::write_thunk_call<OnIdle_Swap>(swapSite);
			_impl->latencyHooksInstalled.store(
				true, std::memory_order_release);
			L->info("Installed validated normal-loop latency hooks");
			return true;
		} catch (const std::exception& e) {
			PostFailure(
				temporal::FailureDomain::kEngine,
				std::format(
					"Latency hook installation failed: {}", e.what()));
		} catch (...) {
			PostFailure(
				temporal::FailureDomain::kEngine,
				"Latency hook installation failed.");
		}
		return false;
	}

	void TemporalPipeline::RegisterCreationRouter()
	{
		auto expected = TemporalCreationState::kUnregistered;
		if (!_impl->creationState.compare_exchange_strong(
				expected,
				TemporalCreationState::kRegistered,
				std::memory_order_acq_rel)) {
			return;
		}

		const bool preRegistered = RegisterPreCreateDeviceAndSwapChain(
			[this](DXGI_SWAP_CHAIN_DESC* a_desc, std::vector<D3D_FEATURE_LEVEL>& a_levels) {
				OnPreCreateDeviceAndSwapChain(a_desc, a_levels);
			});
		const bool postRegistered = RegisterPostCreateDeviceAndSwapChain(
			[this](IDXGIAdapter* a_adapter, ID3D11Device** a_device, IDXGISwapChain** a_swapChain) {
				OnPostCreateDeviceAndSwapChain(a_adapter, a_device, a_swapChain);
			});
		const bool replacementRegistered = RegisterReplacementCreateDeviceAndSwapChain(
				[this](CreateDeviceAndSwapChainContext& a_context) {
					return OnReplacementCreateDeviceAndSwapChain(a_context);
				});
		if (!preRegistered || !postRegistered || !replacementRegistered) {
			_impl->creationState.store(TemporalCreationState::kFailed, std::memory_order_release);
			PostFailure(
				temporal::FailureDomain::kConfiguration,
				"The swap-chain replacement route is already owned.");
			L->error("Temporal pipeline could not register the sole swap-chain creation router");
			return;
		}
		L->info("Temporal pipeline registered the sole swap-chain creation router");
	}

	void TemporalPipeline::FreezeRequest()
	{
		auto* upscaling = features::Upscaling::GetSingleton();
		auto* frameGeneration = features::FrameGeneration::GetSingleton();

		temporal::RequestedTopology requested;
		requested.upscalingEligible = upscaling && upscaling->GetState().IsActive();
		requested.frameGenerationEligible =
			frameGeneration && frameGeneration->GetState().IsActive();
		if (upscaling) {
			_impl->requestedRenderSettings = upscaling->settings;
			requested.superResolutionEnabled = upscaling->settings.enabled;
			requested.superResolution = ToCore(
				static_cast<features::Upscaling::UpscaleMethod>(
					upscaling->settings.upscaleMethod));
			requested.noDlssFallback = ToCore(
				static_cast<features::Upscaling::UpscaleMethod>(
					upscaling->settings.upscaleMethodNoDLSS));
			requested.qualityMode = upscaling->settings.qualityMode;
			requested.streamlineLogLevel = upscaling->settings.streamlineLogLevel;
		}
		if (frameGeneration) {
			requested.frameGenerationEnabled = frameGeneration->settings.enabled;
			requested.frameGeneration = ToCore(
				static_cast<features::FrameGeneration::Method>(
					frameGeneration->settings.frameGenerationMethod));
			requested.forceFrameGeneration =
				frameGeneration->settings.frameGenerationForceEnable != 0;
			requested.allowFrameGenerationInMenus =
				frameGeneration->settings.frameGenerationAllowInMenus;
		}
		requested.revision =
			_impl->configurationRevision.fetch_add(1, std::memory_order_acq_rel);

		bool frozen = false;
		{
			std::scoped_lock lock(_impl->mutex);
			frozen = _impl->topology.Freeze(requested);
		}
		if (!frozen) {
			PostFailure(
				temporal::FailureDomain::kConfiguration,
				"The temporal request was frozen more than once.");
			return;
		}
		_impl->frameGenerationEnabled.store(
			requested.frameGenerationEnabled,
			std::memory_order_release);
		_impl->allowFrameGenerationInMenus.store(
			requested.allowFrameGenerationInMenus,
			std::memory_order_release);
		_impl->requestFrozen.store(true, std::memory_order_release);
		_impl->creationState.store(TemporalCreationState::kFrozen, std::memory_order_release);
		_impl->rendererEligible = requested.upscalingEligible;
		_impl->renderer.ApplyConfiguration(
			_impl->requestedRenderSettings, _impl->rendererEligible);
		if (requested.upscalingEligible || requested.frameGenerationEligible) {
			InstallLatencyHooks();
			_impl->renderer.InstallTemporalHooks();
		}
		L->info(
			"Temporal request frozen: SR={} eligible={}, FG={} eligible={}",
			static_cast<unsigned>(requested.superResolution),
			requested.upscalingEligible,
			static_cast<unsigned>(requested.frameGeneration),
			requested.frameGenerationEligible);
	}

	void TemporalPipeline::SubmitLiveConfiguration()
	{
		auto* upscaling = features::Upscaling::GetSingleton();
		auto* frameGeneration = features::FrameGeneration::GetSingleton();
		if (!upscaling || !frameGeneration) {
			return;
		}
		const auto revision =
			_impl->configurationRevision.fetch_add(1, std::memory_order_acq_rel);
		std::scoped_lock lock(_impl->mutex);
		_impl->requestedRenderSettings = upscaling->settings;
		_impl->topology.SubmitLive(
			upscaling->settings.enabled,
			ToCore(static_cast<features::Upscaling::UpscaleMethod>(
				upscaling->settings.upscaleMethod)),
			upscaling->settings.qualityMode,
			frameGeneration->settings.enabled,
			ToCore(static_cast<features::FrameGeneration::Method>(
				frameGeneration->settings.frameGenerationMethod)),
			revision);
		_impl->frameGenerationEnabled.store(
			_impl->topology.Effective().frameGenerationEnabled &&
				!_impl->frameGenerationQuarantined,
			std::memory_order_release);
		_impl->allowFrameGenerationInMenus.store(
			frameGeneration->settings.frameGenerationAllowInMenus,
			std::memory_order_release);
	}

	void TemporalPipeline::SetDetailedTracing(bool a_enabled) noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		_impl->detailedTracing = a_enabled;
		if (!a_enabled) {
			_impl->traceCount = 0;
		}
	}

	void TemporalPipeline::BeginMainLoopFrame() noexcept
	{
		std::uint64_t frame = 0;
		temporal::UpscalingSettings renderSettings;
		bool eligible = false;
		{
			std::scoped_lock lock(_impl->mutex);
			frame = _impl->latency.BeginFrame();
			renderSettings = _impl->requestedRenderSettings;
			renderSettings.qualityMode = _impl->topology.Effective().qualityMode;
			eligible = _impl->rendererEligible;
		}
		_impl->renderer.ApplyConfiguration(renderSettings, eligible);
		cs::engine::RefreshFrameBufferContextHooks();
		if (_impl->latencySdkActive.load(std::memory_order_acquire)) {
			(void)_impl->activePresentation->Sleep(
				static_cast<std::uint32_t>(frame));
		}
		cs::render::annotation::SetMarker(
			"Temporal/Latency/Sleep");
	}

	void TemporalPipeline::BeginSimulation() noexcept
	{
		std::uint64_t frame = 0;
		{
			std::scoped_lock lock(_impl->mutex);
			if (!_impl->latency.BeginSimulation()) {
				return;
			}
			frame = _impl->latency.Frame();
		}
		if (_impl->latencySdkActive.load(std::memory_order_acquire)) {
			(void)_impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kInputSample,
				static_cast<std::uint32_t>(frame));
			(void)_impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kSimulationStart,
				static_cast<std::uint32_t>(frame));
		}
		cs::render::annotation::SetMarker(
			"Temporal/Latency/InputSample");
		cs::render::annotation::SetMarker(
			"Temporal/Latency/SimulationStart");
	}

	void TemporalPipeline::EndSimulationAndBeginRenderSubmit() noexcept
	{
		std::uint64_t frame = 0;
		{
			std::scoped_lock lock(_impl->mutex);
			if (!_impl->latency.EndSimulationAndBeginRenderSubmit()) {
				return;
			}
			frame = _impl->latency.Frame();
		}
		if (_impl->latencySdkActive.load(std::memory_order_acquire)) {
			(void)_impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kSimulationEnd,
				static_cast<std::uint32_t>(frame));
			(void)_impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kRenderSubmitStart,
				static_cast<std::uint32_t>(frame));
		}
		cs::render::annotation::SetMarker(
			"Temporal/Latency/SimulationEnd");
		cs::render::annotation::SetMarker(
			"Temporal/Latency/RenderSubmitStart");
	}

	void TemporalPipeline::BeginPresentAttempt(UINT a_flags) noexcept
	{
		const bool testOnly = (a_flags & DXGI_PRESENT_TEST) != 0;
		bool renderSubmitEnd = false;
		std::uint64_t frame = 0;
		{
			std::scoped_lock lock(_impl->mutex);
			renderSubmitEnd =
				_impl->latency.Phase() == temporal::LatencyPhase::kRenderSubmit;
			if (!_impl->latency.BeginPresent(testOnly) || testOnly) {
				return;
			}
			frame = _impl->latency.Frame();
		}
		if (renderSubmitEnd) {
			if (_impl->latencySdkActive.load(std::memory_order_acquire)) {
				(void)_impl->activePresentation->SetLatencyMarker(
					temporal::LatencyMarker::kRenderSubmitEnd,
					static_cast<std::uint32_t>(frame));
			}
			cs::render::annotation::SetMarker(
				"Temporal/Latency/RenderSubmitEnd");
		}
		if (_impl->latencySdkActive.load(std::memory_order_acquire)) {
			(void)_impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kPresentStart,
				static_cast<std::uint32_t>(frame));
		}
		cs::render::annotation::SetMarker(
			"Temporal/Latency/PresentStart");
	}

	void TemporalPipeline::EndPresentAttempt(
		UINT a_flags,
		HRESULT a_result) noexcept
	{
		const bool testOnly = (a_flags & DXGI_PRESENT_TEST) != 0;
		const bool retryable = a_result == DXGI_ERROR_WAS_STILL_DRAWING;
		std::uint64_t frame = 0;
		{
			std::scoped_lock lock(_impl->mutex);
			if (!_impl->latency.EndPresent(testOnly, retryable) || testOnly) {
				return;
			}
			frame = _impl->latency.Frame();
		}
		if (_impl->latencySdkActive.load(std::memory_order_acquire)) {
			(void)_impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kPresentEnd,
				static_cast<std::uint32_t>(frame));
		}
		cs::render::annotation::SetMarker(
			"Temporal/Latency/PresentEnd");
	}

	std::uint64_t TemporalPipeline::CurrentRealFrame() const noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		return _impl->latency.Frame();
	}

	void TemporalPipeline::OnDataLoaded()
	{
		std::scoped_lock lock(_impl->mutex);
		const auto& request = _impl->topology.Request();
		if (request &&
			(request->upscalingEligible || request->frameGenerationEligible)) {
			_impl->renderer.InstallTemporalMenuListener();
		}
	}

	void TemporalPipeline::OnPreCreateDeviceAndSwapChain(
		DXGI_SWAP_CHAIN_DESC*,
		std::vector<D3D_FEATURE_LEVEL>& a_featureLevels)
	{
		if (!_impl->requestFrozen.load(std::memory_order_acquire)) {
			_impl->creationState.store(TemporalCreationState::kNative, std::memory_order_release);
			PostFailure(
				temporal::FailureDomain::kConfiguration,
				"Graphics creation arrived before the post-preset temporal request freeze.");
			return;
		}

		temporal::RequestedTopology request;
		{
			std::scoped_lock lock(_impl->mutex);
			request = *_impl->topology.Request();
		}
		const bool streamlineRequested =
			(request.upscalingEligible &&
			 request.superResolution == temporal::SuperResolutionMethod::kDLSS) ||
			(request.frameGenerationEligible &&
			 request.frameGeneration == temporal::FrameGenerationMethod::kDLSSG);
		temporal::ConfigureTemporalFeatureLevels(request, a_featureLevels);
		if (streamlineRequested) {
			const bool dlssRequested =
				request.upscalingEligible &&
				request.superResolution ==
					temporal::SuperResolutionMethod::kDLSS;
			const bool dlssGRequested =
				request.frameGenerationEligible &&
				request.frameGeneration ==
					temporal::FrameGenerationMethod::kDLSSG;
			_impl->streamline.LoadInterposer(
				request.streamlineLogLevel,
				dlssRequested,
				dlssGRequested,
				dlssGRequested
					? sl::RenderAPI::eD3D12
					: sl::RenderAPI::eD3D11);
		}
		if (request.frameGenerationEligible &&
			request.frameGeneration == temporal::FrameGenerationMethod::kFSR3) {
			_impl->fidelityFX.LoadFrameGeneration();
		}
	}

	std::optional<HRESULT> TemporalPipeline::OnReplacementCreateDeviceAndSwapChain(
		CreateDeviceAndSwapChainContext& a_context)
	{
		if (!_impl->requestFrozen.load(std::memory_order_acquire)) {
			return std::nullopt;
		}

		temporal::RequestedTopology request;
		{
			std::scoped_lock lock(_impl->mutex);
			request = *_impl->topology.Request();
		}
		if (!request.frameGenerationEligible ||
			request.frameGeneration == temporal::FrameGenerationMethod::kOff) {
			return std::nullopt;
		}
		if (!a_context.realCreate || !a_context.swapChainDesc || !a_context.swapChain ||
			!a_context.device || !a_context.immediateContext) {
			PostFailure(
				temporal::FailureDomain::kTransport,
				"The swap-chain replacement callback received an incomplete creation request.");
			return std::nullopt;
		}
		if (!a_context.swapChainDesc->Windowed) {
			PostFailure(
				temporal::FailureDomain::kTransport,
				"FSR 3 frame generation requires windowed or borderless presentation.");
			return std::nullopt;
		}
		if (request.frameGeneration == temporal::FrameGenerationMethod::kFSR3 &&
			!_impl->fidelityFX.IsFrameGenerationModuleReady()) {
			PostFailure(
				temporal::FailureDomain::kFrameGeneration,
				"The staged FidelityFX frame-generation runtime is unavailable.");
			return std::nullopt;
		}

		const double refreshRate = GetRefreshRate(a_context.swapChainDesc->OutputWindow);
		if (refreshRate < 120.0 && !request.forceFrameGeneration) {
			PostFailure(
				temporal::FailureDomain::kFrameGeneration,
				std::format(
					"FSR 3 frame generation requires 120 Hz or the explicit force policy; observed {:.2f} Hz.",
					refreshRate));
			return std::nullopt;
		}

		_impl->creationState.store(TemporalCreationState::kCreating, std::memory_order_release);
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* immediateContext = nullptr;
		D3D_FEATURE_LEVEL featureLevel{};
		const HRESULT deviceResult = a_context.realCreate(
			a_context.adapter,
			a_context.driverType,
			a_context.software,
			a_context.flags,
			a_context.featureLevels,
			a_context.featureLevelCount,
			a_context.sdkVersion,
			nullptr,
			nullptr,
			&device,
			&featureLevel,
			&immediateContext);
		if (FAILED(deviceResult) || !device || !immediateContext) {
			if (immediateContext) {
				immediateContext->Release();
			}
			if (device) {
				device->Release();
			}
			_impl->creationState.store(TemporalCreationState::kNative, std::memory_order_release);
			PostFailure(
				temporal::FailureDomain::kTransport,
				std::format(
					"Frame-generation proxy device creation failed ({:#010x}); native creation remains safe.",
					static_cast<std::uint32_t>(deviceResult)));
			return std::nullopt;
		}

		HRESULT proxyResult = E_FAIL;
		const std::string_view providerName =
			request.frameGeneration ==
					temporal::FrameGenerationMethod::kDLSSG
				? "DLSS-G"
			: request.frameGeneration ==
					temporal::FrameGenerationMethod::kXeSS
				? "XeSS-FG"
				: "FSR 3";
		try {
			auto& provider =
				request.frameGeneration ==
						temporal::FrameGenerationMethod::kDLSSG
					? static_cast<temporal::IFrameGenerationProvider&>(
						_impl->streamlinePresentation)
				: request.frameGeneration ==
						temporal::FrameGenerationMethod::kXeSS
					? static_cast<temporal::IFrameGenerationProvider&>(
						_impl->xessPresentation)
					: static_cast<temporal::IFrameGenerationProvider&>(
						_impl->fidelityFXPresentation);
			proxyResult = _impl->swapChain.Initialize(
				a_context.adapter,
				device,
				immediateContext,
				*a_context.swapChainDesc,
				provider,
				features::TemporalPresentationCallbacks{
					.clearCapture = [] {
						TemporalPipeline::Get().Renderer()
							.ClearFrameGenerationCaptureState();
					},
					.recordFailure = [] {
						TemporalPipeline::Get()
							.RecordFrameGenerationFailure();
					},
					.queryFrameState = [] {
						const auto* upscaling =
							&TemporalPipeline::Get().Renderer();
						const auto [width, height] = upscaling->GetRenderSize();
						const auto jitter = upscaling->GetAppliedJitter();
						features::FrameGenerationFrameState result{
							.realFrame =
								TemporalPipeline::Get().CurrentRealFrame(),
							.renderWidth = width,
							.renderHeight = height,
							.jitterX = jitter.x,
							.jitterY = jitter.y,
							.frameTimeMilliseconds =
								RE::BSTimer::GetSingleton()
									? RE::BSTimer::GetSingleton()->realTimeDelta *
										1000.0f
									: 0.0f,
							.enable =
								upscaling->ShouldUseFrameGenerationThisFrame(),
							.resetHistory =
								TemporalPipeline::Get()
									.ArmFrameGenerationReset(),
							.color = {
								.resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
								.viewFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
								.range = features::ColorRange::kFull,
								.transfer = features::TransferFunction::kGamma22,
								.primaries =
									features::ColorPrimaries::kUnspecified,
								.stage =
									features::ColorStage::kPostTonemapLut,
								.alpha = features::AlphaMode::kIgnored,
								.exposure = features::ExposureMode::kAutomatic
							}
						};
						const auto& snapshot = cs::engine::GetFrameBuffer();
						if (snapshot.valid) {
							auto& camera = result.camera;
							std::memcpy(
								camera.currentWorldToClip,
								snapshot.data.CurrFrameWorldToClip,
								sizeof(camera.currentWorldToClip));
							std::memcpy(
								camera.previousWorldToClip,
								snapshot.data.PrevFrameWorldToClip,
								sizeof(camera.previousWorldToClip));
							for (std::size_t row = 0; row < 3; ++row) {
								std::memcpy(
								camera.viewToWorld + row * 4,
								&snapshot.data.ViewToWorld[row],
								sizeof(float) * 4);
							}
							camera.viewToWorld[15] = 1.0f;
							const auto position =
								cs::engine::CameraWorldOrigin(snapshot.data);
							const auto previousPosition =
								cs::engine::CameraPreviousWorldOrigin(
								snapshot.data);
							const auto basis =
								cs::engine::GetCameraWorldBasis(snapshot.data);
							const float fov =
								cs::engine::VerticalFieldOfViewFromWorldToClip(
								snapshot.data.CurrFrameWorldToClip);
							camera.position[0] = position.x;
							camera.position[1] = position.y;
							camera.position[2] = position.z;
							camera.previousPosition[0] = previousPosition.x;
							camera.previousPosition[1] = previousPosition.y;
							camera.previousPosition[2] = previousPosition.z;
							camera.right[0] = basis.right.x;
							camera.right[1] = basis.right.y;
							camera.right[2] = basis.right.z;
							camera.up[0] = basis.up.x;
							camera.up[1] = basis.up.y;
							camera.up[2] = basis.up.z;
							camera.forward[0] = basis.forward.x;
							camera.forward[1] = basis.forward.y;
							camera.forward[2] = basis.forward.z;
							camera.nearPlane = cs::engine::GetCameraNear();
							camera.farPlane = cs::engine::GetCameraFar();
							camera.verticalFov = fov;
							const auto* graphics =
								cs::engine::GetGraphicsState();
							camera.aspectRatio =
								graphics && graphics->screenHeight
								? static_cast<float>(graphics->screenWidth) /
									static_cast<float>(graphics->screenHeight)
								: 0.0f;
							camera.engineFrame = snapshot.frameCount;
							camera.valid =
								cs::engine::HasUsableWorldCamera(snapshot.data) &&
								fov > 0.0f &&
								camera.nearPlane > 0.0f &&
								camera.farPlane > camera.nearPlane;
						}
						return result;
					}
				});
			if (SUCCEEDED(proxyResult)) {
				_impl->activePresentation = &provider;
			}
		} catch (const std::exception& e) {
			_impl->swapChain.Rollback();
			PostFailure(temporal::FailureDomain::kTransport, e.what());
		} catch (...) {
			_impl->swapChain.Rollback();
			PostFailure(
				temporal::FailureDomain::kTransport,
				"Frame-generation proxy construction raised a non-standard exception.");
		}
		if (FAILED(proxyResult) || !_impl->swapChain.GetProxy()) {
			immediateContext->Release();
			device->Release();
			_impl->creationState.store(TemporalCreationState::kNative, std::memory_order_release);
			if (SUCCEEDED(proxyResult)) {
				proxyResult = E_FAIL;
			}
			L->error(
				"{} proxy construction failed ({:#010x}); native creation remains safe",
				providerName,
				static_cast<std::uint32_t>(proxyResult));
			return std::nullopt;
		}

		*a_context.device = device;
		*a_context.immediateContext = immediateContext;
		*a_context.swapChain = _impl->swapChain.GetProxy();
		if (a_context.featureLevel) {
			*a_context.featureLevel = featureLevel;
		}
		_impl->creationState.store(TemporalCreationState::kProxy, std::memory_order_release);
		L->info(
			"Temporal pipeline published the {} D3D11-facing proxy at {:.2f} Hz",
			providerName,
			refreshRate);
		return S_OK;
	}

	void TemporalPipeline::OnPostCreateDeviceAndSwapChain(
		IDXGIAdapter* a_adapter,
		ID3D11Device** a_device,
		IDXGISwapChain** a_swapChain)
	{
		temporal::RequestedTopology request;
		{
			std::scoped_lock lock(_impl->mutex);
			if (!_impl->topology.Request()) {
				return;
			}
			request = *_impl->topology.Request();
		}

		const bool proxyPath = a_swapChain && _impl->swapChain.Owns(*a_swapChain);
		if (_impl->streamline.initialized &&
			!_impl->streamline.IsD3D12Session()) {
			// Native SR owns no presentation hooks; retain the engine's D3D11 interfaces.
			if (!_impl->streamline.SetDevice(a_device ? *a_device : nullptr)) {
				_impl->streamline.featureDLSS = false;
				_impl->streamline.featurePCL = false;
				_impl->streamline.featureReflex = false;
				PostFailure(
					temporal::FailureDomain::kStreamline,
					"Streamline D3D11 device registration failed; native device and swapchain retained.");
			} else {
				_impl->streamline.CheckFeatures(a_adapter);
				_impl->streamline.PostDevice();
			}
		}
		if (proxyPath && a_device) {
			_impl->swapChain.SetOutwardD3D11Device(*a_device);
		}
		render::temporal::SuperResolutionInitContext d3d11Init{};
		d3d11Init.device = a_device ? *a_device : nullptr;
		const auto fsrInit =
			_impl->fsrProvider.Initialize(d3d11Init);
		render::temporal::SuperResolutionInitContext dlssInit{};
		if (_impl->streamline.IsD3D12Session()) {
			dlssInit.device = _impl->swapChain.GetD3D12Device();
		} else {
			dlssInit.device = a_device ? *a_device : nullptr;
		}
		const auto dlssInitResult =
			_impl->dlssProvider.Initialize(dlssInit);
		bool xessAdmitted = false;
		std::string xessFailure;
		if (request.superResolution ==
			temporal::SuperResolutionMethod::kXeSS) {
			DXGI_ADAPTER_DESC adapterDesc{};
			const bool intelAdapter = a_adapter &&
				SUCCEEDED(a_adapter->GetDesc(&adapterDesc)) &&
				adapterDesc.VendorId == 0x8086;
			render::temporal::SuperResolutionInitContext init{};
			if (!intelAdapter && !proxyPath && a_device && *a_device) {
				winrt::com_ptr<ID3D11DeviceContext> context;
				(*a_device)->GetImmediateContext(context.put());
				const HRESULT bridgeResult =
					_impl->swapChain.InitializeBridge(
						a_adapter, *a_device, context.get());
				if (FAILED(bridgeResult)) {
					xessFailure = std::format(
						"XeSS D3D12 bridge initialization failed ({:#010x}).",
						static_cast<std::uint32_t>(bridgeResult));
				}
			}
			if (intelAdapter && a_device && *a_device) {
				init.device = *a_device;
			} else if (_impl->swapChain.IsBridgeReady() &&
					   _impl->swapChain.GetD3D12Device()) {
				init.device = _impl->swapChain.GetD3D12Device();
			} else {
				xessFailure =
					"XeSS requires native Intel D3D11 or the D3D12 bridge.";
			}
			if (xessFailure.empty()) {
				const auto result = _impl->xess.Initialize(init);
				xessAdmitted = result.Succeeded();
				xessFailure = result.message;
			}
		}
		_impl->latencySdkActive.store(
			_impl->latencyHooksInstalled.load(std::memory_order_acquire) &&
				proxyPath &&
				_impl->activePresentation &&
				request.frameGeneration !=
					temporal::FrameGenerationMethod::kFSR3 &&
				_impl->activePresentation->IsReady(),
			std::memory_order_release);

		temporal::SessionTopology session;
		session.valid = true;
		session.proxyInstalled = proxyPath;
		session.bridgePresent = _impl->swapChain.IsBridgeReady();
		session.latencyHooksInstalled =
			_impl->latencyHooksInstalled.load(std::memory_order_acquire);
		session.streamlineApi = _impl->streamline.IsD3D12Session()
			? temporal::GraphicsApi::kD3D12
			: temporal::GraphicsApi::kD3D11;
		session.admittedSr[static_cast<std::size_t>(temporal::SuperResolutionMethod::kNone)] =
			request.upscalingEligible;
		session.admittedSr[static_cast<std::size_t>(temporal::SuperResolutionMethod::kTAA)] =
			request.upscalingEligible;
		session.admittedSr[static_cast<std::size_t>(temporal::SuperResolutionMethod::kFSR3)] =
			request.upscalingEligible && fsrInit.Succeeded();
		session.admittedSr[static_cast<std::size_t>(temporal::SuperResolutionMethod::kDLSS)] =
			request.upscalingEligible && dlssInitResult.Succeeded();
		session.admittedSr[static_cast<std::size_t>(temporal::SuperResolutionMethod::kXeSS)] =
			request.upscalingEligible && xessAdmitted;
		session.admittedFg = proxyPath
			? request.frameGeneration
			: temporal::FrameGenerationMethod::kOff;
		if (a_adapter) {
			DXGI_ADAPTER_DESC desc{};
			if (SUCCEEDED(a_adapter->GetDesc(&desc))) {
				session.adapterLuid =
					(static_cast<std::uint64_t>(
						static_cast<std::uint32_t>(desc.AdapterLuid.HighPart)) << 32) |
					desc.AdapterLuid.LowPart;
			}
		}

		bool admitted = false;
		{
			std::scoped_lock lock(_impl->mutex);
			admitted = _impl->topology.Admit(std::move(session));
		}
		if (!admitted) {
			PostFailure(
				temporal::FailureDomain::kConfiguration,
				"The temporal session topology was published more than once.");
		} else if (
			request.superResolution == temporal::SuperResolutionMethod::kXeSS &&
			!xessAdmitted) {
			PostFailure(
				temporal::FailureDomain::kSuperResolution,
				xessFailure.empty()
					? "XeSS super-resolution was not admitted."
					: xessFailure);
		} else if (
			request.superResolution == temporal::SuperResolutionMethod::kDLSS &&
			!dlssInitResult.Succeeded()) {
			PostFailure(
				temporal::FailureDomain::kStreamline,
				dlssInitResult.message);
		} else if (
			request.superResolution == temporal::SuperResolutionMethod::kFSR3 &&
			!fsrInit.Succeeded()) {
			PostFailure(
				temporal::FailureDomain::kSuperResolution,
				fsrInit.message);
		}
	}

	void TemporalPipeline::OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device)
	{
		_impl->d3d11Ready.store(true, std::memory_order_release);
		bool active = false;
		{
			std::scoped_lock lock(_impl->mutex);
			++_impl->display.deviceGeneration;
			const auto& request = _impl->topology.Request();
			active = request &&
				(request->upscalingEligible || request->frameGenerationEligible);
		}
		if (active) {
			_impl->renderer.OnD3D11Ready(a_adapter, a_device);
		}
	}

	TemporalRenderer& TemporalPipeline::Renderer() noexcept
	{
		return _impl->renderer;
	}

	void TemporalPipeline::PostFailure(
		temporal::FailureDomain a_domain,
		std::string a_message) noexcept
	{
		try {
			std::array<TemporalTraceEntry, Impl::kTraceCapacity> trace;
			std::size_t traceCount = 0;
			std::uint64_t traceSequence = 0;
			{
				std::scoped_lock lock(_impl->mutex);
				_impl->failureDomain = a_domain;
				_impl->failure = a_message;
				const auto& effective = _impl->topology.Effective();
				const auto impact = temporal::ClassifyFailure(
					a_domain, effective.superResolution, effective.frameGeneration);
				const auto& session = _impl->topology.Session();
				const bool runtimeActive = session && session->valid;
				if (runtimeActive) {
					_impl->topology.Quarantine(
						impact,
						_impl->configurationRevision.fetch_add(1, std::memory_order_acq_rel) + 1,
						a_message);
				}
				if (runtimeActive && impact.superResolution) {
					_impl->rendererEligible = false;
					_impl->resetEpochs.RequestSuperResolution();
					_impl->resetEpochs.RequestFrameGeneration();
				}
				if (runtimeActive && impact.frameGeneration) {
					_impl->frameGenerationQuarantined = true;
					_impl->frameGenerationEnabled.store(false, std::memory_order_release);
					_impl->resetEpochs.RequestFrameGeneration();
				}
				if (_impl->detailedTracing) {
					trace = _impl->trace;
					traceCount = _impl->traceCount;
					traceSequence = _impl->traceSequence;
				}
			}

			L->error("Temporal pipeline failure [{}]: {}",
				static_cast<unsigned>(a_domain), a_message);
			const auto firstSequence =
				traceSequence >= traceCount ? traceSequence - traceCount + 1 : 1;
			for (auto sequence = firstSequence; sequence <= traceSequence; ++sequence) {
				const auto& entry = trace[(sequence - 1) % trace.size()];
				L->error(
					"Temporal trace seq={} real={} engine={} slot={} event={} hr={:#010x}",
					entry.sequence,
					entry.realFrame,
					entry.engineFrame,
					entry.slot,
					static_cast<unsigned>(entry.event),
					static_cast<std::uint32_t>(entry.result));
			}
		} catch (...) {
		}
	}

	void TemporalPipeline::FailSuperResolutionToNative(
		std::string a_reason) noexcept
	{
		try {
			const auto revision =
				_impl->configurationRevision.fetch_add(
					1, std::memory_order_acq_rel) +
				1;
			std::scoped_lock lock(_impl->mutex);
			_impl->topology.FailSuperResolutionToNative(
				revision, std::move(a_reason));
			_impl->resetEpochs.RequestSuperResolution();
			_impl->resetEpochs.RequestFrameGeneration();
		} catch (...) {
		}
	}

	void TemporalPipeline::AdvanceEngineResourceGeneration() noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		++_impl->display.engineResourceGeneration;
	}

	void TemporalPipeline::AdvanceDisplayGeneration(
		std::uint32_t a_width,
		std::uint32_t a_height) noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		_impl->display.output = { a_width, a_height };
		++_impl->display.displayGeneration;
		_impl->resetEpochs.RequestSuperResolution();
		_impl->resetEpochs.RequestFrameGeneration();
	}

	void TemporalPipeline::RequestSuperResolutionReset() noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		_impl->resetEpochs.RequestSuperResolution();
	}

	void TemporalPipeline::RequestFrameGenerationReset() noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		_impl->resetEpochs.RequestFrameGeneration();
	}

	bool TemporalPipeline::SuperResolutionResetPending() const noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		const auto& effective = _impl->topology.Effective();
		const bool sharedStreamline =
			effective.superResolution == temporal::SuperResolutionMethod::kDLSS &&
			effective.frameGeneration == temporal::FrameGenerationMethod::kDLSSG;
		return _impl->resetEpochs.SuperResolutionPending() ||
			(sharedStreamline && _impl->resetEpochs.FrameGenerationPending());
	}

	bool TemporalPipeline::ArmFrameGenerationReset() noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		return _impl->resetEpochs.ArmFrameGeneration();
	}

	void TemporalPipeline::ConsumeSuperResolutionReset(bool a_completed) noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		_impl->resetEpochs.ConsumeSuperResolution(a_completed);
	}

	bool TemporalPipeline::RecordInputPacket(
		std::uint64_t a_engineFrame,
		temporal::Extent a_renderExtent,
		temporal::Extent a_outputExtent,
		std::uint32_t a_slot,
		bool a_externalPublished) noexcept
	{
		if (a_slot >= _impl->frames.size()) {
			PostFailure(temporal::FailureDomain::kEngine, "Frame slot index is out of range.");
			return false;
		}
		std::scoped_lock lock(_impl->mutex);
		auto& frame = _impl->frames[a_slot];
		const temporal::FrameIdentity identity{
			.realFrame = _impl->latency.Frame(),
			.engineFrame = a_engineFrame,
			.configurationRevision = _impl->topology.Effective().revision,
			.deviceGeneration = _impl->display.deviceGeneration,
			.displayGeneration = _impl->display.displayGeneration,
			.engineResourceGeneration = _impl->display.engineResourceGeneration,
			.slot = a_slot
		};
		const auto resolution = a_externalPublished
			? temporal::SceneResolution::kExternalPublished
			: temporal::SceneResolution::kNativeCompleted;
		const bool valid =
			frame.Begin(identity) &&
			frame.Plan(a_renderExtent, a_outputExtent) &&
			frame.CommitRenderState(a_renderExtent) &&
			frame.CaptureWorld(true) &&
			frame.ResolveScene(resolution) &&
			frame.CapturePreUi();
		if (valid) {
			_impl->currentFrameSlot = a_slot;
			_impl->Trace(TemporalTraceEvent::kInputPacket, frame);
		}
		return valid;
	}

	bool TemporalPipeline::PreparePresent(
		std::uint32_t a_slot,
		bool a_frameGenerationPrepared) noexcept
	{
		if (a_slot >= _impl->frames.size()) {
			return false;
		}
		std::scoped_lock lock(_impl->mutex);
		auto& frame = _impl->frames[a_slot];
		if (frame.Phase() == temporal::FramePhase::kPresentPrepared) {
			return true;
		}
		const bool prepared = frame.CaptureFinal() &&
			frame.PreparePresent(a_frameGenerationPrepared);
		if (prepared) {
			_impl->Trace(TemporalTraceEvent::kPresentPrepared, frame);
		}
		return prepared;
	}

	void TemporalPipeline::RecordPresentAttempt(
		std::uint32_t a_slot,
		UINT a_flags,
		HRESULT a_result) noexcept
	{
		if (a_slot >= _impl->frames.size()) {
			return;
		}
		std::scoped_lock lock(_impl->mutex);
		auto& frame = _impl->frames[a_slot];
		const bool testOnly = (a_flags & DXGI_PRESENT_TEST) != 0;
		const bool retryable = a_result == DXGI_ERROR_WAS_STILL_DRAWING;
		const bool accepted = SUCCEEDED(a_result);
		const bool occluded = a_result == DXGI_STATUS_OCCLUDED;
		const bool consumedFrameGenerationReset =
			frame.FrameGenerationPrepared() && !testOnly &&
			a_result == S_OK;
		_impl->Trace(
			testOnly ? TemporalTraceEvent::kPresentTest :
			retryable ? TemporalTraceEvent::kPresentRetry :
			occluded ? TemporalTraceEvent::kPresentOccluded :
			accepted ? TemporalTraceEvent::kPresentAccepted :
			TemporalTraceEvent::kPresentFailed,
			frame,
			a_result);
		if (!frame.PresentAttempt(testOnly, accepted, retryable)) {
			_impl->failureDomain = temporal::FailureDomain::kPresentation;
			_impl->failure = std::string(frame.Failure());
			return;
		}
		if (!testOnly && accepted && !retryable && !frame.Retire()) {
			_impl->failureDomain = temporal::FailureDomain::kPresentation;
			_impl->failure = std::string(frame.Failure());
		}
		if (consumedFrameGenerationReset) {
			_impl->resetEpochs.ConsumeFrameGeneration(true);
		}
	}

	void TemporalPipeline::RecordGeneratedFrames(
		std::optional<std::uint32_t> a_count) noexcept
	{
		if (a_count) {
			_impl->generatedFrames.fetch_add(
				*a_count, std::memory_order_relaxed);
		}
		_impl->generatedFrameCountAvailable.store(
			a_count.has_value(), std::memory_order_relaxed);
	}

	TemporalPipelineStatus TemporalPipeline::GetStatus() const
	{
		std::scoped_lock lock(_impl->mutex);
		TemporalPipelineStatus status;
		if (_impl->topology.Request()) {
			status.requested = *_impl->topology.Request();
		}
		status.effective = _impl->topology.Effective();
		status.pending = _impl->topology.Pending();
		if (_impl->topology.Session()) {
			status.session = *_impl->topology.Session();
		}
		status.display = _impl->display;
		status.failureDomain = _impl->failureDomain;
		status.creationState = _impl->creationState.load(std::memory_order_acquire);
		status.requestFrozen = _impl->requestFrozen.load(std::memory_order_acquire);
		status.d3d11Ready = _impl->d3d11Ready.load(std::memory_order_acquire);
		status.latencyHooksInstalled =
			_impl->latencyHooksInstalled.load(std::memory_order_acquire);
		status.latencySdkActive =
			_impl->latencySdkActive.load(std::memory_order_acquire);
		status.latencyPhase = _impl->latency.Phase();
		const auto& frame = _impl->frames[_impl->currentFrameSlot];
		status.framePhase = frame.Phase();
		status.realFrame = _impl->latency.Frame();
		status.engineFrame = frame.Identity().engineFrame;
		status.frameSlot = frame.Identity().slot;
		status.presentAttempts = frame.PresentAttempts();
		status.superResolutionResetRequested =
			_impl->resetEpochs.SuperResolutionRequested();
		status.superResolutionResetConsumed =
			_impl->resetEpochs.SuperResolutionConsumed();
		status.frameGenerationResetRequested =
			_impl->resetEpochs.FrameGenerationRequested();
		status.frameGenerationResetConsumed =
			_impl->resetEpochs.FrameGenerationConsumed();
		status.traceSequence = _impl->traceSequence;
		status.traceEntryCount = static_cast<std::uint32_t>(_impl->traceCount);
		status.failure = _impl->failure;
		return status;
	}

	temporal::EffectiveConfiguration TemporalPipeline::GetEffectiveConfiguration() const
	{
		std::scoped_lock lock(_impl->mutex);
		return _impl->topology.Effective();
	}

	bool TemporalPipeline::IsFrameGenerationProxyActive() const noexcept
	{
		return _impl->creationState.load(std::memory_order_acquire) ==
			TemporalCreationState::kProxy;
	}

	bool TemporalPipeline::IsFrameGenerationEnabledForFrame(
		bool a_inExcludedMenu) const noexcept
	{
		return IsFrameGenerationProxyActive() &&
			_impl->frameGenerationEnabled.load(std::memory_order_acquire) &&
			(!a_inExcludedMenu ||
				_impl->allowFrameGenerationInMenus.load(std::memory_order_acquire));
	}

	bool TemporalPipeline::AllowFrameGenerationInMenus() const noexcept
	{
		return _impl->allowFrameGenerationInMenus.load(std::memory_order_acquire);
	}

	FrameGenerationDiagnostics
		TemporalPipeline::GetFrameGenerationDiagnostics() const noexcept
	{
		const auto& camera = cs::engine::GetFrameBuffer();
		const auto* state = cs::engine::GetGraphicsState();
		const float fov = camera.valid
			? cs::engine::VerticalFieldOfViewFromWorldToClip(
				camera.data.CurrFrameWorldToClip)
			: 0.0f;
		const std::int64_t frameDelta = camera.valid && state
			? static_cast<std::int64_t>(state->frameCount) -
				static_cast<std::int64_t>(camera.frameCount)
			: 0;
		const auto effective = GetEffectiveConfiguration();
		return {
			.ready = _impl->swapChain.IsFrameGenerationReady(),
			.active = _impl->swapChain.IsFrameGenerationReady() &&
				effective.frameGenerationEnabled,
			.inputsCaptured =
				_impl->inputsCaptured.load(std::memory_order_relaxed),
			.hudlessPending =
				_impl->hudlessCapturePending.load(std::memory_order_relaxed),
			.alphaConditioned =
				_impl->alphaConditioned.load(std::memory_order_relaxed),
			.conditionedCaptures =
				_impl->conditionedCaptures.load(std::memory_order_relaxed),
			.rawCaptures =
				_impl->rawCaptures.load(std::memory_order_relaxed),
			.dispatches =
				_impl->frameGenerationDispatches.load(
					std::memory_order_relaxed),
			.generatedFrames =
				_impl->generatedFrames.load(std::memory_order_relaxed),
			.generatedFrameCountAvailable =
				_impl->generatedFrameCountAvailable.load(std::memory_order_relaxed),
			.failures =
				_impl->frameGenerationFailures.load(
					std::memory_order_relaxed),
			.cameraValid = camera.valid && fov > 0.0f,
			.cameraFrameDelta = frameDelta,
			.cameraFovDegrees =
				static_cast<double>(fov) * 180.0 / std::numbers::pi
		};
	}

	TemporalPipeline::FrameGenerationCaptureResources
		TemporalPipeline::GetFrameGenerationCaptureResources() const noexcept
	{
		const auto captureTexture = [](const auto* a_texture) {
			return a_texture
				? FrameGenerationCaptureResources::Texture{
					.resource = a_texture->texture11.get(),
					.srv = a_texture->srv11.get(),
					.uav = a_texture->uav11.get()
				}
				: FrameGenerationCaptureResources::Texture{};
		};
		return {
			.ready = _impl->swapChain.IsFrameGenerationReady(),
			.motion = captureTexture(_impl->swapChain.GetMotionTexture()),
			.depth = captureTexture(_impl->swapChain.GetDepthTexture()),
			.hudlessColor =
				captureTexture(_impl->swapChain.GetHudlessTexture()),
			.width = _impl->swapChain.GetWidth(),
			.height = _impl->swapChain.GetHeight(),
			.frameSlot = _impl->swapChain.GetFrameSlot()
		};
	}

	void TemporalPipeline::SetFrameGenerationInputsReady(
		bool a_ready) noexcept
	{
		_impl->swapChain.SetFrameGenerationInputsReady(a_ready);
	}

	void TemporalPipeline::FailFrameGenerationFrame(
		const char* a_reason) noexcept
	{
		_impl->swapChain.DisableFrameGeneration(a_reason);
	}

	void TemporalPipeline::ResetFrameGenerationCaptureDiagnostics() noexcept
	{
		_impl->inputsCaptured.store(false, std::memory_order_relaxed);
		_impl->hudlessCapturePending.store(false, std::memory_order_relaxed);
		_impl->alphaConditioned.store(false, std::memory_order_relaxed);
	}

	void TemporalPipeline::RecordFrameGenerationCapture(
		bool a_alphaConditioned) noexcept
	{
		_impl->inputsCaptured.store(true, std::memory_order_relaxed);
		_impl->hudlessCapturePending.store(true, std::memory_order_relaxed);
		_impl->alphaConditioned.store(
			a_alphaConditioned,
			std::memory_order_relaxed);
		if (a_alphaConditioned) {
			_impl->conditionedCaptures.fetch_add(
				1,
				std::memory_order_relaxed);
		} else {
			_impl->rawCaptures.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void TemporalPipeline::SetHudlessCapturePending(
		bool a_pending) noexcept
	{
		_impl->hudlessCapturePending.store(
			a_pending,
			std::memory_order_relaxed);
	}

	void TemporalPipeline::RecordFrameGenerationDispatch() noexcept
	{
		_impl->frameGenerationDispatches.fetch_add(
			1,
			std::memory_order_relaxed);
	}

	void TemporalPipeline::RecordFrameGenerationFailure() noexcept
	{
		_impl->frameGenerationFailures.fetch_add(
			1,
			std::memory_order_relaxed);
	}

	bool TemporalPipeline::EvaluateD3D12DLSS(
		const features::SuperResolutionExecutionContext& a_context)
	{
		return _impl->swapChain.EvaluateD3D12SuperResolution(
			_impl->dlssProvider,
			a_context);
	}

	bool TemporalPipeline::EvaluateD3D12XeSS(
		const features::SuperResolutionExecutionContext& a_context)
	{
		return _impl->swapChain.EvaluateD3D12SuperResolution(
			_impl->xess,
			a_context);
	}

	temporal::ProviderResult TemporalPipeline::EvaluateD3D11SuperResolution(
		temporal::SuperResolutionMethod a_method,
		const temporal::SuperResolutionRequest& a_request)
	{
		switch (a_method) {
		case temporal::SuperResolutionMethod::kFSR3:
			return _impl->fsrProvider.Record(a_request);
		case temporal::SuperResolutionMethod::kDLSS:
			return _impl->dlssProvider.Record(a_request);
		case temporal::SuperResolutionMethod::kXeSS:
			return _impl->xess.Record(a_request);
		default:
			return {
				.code = temporal::ProviderResultCode::kUnavailable,
				.message =
					"No external super-resolution provider is active."
			};
		}
	}

	temporal::SuperResolutionSizeResult
		TemporalPipeline::QuerySuperResolutionRenderSize(
			temporal::SuperResolutionMethod a_method,
			const temporal::SuperResolutionSizeRequest& a_request)
	{
		const auto status = GetStatus();
		const auto index = static_cast<std::size_t>(a_method);
		if (!status.session.valid ||
			index >= status.session.admittedSr.size() ||
			!status.session.admittedSr[index]) {
			return {
				.result = {
					.code = temporal::ProviderResultCode::kUnavailable,
					.message =
						"The selected super-resolution provider was not admitted."
				}
			};
		}

		switch (a_method) {
		case temporal::SuperResolutionMethod::kFSR3:
			return _impl->fsrProvider.QueryRenderSize(a_request);
		case temporal::SuperResolutionMethod::kDLSS:
			return _impl->dlssProvider.QueryRenderSize(a_request);
		case temporal::SuperResolutionMethod::kXeSS:
			return _impl->xess.QueryRenderSize(a_request);
		default:
			return {
				.result = {
					.code = temporal::ProviderResultCode::kUnavailable,
					.message =
						"No external super-resolution provider is active."
				}
			};
		}
	}

	bool TemporalPipeline::IsSuperResolutionRuntimeReady(
		temporal::SuperResolutionMethod a_method) const noexcept
	{
		switch (a_method) {
		case temporal::SuperResolutionMethod::kFSR3:
			return _impl->fidelityFX.IsReady();
		case temporal::SuperResolutionMethod::kDLSS:
			return _impl->streamline.featureDLSS;
		case temporal::SuperResolutionMethod::kXeSS:
			return _impl->xess.IsD3D12() ||
				GetStatus().session.admittedSr[
					static_cast<std::size_t>(
						temporal::SuperResolutionMethod::kXeSS)];
		default:
			return true;
		}
	}

	bool TemporalPipeline::UsesD3D12SuperResolution(
		temporal::SuperResolutionMethod a_method) const noexcept
	{
		return a_method == temporal::SuperResolutionMethod::kDLSS
			? _impl->streamline.IsD3D12Session()
			: a_method == temporal::SuperResolutionMethod::kXeSS &&
				_impl->xess.IsD3D12();
	}

	bool TemporalPipeline::CreateFsrSuperResolutionResources(
		ID3D11Device* a_device,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight)
	{
		return _impl->fidelityFX.CreateFSRResources({
			.device = a_device,
			.maxRenderWidth = a_renderWidth,
			.maxRenderHeight = a_renderHeight,
			.outputWidth = a_outputWidth,
			.outputHeight = a_outputHeight
		});
	}

	void TemporalPipeline::DestroySuperResolutionResources(
		temporal::SuperResolutionMethod a_method) noexcept
	{
		if (a_method == temporal::SuperResolutionMethod::kDLSS) {
			_impl->streamline.DestroyDLSSResources();
		} else if (a_method == temporal::SuperResolutionMethod::kFSR3) {
			_impl->fidelityFX.DestroyFSRResources();
		}
	}

	temporal::ProviderResult TemporalPipeline::PreflightXeSS(
		ID3D11Device* a_conversionDevice,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight,
		std::uint32_t a_qualityMode)
	{
		bool contextDrained = false;
		if (_impl->xess.RequiresContextReinitialization(
				a_outputWidth,
				a_outputHeight,
				a_qualityMode)) {
			contextDrained = _impl->xess.IsD3D12()
				? _impl->swapChain.DrainSuperResolution()
				: cs::engine::WaitForGpuIdle(
					cs::engine::GetImmediateContext());
			if (!contextDrained) {
				return {
					.code = temporal::ProviderResultCode::kFailure,
					.message =
						"XeSS context reinitialization could not drain pending GPU work."
				};
			}
		}
		return _impl->xess.Preflight(
			a_conversionDevice,
			a_renderWidth,
			a_renderHeight,
			a_outputWidth,
			a_outputHeight,
			a_qualityMode,
			contextDrained);
	}

	void TemporalPipeline::ResetFsrFrameGenerationCamera() noexcept
	{
		_impl->fidelityFX.ResetFrameGenerationCameraData();
	}

	void TemporalPipeline::RequestFsrFrameGenerationReset() noexcept
	{
		_impl->fidelityFX.RequestFrameGenerationReset();
	}

	FrameGenerationDebugTexture
		TemporalPipeline::GetFrameGenerationDebugTexture(
			FrameGenerationDebugResource a_resource) const noexcept
	{
		const features::SharedD3D11D3D12Texture* resource = nullptr;
		switch (a_resource) {
		case FrameGenerationDebugResource::kHudless:
			resource = _impl->swapChain.GetHudlessTexture();
			break;
		case FrameGenerationDebugResource::kFinal:
			resource = _impl->swapChain.GetProxyTexture();
			break;
		case FrameGenerationDebugResource::kDepth:
			resource = _impl->swapChain.GetDepthTexture();
			break;
		case FrameGenerationDebugResource::kMotion:
			resource = _impl->swapChain.GetMotionTexture();
			break;
		}
		return resource && resource->srv11
			? FrameGenerationDebugTexture{
				.srv = resource->srv11.get(),
				.width = _impl->swapChain.GetWidth(),
				.height = _impl->swapChain.GetHeight()
			}
			: FrameGenerationDebugTexture{};
	}

}
