#include "Render/TemporalPipeline.h"
#include "Render/TemporalDevicePolicy.h"
#include "Render/TemporalStartup.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <format>
#include <mutex>
#include <numbers>
#include <utility>

#include "DX12SwapChain.h"
#include "Feature.h"
#include "FrameGeneration.h"
#include "Log.h"
#include "PresentationProviders.h"
#include "RenderDoc.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/RendererContext.h"
#include "Render/TemporalPipelineAnchors.h"
#include "Render/TemporalRenderer.h"
#include "Streamline.h"
#include "SuperResolutionProviders.h"
#include "Upscaling.h"

namespace cs::render::temporal
{
	FrameGenerationCamera
	BuildFrameGenerationCamera(const engine::FrameBufferSnapshot& a_snapshot,
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight) noexcept
	{
		FrameGenerationCamera camera;
		if (!a_snapshot.valid) {
			return camera;
		}

		std::memcpy(camera.currentWorldToClip, a_snapshot.data.CurrFrameWorldToClip,
			sizeof(camera.currentWorldToClip));
		std::memcpy(camera.previousWorldToClip, a_snapshot.data.PrevFrameWorldToClip,
			sizeof(camera.previousWorldToClip));
		std::memcpy(camera.viewToWorld, a_snapshot.data.ViewToWorld,
			sizeof(a_snapshot.data.ViewToWorld));
		camera.viewToWorld[15] = 1.0f;
		const auto basis = engine::GetCameraWorldBasis(a_snapshot.data);
		const auto position = engine::CameraWorldOrigin(a_snapshot.data);
		const auto previousPosition =
			engine::CameraPreviousWorldOrigin(a_snapshot.data);
		std::memcpy(camera.right, &basis.right, sizeof(camera.right));
		std::memcpy(camera.up, &basis.up, sizeof(camera.up));
		std::memcpy(camera.forward, &basis.forward, sizeof(camera.forward));
		std::memcpy(camera.position, &position, sizeof(camera.position));
		std::memcpy(camera.previousPosition, &previousPosition,
			sizeof(camera.previousPosition));
		camera.nearPlane = engine::GetCameraNear();
		camera.farPlane = engine::GetCameraFar();
		camera.verticalFov = engine::VerticalFieldOfViewFromWorldToClip(
			a_snapshot.data.CurrFrameWorldToClip);
		camera.aspectRatio = a_outputHeight ? static_cast<float>(a_outputWidth) /
		                                          static_cast<float>(a_outputHeight) :
		                                      0.0f;
		camera.engineFrame = a_snapshot.frameCount;
		camera.valid = engine::HasUsableWorldCamera(a_snapshot.data) &&
		               camera.verticalFov > 0.0f && camera.nearPlane > 0.0f &&
		               camera.farPlane > camera.nearPlane;
		return camera;
	}
}  // namespace cs::render::temporal

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.temporalpipeline");

		bool IsCallSiteTargeting(std::uintptr_t a_site,
			std::uintptr_t a_expectedTarget) noexcept
		{
			if (!a_site || !a_expectedTarget) {
				return false;
			}
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_site);
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
			const HMONITOR monitor =
				MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
			MONITORINFOEXW monitorInfo{};
			monitorInfo.cbSize = sizeof(monitorInfo);
			if (!GetMonitorInfoW(monitor, &monitorInfo)) {
				return 0.0;
			}
			DEVMODEW mode{};
			mode.dmSize = sizeof(mode);
			if (!EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS,
					&mode)) {
				return 0.0;
			}
			return mode.dmDisplayFrequency > 1 ? static_cast<double>(mode.dmDisplayFrequency) : 0.0;
		}

		temporal::SuperResolutionMethod
		ToCore(features::Upscaling::UpscaleMethod a_method) noexcept
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
			case Source::kFSR4:
				return Target::kFSR4;
			}
			return Target::kNone;
		}

		temporal::FrameGenerationMethod
		ToCore(features::FrameGeneration::Method a_method) noexcept
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
			case Source::kFSR4:
				return Target::kFSR4;
			}
			return Target::kOff;
		}

		temporal::FrameGenerationConfiguration
		ToCoreFrameGenerationConfiguration(
			const features::FrameGeneration::Settings& a_settings) noexcept
		{
			return {
				.mode = a_settings.dlssgMode == 1
					? temporal::FrameGenerationMode::kDynamic
					: temporal::FrameGenerationMode::kFixed,
				.fixedMultiplier =
					a_settings.dlssgFixedMultiplier,
				.dynamicTargetFrameRate =
					a_settings.dlssgDynamicTargetFps
			};
		}

		features::Upscaling::UpscaleMethod
		ToFeature(temporal::SuperResolutionMethod a_method) noexcept
		{
			using Source = temporal::SuperResolutionMethod;
			using Target = features::Upscaling::UpscaleMethod;
			switch (a_method) {
			case Source::kNone:
				return Target::kNONE;
			case Source::kTAA:
				return Target::kTAA;
			case Source::kFSR3:
				return Target::kFSR;
			case Source::kDLSS:
				return Target::kDLSS;
			case Source::kFSR4:
				return Target::kFSR4;
			case Source::kCount:
				break;
			}
			return Target::kNONE;
		}

		bool UsesSharedStreamlineConstants(
			const temporal::EffectiveConfiguration& a_effective) noexcept
		{
			const bool streamlineSuperResolution =
				a_effective.superResolution ==
					temporal::SuperResolutionMethod::kDLSS ||
				a_effective.superResolution ==
					temporal::SuperResolutionMethod::kFSR3 ||
				a_effective.superResolution ==
					temporal::SuperResolutionMethod::kFSR4;
			const bool streamlineFrameGeneration =
				a_effective.frameGenerationEnabled &&
				(a_effective.frameGeneration ==
						temporal::FrameGenerationMethod::kDLSSG ||
					a_effective.frameGeneration ==
							temporal::FrameGenerationMethod::kFSR3 ||
						a_effective.frameGeneration ==
							temporal::FrameGenerationMethod::kFSR4);
			return streamlineSuperResolution &&
			       streamlineFrameGeneration;
		}
	}  // namespace

	struct TemporalPipeline::Impl
	{
		static constexpr std::size_t kTraceCapacity = 64;

		mutable std::mutex mutex;
		TemporalRenderer renderer;
		temporal::UpscalingSettings requestedRenderSettings;
		temporal::UpscalingSettings effectiveRenderSettings;
		bool rendererEligible = false;
		bool frameGenerationQuarantined = false;
		temporal::TopologyState topology;
		temporal::DisplayState display;
		temporal::ResetEpochs resetEpochs;
		temporal::FailureDomain failureDomain = temporal::FailureDomain::kNone;
		std::string failure;
		std::atomic<TemporalCreationState> creationState{
			TemporalCreationState::kUnregistered
		};
		std::atomic_bool requestFrozen{ false };
		std::atomic_bool d3d11Ready{ false };
		std::atomic_bool frameGenerationEnabled{ false };
		std::atomic_bool allowFrameGenerationInMenus{ false };
		std::atomic_uint64_t configurationRevision{ 1 };
		temporal::LatencyTimeline latency;
		std::atomic_bool latencyHooksInstalled{ false };
		std::atomic_bool latencySdkActive{ false };
		std::atomic_bool latencyFailureReported{ false };
		std::atomic_uint64_t generatedFrames{ 0 };
		std::atomic_bool generatedFrameCountAvailable{ false };
		std::atomic_uint64_t providerPresentedFrames{ 0 };
		std::atomic_bool providerPresentedFrameCountAvailable{ false };
		std::atomic_bool inputsCaptured{ false };
		std::atomic_bool hudlessCapturePending{ false };
		std::atomic_bool alphaConditioned{ false };
		std::atomic_uint64_t conditionedCaptures{ 0 };
		std::atomic_uint64_t rawCaptures{ 0 };
		std::atomic_uint64_t frameGenerationDispatches{ 0 };
		std::atomic_uint64_t frameGenerationFailures{ 0 };
		std::array<temporal::FrameTransaction, 2> frames;
		std::array<std::optional<temporal::FrameGenerationRequest>, 2>
			frozenFrameConstants;
		std::uint32_t currentFrameSlot = 0;
		std::array<TemporalTraceEntry, kTraceCapacity> trace;
		std::uint64_t traceSequence = 0;
		std::size_t traceCount = 0;
		std::atomic_bool detailedTracing{ false };
		FrameGenerationCpuTimingCollector<> frameGenerationCpuTimings;
		features::Streamline streamline;
		features::StreamlineSuperResolution dlssProvider{
			streamline,
			features::StreamlineSuperResolution::Method::kDLSS
		};
		features::StreamlineSuperResolution fsrProvider{
			streamline,
			features::StreamlineSuperResolution::Method::kFSR3
		};
		features::StreamlineSuperResolution fsr4Provider{
			streamline,
			features::StreamlineSuperResolution::Method::kFSR4
		};
		features::StreamlinePresentation streamlineFsrPresentation{
			streamline,
			features::StreamlinePresentation::Method::kFSRG
		};
		features::StreamlinePresentation streamlinePresentation{
			streamline,
			features::StreamlinePresentation::Method::kDLSSG
		};
		features::StreamlinePresentation streamlineFsr4Presentation{
			streamline,
			features::StreamlinePresentation::Method::kFSR4
		};
		temporal::IFrameGenerationProvider* activePresentation = nullptr;
		features::DX12SwapChain swapChain;

		temporal::ISuperResolutionProvider*
		SuperResolutionProvider(temporal::SuperResolutionMethod a_method) noexcept
		{
			switch (a_method) {
			case temporal::SuperResolutionMethod::kFSR3:
				return &fsrProvider;
			case temporal::SuperResolutionMethod::kDLSS:
				return &dlssProvider;
			case temporal::SuperResolutionMethod::kFSR4:
				return &fsr4Provider;
			default:
				return nullptr;
			}
		}

		temporal::IFrameGenerationProvider*
		FrameGenerationProvider(
			temporal::FrameGenerationMethod a_method) noexcept
		{
			switch (a_method) {
			case temporal::FrameGenerationMethod::kFSR3:
				return &streamlineFsrPresentation;
			case temporal::FrameGenerationMethod::kDLSSG:
				return &streamlinePresentation;
			case temporal::FrameGenerationMethod::kFSR4:
				return &streamlineFsr4Presentation;
			default:
				return nullptr;
			}
		}

		void Trace(TemporalTraceEvent a_event,
			const temporal::FrameTransaction& a_frame,
			HRESULT a_result = S_OK) noexcept
		{
			if (!detailedTracing.load(std::memory_order_relaxed)) {
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

	TemporalPipeline::TemporalPipeline() : _impl(std::make_unique<Impl>()) {}

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
				REL::ID({ kMainLoopAnchor[0], kMainLoopAnchor[1], kMainLoopAnchor[2] })
					.address() +
				kMainLoopMessageLoopCall[runtimeIndex];
			const auto messageLoopTarget =
				REL::ID({ kWindowsMessageLoop[0], kWindowsMessageLoop[1],
							kWindowsMessageLoop[2] })
					.address();
			const auto swapSite =
				REL::ID({ kOnIdle[0], kOnIdle[1], kOnIdle[2] }).address() +
				kOnIdleSwapCall[runtimeIndex];
			const auto swapTarget = REL::ID({ kSwap[0], kSwap[1], kSwap[2] }).address();
			if (!IsCallSiteTargeting(messageLoopSite, messageLoopTarget) ||
				!IsCallSiteTargeting(swapSite, swapTarget)) {
				PostFailure(temporal::FailureDomain::kEngine,
					"Normal main-loop latency callsites did not match their "
					"validated targets.");
				return false;
			}

			stl::write_thunk_call<MainLoop_WindowsMessageLoop>(messageLoopSite);
			stl::write_thunk_call<OnIdle_Swap>(swapSite);
			_impl->latencyHooksInstalled.store(true, std::memory_order_release);
			L->info("Installed validated normal-loop latency hooks");
			return true;
		} catch (const std::exception& e) {
			PostFailure(temporal::FailureDomain::kEngine,
				std::format("Latency hook installation failed: {}", e.what()));
		} catch (...) {
			PostFailure(temporal::FailureDomain::kEngine,
				"Latency hook installation failed.");
		}
		return false;
	}

	void TemporalPipeline::RegisterCreationRouter()
	{
		auto expected = TemporalCreationState::kUnregistered;
		if (!_impl->creationState.compare_exchange_strong(
				expected, TemporalCreationState::kRegistered,
				std::memory_order_acq_rel)) {
			return;
		}

		const bool preRegistered = RegisterPreCreateDeviceAndSwapChain(
			[this](DXGI_SWAP_CHAIN_DESC* a_desc,
				std::vector<D3D_FEATURE_LEVEL>& a_levels) {
				OnPreCreateDeviceAndSwapChain(a_desc, a_levels);
			});
		const bool postRegistered = RegisterPostCreateDeviceAndSwapChain(
			[this](IDXGIAdapter* a_adapter, ID3D11Device** a_device,
				IDXGISwapChain** a_swapChain) {
				OnPostCreateDeviceAndSwapChain(a_adapter, a_device, a_swapChain);
			});
		const bool replacementRegistered =
			RegisterReplacementCreateDeviceAndSwapChain(
				[this](CreateDeviceAndSwapChainContext& a_context) {
					return OnReplacementCreateDeviceAndSwapChain(a_context);
				});
		if (!preRegistered || !postRegistered || !replacementRegistered) {
			_impl->creationState.store(TemporalCreationState::kFailed,
				std::memory_order_release);
			PostFailure(temporal::FailureDomain::kConfiguration,
				"The swap-chain replacement route is already owned.");
			L->error(
				"Temporal pipeline could not register the sole swap-chain "
				"creation router");
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
			requested.superResolution =
				ToCore(static_cast<features::Upscaling::UpscaleMethod>(
					upscaling->settings.upscaleMethod));
			requested.superResolutionEnabled =
				requested.superResolution !=
				temporal::SuperResolutionMethod::kNone;
			requested.qualityMode = upscaling->settings.qualityMode;
			requested.streamlineLogLevel = upscaling->settings.streamlineLogLevel;
		}
		if (frameGeneration) {
			requested.frameGeneration =
				ToCore(static_cast<features::FrameGeneration::Method>(
					frameGeneration->settings.frameGenerationMethod));
			requested.frameGenerationEnabled =
				requested.frameGeneration !=
				temporal::FrameGenerationMethod::kOff;
			requested.allowFrameGenerationInMenus =
				frameGeneration->settings.frameGenerationAllowInMenus;
			requested.frameGenerationConfiguration =
				ToCoreFrameGenerationConfiguration(
					frameGeneration->settings);
		}
		requested.revision =
			_impl->configurationRevision.fetch_add(1, std::memory_order_acq_rel);

		bool frozen = false;
		{
			std::scoped_lock lock(_impl->mutex);
			frozen = _impl->topology.Freeze(requested);
		}
		if (!frozen) {
			PostFailure(temporal::FailureDomain::kConfiguration,
				"The temporal request was frozen more than once.");
			return;
		}
		_impl->frameGenerationEnabled.store(requested.frameGenerationEnabled,
			std::memory_order_release);
		_impl->allowFrameGenerationInMenus.store(
			requested.allowFrameGenerationInMenus, std::memory_order_release);
		_impl->requestFrozen.store(true, std::memory_order_release);
		_impl->creationState.store(TemporalCreationState::kFrozen,
			std::memory_order_release);
		_impl->rendererEligible = requested.upscalingEligible;
		_impl->effectiveRenderSettings = _impl->requestedRenderSettings;
		_impl->renderer.ApplyConfiguration(_impl->effectiveRenderSettings,
			_impl->rendererEligible);
		if (requested.upscalingEligible || requested.frameGenerationEligible) {
			InstallLatencyHooks();
			_impl->renderer.InstallTemporalHooks();
		}
		L->info("Temporal request frozen: SR={} eligible={}, FG={} eligible={}",
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
		const auto superResolution =
			ToCore(static_cast<features::Upscaling::UpscaleMethod>(
				upscaling->settings.upscaleMethod));
		const auto frameGenerationMethod =
			ToCore(static_cast<features::FrameGeneration::Method>(
				frameGeneration->settings.frameGenerationMethod));
		_impl->topology.SubmitLive(
			superResolution != temporal::SuperResolutionMethod::kNone,
			superResolution,
			upscaling->settings.qualityMode,
			frameGenerationMethod != temporal::FrameGenerationMethod::kOff,
			frameGenerationMethod,
			revision,
			ToCoreFrameGenerationConfiguration(
				frameGeneration->settings));
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
		if (a_enabled == _impl->detailedTracing.load(std::memory_order_acquire)) {
			return;
		}
		if (a_enabled) {
			_impl->frameGenerationCpuTimings.SetEnabled(true);
			_impl->detailedTracing.store(true, std::memory_order_release);
		} else {
			_impl->detailedTracing.store(false, std::memory_order_release);
			_impl->frameGenerationCpuTimings.SetEnabled(false);
			std::scoped_lock lock(_impl->mutex);
			_impl->traceCount = 0;
		}
	}

	bool TemporalPipeline::DetailedTracingEnabled() const noexcept
	{
		return _impl->detailedTracing.load(std::memory_order_acquire);
	}

	void TemporalPipeline::ApplyPendingConfiguration()
	{
		std::optional<temporal::EffectiveConfiguration> pending;
		temporal::EffectiveConfiguration current;
		{
			std::scoped_lock lock(_impl->mutex);
			pending = _impl->topology.BeginPendingTransition();
			current = _impl->topology.Effective();
		}
		if (!pending) {
			return;
		}

		const auto targetFrameGenerationMethod =
			pending->frameGenerationEnabled
				? pending->frameGeneration
				: temporal::FrameGenerationMethod::kOff;
		auto* targetPresentation =
			_impl->FrameGenerationProvider(targetFrameGenerationMethod);
		if (targetPresentation &&
			pending->frameGenerationEnabled) {
			const auto validation =
				targetPresentation->ValidateConfiguration(
					pending->frameGenerationConfiguration);
			if (validation.code ==
				temporal::ProviderResultCode::kSkipped) {
				std::scoped_lock lock(_impl->mutex);
				_impl->topology.DeferTransition(
					pending->revision);
				return;
			}
			if (!validation.Succeeded()) {
				std::scoped_lock lock(_impl->mutex);
				_impl->topology.RejectPendingTransition(
					pending->revision);
				_impl->failureDomain =
					validation.failureDomain ==
							temporal::FailureDomain::kNone
						? temporal::FailureDomain::kFrameGeneration
						: validation.failureDomain;
				_impl->failure = validation.message.empty()
					? "The requested frame-generation options are unavailable."
					: validation.message;
				return;
			}
		}
		const bool presentationChange =
			targetPresentation != _impl->activePresentation;
		const bool frameGenerationConfigurationChange =
			pending->frameGenerationConfiguration !=
			current.frameGenerationConfiguration;
		if (presentationChange) {
			const auto preflight =
				_impl->swapChain.CanReplacePresentationProvider(
					targetPresentation);
			if (preflight.code ==
				temporal::ProviderResultCode::kSkipped) {
				std::scoped_lock lock(_impl->mutex);
				_impl->topology.DeferTransition(pending->revision);
				return;
			}
			if (!preflight.Succeeded()) {
				std::scoped_lock lock(_impl->mutex);
				_impl->topology.RejectPendingTransition(
					pending->revision);
				_impl->failureDomain =
					preflight.failureDomain ==
							temporal::FailureDomain::kNone
						? temporal::FailureDomain::kFrameGeneration
						: preflight.failureDomain;
				_impl->failure = preflight.message.empty()
					? "The requested frame-generation configuration was rejected."
					: preflight.message;
				return;
			}
		}

		const bool srResourcesChange =
			pending->superResolution != current.superResolution ||
			pending->qualityMode != current.qualityMode;
		const auto usesLiveD3D12SuperResolution =
			[](temporal::SuperResolutionMethod a_method) {
				return a_method ==
						temporal::SuperResolutionMethod::kDLSS ||
					a_method ==
						temporal::SuperResolutionMethod::kFSR3 ||
					a_method ==
						temporal::SuperResolutionMethod::kFSR4;
			};
		if (srResourcesChange &&
			usesLiveD3D12SuperResolution(
				pending->superResolution)) {
			temporal::SuperResolutionInitContext init{};
			init.device = _impl->swapChain.GetD3D12Device();
			auto* provider =
				_impl->SuperResolutionProvider(pending->superResolution);
			auto initialized = provider
				? provider->Initialize(init)
				: temporal::ProviderResult{
					  .code =
						  temporal::ProviderResultCode::kUnavailable,
					  .message =
						  "The requested super-resolution provider is unavailable."
				  };
			const auto* state = cs::engine::GetGraphicsState();
			if (initialized.Succeeded() && !state) {
				initialized = {
					.code = temporal::ProviderResultCode::kFailure,
					.message =
						"The requested super-resolution configuration has no current output "
						"extent.",
					.failureDomain = temporal::FailureDomain::kEngine
				};
			} else if (initialized.Succeeded()) {
				const auto size = provider->QueryRenderSize(
					{ .outputWidth = state->screenWidth,
						.outputHeight = state->screenHeight,
						.qualityMode = pending->qualityMode });
				if (!size.Succeeded()) {
					initialized = size.result;
				}
			}
			if (!initialized.Succeeded()) {
				std::scoped_lock lock(_impl->mutex);
				_impl->topology.RejectPendingTransition(pending->revision);
				_impl->failureDomain =
					initialized.failureDomain ==
							temporal::FailureDomain::kNone
						? temporal::FailureDomain::kSuperResolution
						: initialized.failureDomain;
				_impl->failure = initialized.message.empty()
					? "The requested super-resolution configuration was rejected."
					: initialized.message;
				return;
			}
		}

		if (srResourcesChange &&
			usesLiveD3D12SuperResolution(
				current.superResolution)) {
			const auto destroy = DestroySuperResolutionResources(
				current.superResolution);
			if (!destroy.Succeeded()) {
				{
					std::scoped_lock lock(_impl->mutex);
					_impl->topology.RejectPendingTransition(
						pending->revision);
				}
				PostFailure(
					destroy.failureDomain ==
							temporal::FailureDomain::kNone
						? temporal::FailureDomain::kStreamline
						: destroy.failureDomain,
					destroy.message.empty()
						? "Super-resolution resources could not be retired for a live transition."
						: destroy.message);
				return;
			}
		}

		temporal::ProviderResult presentationResult{
			.code = temporal::ProviderResultCode::kSuccess
		};
		if (presentationChange) {
			presentationResult =
				_impl->swapChain.ReplacePresentationProvider(
					targetPresentation);
			if (presentationResult.code ==
				temporal::ProviderResultCode::kSkipped) {
				std::scoped_lock lock(_impl->mutex);
				_impl->topology.DeferTransition(pending->revision);
				return;
			}
			if (!presentationResult.Succeeded()) {
				const std::string failure =
					presentationResult.message.empty()
						? "The requested frame-generation presentation chain "
						  "could not be activated."
						: presentationResult.message;
				if (_impl->swapChain.IsReady() &&
					!_impl->swapChain.GetPresentationProvider()) {
					bool recovered = false;
					temporal::UpscalingSettings recoveredSettings;
					{
						std::scoped_lock lock(_impl->mutex);
						recovered =
							_impl->topology
								.CommitPendingFrameGenerationFallback(
									pending->revision, failure);
						if (recovered) {
							const bool streamlineSrInvalidated =
								pending->superResolutionEnabled &&
								(pending->superResolution ==
										temporal::SuperResolutionMethod::kDLSS ||
									pending->superResolution ==
										temporal::SuperResolutionMethod::kFSR3 ||
									pending->superResolution ==
										temporal::SuperResolutionMethod::kFSR4) &&
								!_impl->streamline.deviceRegistered;
							if (streamlineSrInvalidated) {
								_impl->topology
									.FailSuperResolutionToNative(
										pending->revision,
										"Streamline became unavailable while "
										"recovering plain presentation.");
							}
							_impl->activePresentation = nullptr;
							_impl->effectiveRenderSettings =
								_impl->requestedRenderSettings;
							const auto& effective =
								_impl->topology.Effective();
							_impl->effectiveRenderSettings.upscaleMethod =
								static_cast<std::uint32_t>(
									ToFeature(
										effective.superResolution));
							_impl->effectiveRenderSettings.qualityMode =
								effective.qualityMode;
							_impl->effectiveRenderSettings.enabled =
								effective.superResolutionEnabled;
							_impl->frameGenerationEnabled.store(
								false, std::memory_order_release);
							_impl->latencySdkActive.store(
								false, std::memory_order_release);
							_impl->failureDomain =
								temporal::FailureDomain::kFrameGeneration;
							_impl->failure = failure;
							_impl->resetEpochs.RequestFrameGeneration();
							if (srResourcesChange) {
								_impl->resetEpochs
									.RequestSuperResolution();
							}
							recoveredSettings =
								_impl->effectiveRenderSettings;
						}
					}
					if (recovered) {
						_impl->renderer.ApplyConfiguration(
							recoveredSettings,
							_impl->rendererEligible);
						RecordFrameGenerationFailure();
						L->error("{}", failure);
					}
					return;
				}
				PostFailure(
					presentationResult.failureDomain ==
							temporal::FailureDomain::kNone
						? temporal::FailureDomain::kPresentation
						: presentationResult.failureDomain,
					failure);
				return;
			}
		}

		bool committed = false;
		{
			std::scoped_lock lock(_impl->mutex);
			_impl->activePresentation = targetPresentation;
			committed =
				_impl->topology.CommitPendingTransition(
					pending->revision,
					targetPresentation
						? targetFrameGenerationMethod
						: temporal::FrameGenerationMethod::kOff);
			if (committed) {
				const auto& effective = _impl->topology.Effective();
				_impl->effectiveRenderSettings =
					_impl->requestedRenderSettings;
				_impl->effectiveRenderSettings.upscaleMethod =
					static_cast<std::uint32_t>(
						ToFeature(effective.superResolution));
				_impl->effectiveRenderSettings.qualityMode =
					effective.qualityMode;
				_impl->effectiveRenderSettings.enabled =
					effective.superResolutionEnabled;
				_impl->frameGenerationEnabled.store(
					effective.frameGenerationEnabled &&
						!_impl->frameGenerationQuarantined,
					std::memory_order_release);
				_impl->latencySdkActive.store(
					_impl->latencyHooksInstalled.load(
						std::memory_order_acquire) &&
						_impl->activePresentation &&
						effective.frameGeneration ==
							temporal::FrameGenerationMethod::kDLSSG &&
						effective.frameGenerationEnabled &&
						_impl->activePresentation->IsReady(),
					std::memory_order_release);
				if (srResourcesChange) {
					_impl->resetEpochs.RequestSuperResolution();
					_impl->resetEpochs.RequestFrameGeneration();
				} else if (presentationChange ||
					frameGenerationConfigurationChange) {
					_impl->resetEpochs.RequestFrameGeneration();
				}
			}
		}
		if (committed) {
			_impl->renderer.ApplyConfiguration(
				_impl->effectiveRenderSettings, _impl->rendererEligible);
		}
	}

	void TemporalPipeline::BeginMainLoopFrame() noexcept
	{
		try {
			ApplyPendingConfiguration();
		} catch (const std::exception& e) {
			PostFailure(temporal::FailureDomain::kEngine,
				std::format(
					"Live temporal configuration failed: {}", e.what()));
		} catch (...) {
			PostFailure(temporal::FailureDomain::kEngine,
				"Live temporal configuration failed.");
		}
		std::uint64_t frame = 0;
		temporal::UpscalingSettings renderSettings;
		bool eligible = false;
		{
			std::scoped_lock lock(_impl->mutex);
			frame = _impl->latency.BeginFrame();
			renderSettings = _impl->requestedRenderSettings;
			const auto& effective = _impl->topology.Effective();
			renderSettings.upscaleMethod =
				static_cast<std::uint32_t>(
					ToFeature(effective.superResolution));
			renderSettings.qualityMode = effective.qualityMode;
			renderSettings.enabled = effective.superResolutionEnabled;
			_impl->effectiveRenderSettings = renderSettings;
			eligible = _impl->rendererEligible;
		}
		_impl->renderer.ApplyConfiguration(renderSettings, eligible);
		cs::engine::RefreshFrameBufferContextHooks();
		if (_impl->latencySdkActive.load(std::memory_order_acquire) &&
			_impl->renderer.ShouldUseFrameGenerationThisFrame()) {
			const auto result = [&] {
				auto timing = MeasureFrameGenerationCpuPhase(
					FrameGenerationCpuPhase::kLatencySleep);
				return _impl->activePresentation->Sleep(
					static_cast<std::uint32_t>(frame));
			}();
			if (!result.Succeeded() && !_impl->latencyFailureReported.exchange(
										   true, std::memory_order_acq_rel)) {
				PostFailure(temporal::FailureDomain::kFrameGeneration,
					result.message.empty() ? "The selected latency provider rejected Sleep." : result.message);
			}
		}
		cs::render::annotation::SetMarker("Temporal/Latency/Sleep");
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
		if (_impl->latencySdkActive.load(std::memory_order_acquire) &&
			_impl->renderer.ShouldUseFrameGenerationThisFrame()) {
			const auto simulationResult = _impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kSimulationStart,
				static_cast<std::uint32_t>(frame));
			const auto inputResult = _impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kInputSample,
				static_cast<std::uint32_t>(frame));
			const auto& result =
				!simulationResult.Succeeded() ? simulationResult : inputResult;
			if (!result.Succeeded() && !_impl->latencyFailureReported.exchange(
										   true, std::memory_order_acq_rel)) {
				PostFailure(
					temporal::FailureDomain::kFrameGeneration,
					result.message.empty() ? "The selected latency provider rejected a simulation marker." : result.message);
			}
		}
		cs::render::annotation::SetMarker("Temporal/Latency/SimulationStart");
		cs::render::annotation::SetMarker("Temporal/Latency/InputSample");
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
		if (_impl->latencySdkActive.load(std::memory_order_acquire) &&
			_impl->renderer.ShouldUseFrameGenerationThisFrame()) {
			const auto simulationResult = _impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kSimulationEnd,
				static_cast<std::uint32_t>(frame));
			const auto submitResult = _impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kRenderSubmitStart,
				static_cast<std::uint32_t>(frame));
			const auto& result =
				!simulationResult.Succeeded() ? simulationResult : submitResult;
			if (!result.Succeeded() && !_impl->latencyFailureReported.exchange(
										   true, std::memory_order_acq_rel)) {
				PostFailure(
					temporal::FailureDomain::kFrameGeneration,
					result.message.empty() ? "The selected latency provider rejected a render-submit marker." : result.message);
			}
		}
		cs::render::annotation::SetMarker("Temporal/Latency/SimulationEnd");
		cs::render::annotation::SetMarker("Temporal/Latency/RenderSubmitStart");
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
			if (_impl->latencySdkActive.load(std::memory_order_acquire) &&
				_impl->renderer.ShouldUseFrameGenerationThisFrame()) {
				const auto result = _impl->activePresentation->SetLatencyMarker(
					temporal::LatencyMarker::kRenderSubmitEnd,
					static_cast<std::uint32_t>(frame));
				if (!result.Succeeded() && !_impl->latencyFailureReported.exchange(
											   true, std::memory_order_acq_rel)) {
					PostFailure(temporal::FailureDomain::kFrameGeneration,
						result.message.empty() ? "The selected latency provider rejected the "
												 "render-submit end marker." :
												 result.message);
				}
			}
			cs::render::annotation::SetMarker("Temporal/Latency/RenderSubmitEnd");
		}
		if (_impl->latencySdkActive.load(std::memory_order_acquire) &&
			_impl->renderer.ShouldUseFrameGenerationThisFrame()) {
			const auto result = _impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kPresentStart,
				static_cast<std::uint32_t>(frame));
			if (!result.Succeeded() && !_impl->latencyFailureReported.exchange(
										   true, std::memory_order_acq_rel)) {
				PostFailure(temporal::FailureDomain::kFrameGeneration,
					result.message.empty() ? "The selected latency provider "
											 "rejected the present-start marker." :
											 result.message);
			}
		}
		cs::render::annotation::SetMarker("Temporal/Latency/PresentStart");
	}

	void TemporalPipeline::EndPresentAttempt(UINT a_flags,
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
		if (_impl->latencySdkActive.load(std::memory_order_acquire) &&
			_impl->renderer.ShouldUseFrameGenerationThisFrame()) {
			const auto result = _impl->activePresentation->SetLatencyMarker(
				temporal::LatencyMarker::kPresentEnd,
				static_cast<std::uint32_t>(frame));
			if (!result.Succeeded() && !_impl->latencyFailureReported.exchange(
										   true, std::memory_order_acq_rel)) {
				PostFailure(
					temporal::FailureDomain::kFrameGeneration,
					result.message.empty() ? "The selected latency provider rejected the present-end marker." : result.message);
			}
		}
		cs::render::annotation::SetMarker("Temporal/Latency/PresentEnd");
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
		DXGI_SWAP_CHAIN_DESC*, std::vector<D3D_FEATURE_LEVEL>& a_featureLevels)
	{
		if (!_impl->requestFrozen.load(std::memory_order_acquire)) {
			_impl->creationState.store(TemporalCreationState::kNative,
				std::memory_order_release);
			PostFailure(temporal::FailureDomain::kConfiguration,
				"Graphics creation arrived before the post-preset temporal "
				"request freeze.");
			return;
		}

		temporal::RequestedTopology request;
		{
			std::scoped_lock lock(_impl->mutex);
			request = *_impl->topology.StartupRequest();
		}
		const bool streamlineRequested =
			request.upscalingEligible || request.frameGenerationEligible;
		temporal::ConfigureTemporalFeatureLevels(request, a_featureLevels);
		if (streamlineRequested) {
			const bool loadDlssFrameGeneration =
				request.frameGenerationEligible;
			_impl->streamline.LoadInterposer(
				request.streamlineLogLevel, request.upscalingEligible,
				request.upscalingEligible,
				loadDlssFrameGeneration,
				request.frameGenerationEligible);
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
			request = *_impl->topology.StartupRequest();
		}
		if (!request.upscalingEligible && !request.frameGenerationEligible) {
			return std::nullopt;
		}
		if (!a_context.realCreate || !a_context.swapChainDesc ||
			!a_context.swapChain || !a_context.device ||
			!a_context.immediateContext) {
			PostFailure(temporal::FailureDomain::kTransport,
				"The swap-chain replacement callback received an incomplete "
				"creation request.");
			return std::nullopt;
		}
		const double refreshRate =
			GetRefreshRate(a_context.swapChainDesc->OutputWindow);
		const bool wantsFrameGeneration =
			request.frameGenerationEligible &&
			request.frameGenerationEnabled &&
			request.frameGeneration != temporal::FrameGenerationMethod::kOff;
		bool frameGenerationEligible = wantsFrameGeneration;
		if (frameGenerationEligible &&
			!a_context.swapChainDesc->Windowed) {
			frameGenerationEligible = false;
			PostFailure(
				temporal::FailureDomain::kFrameGeneration,
				"Frame generation requires windowed or borderless presentation; "
				"plain D3D12 presentation remains available.");
		}
		_impl->creationState.store(TemporalCreationState::kCreating,
			std::memory_order_release);
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* immediateContext = nullptr;
		D3D_FEATURE_LEVEL featureLevel{};
		const HRESULT deviceResult = a_context.realCreate(
			a_context.adapter, a_context.driverType, a_context.software,
			a_context.flags, a_context.featureLevels, a_context.featureLevelCount,
			a_context.sdkVersion, nullptr, nullptr, &device, &featureLevel,
			&immediateContext);
		if (FAILED(deviceResult) || !device || !immediateContext) {
			if (immediateContext) {
				immediateContext->Release();
			}
			if (device) {
				device->Release();
			}
			_impl->creationState.store(TemporalCreationState::kNative,
				std::memory_order_release);
			PostFailure(temporal::FailureDomain::kTransport,
				std::format("Frame-generation proxy device creation failed "
							"({:#010x}); native creation remains safe.",
					static_cast<std::uint32_t>(deviceResult)));
			return std::nullopt;
		}

		HRESULT proxyResult = E_FAIL;
		temporal::IFrameGenerationProvider* provider = nullptr;
		if (frameGenerationEligible &&
			request.frameGenerationEnabled &&
			request.frameGeneration !=
				temporal::FrameGenerationMethod::kOff) {
			provider =
				_impl->FrameGenerationProvider(
					request.frameGeneration);
		}
		const std::string_view providerName =
			provider ? provider->Name() : "plain D3D12";
		try {
			proxyResult = _impl->swapChain.Initialize(
				a_context.adapter, device, immediateContext, *a_context.swapChainDesc,
				&_impl->streamline, provider,
				features::TemporalPresentationCallbacks{
					.clearCapture =
						[] {
							TemporalPipeline::Get()
								.Renderer()
								.ClearFrameGenerationCaptureState();
						},
					.recordFailure =
						[](const char* a_reason) {
							auto& pipeline = TemporalPipeline::Get();
							pipeline.RecordFrameGenerationFailure();
							pipeline.PostFailure(
								temporal::FailureDomain::kFrameGeneration, a_reason);
						},
					.queryFrameState =
						[this] {
							const auto* upscaling = &TemporalPipeline::Get().Renderer();
							const auto [width, height] = upscaling->GetRenderSize();
							const auto jitter = upscaling->GetAppliedJitter();
							temporal::FrameGenerationRequest result;
							bool frozen = false;
							const auto slot = _impl->swapChain.GetFrameSlot();
							{
								std::scoped_lock lock(_impl->mutex);
								if (slot < _impl->frozenFrameConstants.size() &&
									_impl->frozenFrameConstants[slot]) {
									result =
										*_impl->frozenFrameConstants[slot];
									frozen = true;
								}
								result.configuration =
									_impl->topology.Effective()
										.frameGenerationConfiguration;
							}
							result.realFrame =
								TemporalPipeline::Get().CurrentRealFrame();
							result.renderWidth = width;
							result.renderHeight = height;
							if (!frozen) {
								result.jitterX = jitter.x;
								result.jitterY = jitter.y;
								result.frameTimeMilliseconds =
									RE::BSTimer::GetSingleton()
										? RE::BSTimer::GetSingleton()->realTimeDelta *
											  1000.0f
										: 0.0f;
							}
							result.enabled =
								upscaling->ShouldUseFrameGenerationThisFrame();
							if (!frozen) {
								result.resetHistory =
									TemporalPipeline::Get()
										.FrameGenerationResetPending();
							}
							if (result.color.resourceFormat ==
								DXGI_FORMAT_UNKNOWN) {
								result.color = {
									.resourceFormat =
										DXGI_FORMAT_R8G8B8A8_UNORM,
									.viewFormat =
										DXGI_FORMAT_R8G8B8A8_UNORM,
									.range = temporal::ColorRange::kFull,
									.transfer =
										temporal::TransferFunction::kGamma22,
									.primaries =
										temporal::ColorPrimaries::kUnspecified,
									.stage =
										temporal::ColorStage::kPostTonemapLut,
									.alpha = temporal::AlphaMode::kIgnored,
									.exposure =
										temporal::ExposureMode::kAutomatic
								};
							}
							if (!result.camera.valid) {
								const auto& snapshot =
									cs::engine::GetFrameBuffer();
								const auto* graphics =
									cs::engine::GetGraphicsState();
								result.camera =
									temporal::BuildFrameGenerationCamera(
										snapshot,
										graphics ? graphics->screenWidth : 0,
										graphics ? graphics->screenHeight : 0);
							}
							return result;
						},
					.bindD3D12CaptureTarget =
						[](ID3D12Device* a_device, HWND a_window) {
							auto* renderDoc =
								features::RenderDoc::GetSingleton();
							if (renderDoc->IsHealthy()) {
								renderDoc->BindD3D12CaptureTarget(
									a_device,
									a_window);
							}
						},
					.unbindD3D12CaptureTarget =
						[](ID3D12Device* a_device) {
							features::RenderDoc::GetSingleton()
								->UnbindD3D12CaptureTarget(a_device);
						} });
			if (SUCCEEDED(proxyResult) && provider &&
				_impl->swapChain.IsFrameGenerationReady()) {
				_impl->activePresentation = provider;
			} else {
				_impl->activePresentation = nullptr;
			}
		} catch (const std::exception& e) {
			if (FAILED(_impl->swapChain.Rollback())) {
				PostFailure(
					temporal::FailureDomain::kTransport,
					"Frame-generation proxy rollback failed after initialization error.");
			}
			PostFailure(temporal::FailureDomain::kTransport, e.what());
		} catch (...) {
			if (FAILED(_impl->swapChain.Rollback())) {
				PostFailure(
					temporal::FailureDomain::kTransport,
					"Frame-generation proxy rollback failed after initialization error.");
			}
			PostFailure(
				temporal::FailureDomain::kTransport,
				"Frame-generation proxy construction raised a non-standard exception.");
		}
		if (FAILED(proxyResult) || !_impl->swapChain.GetProxy()) {
			immediateContext->Release();
			device->Release();
			_impl->creationState.store(TemporalCreationState::kNative,
				std::memory_order_release);
			if (SUCCEEDED(proxyResult)) {
				proxyResult = E_FAIL;
			}
			L->error(
				"{} proxy construction failed ({:#010x}); native creation remains safe",
				providerName, static_cast<std::uint32_t>(proxyResult));
			return std::nullopt;
		}

		*a_context.device = device;
		*a_context.immediateContext = immediateContext;
		*a_context.swapChain = _impl->swapChain.AcquireProxy();
		if (a_context.featureLevel) {
			*a_context.featureLevel = featureLevel;
		}
		_impl->creationState.store(TemporalCreationState::kProxy,
			std::memory_order_release);
		const std::string_view activePresentationName =
			_impl->swapChain.IsFrameGenerationReady()
				? providerName
				: "plain D3D12";
		L->info("Temporal pipeline published the {} D3D11-facing proxy at {:.2f} Hz",
			activePresentationName, refreshRate);
		return S_OK;
	}

	void TemporalPipeline::OnPostCreateDeviceAndSwapChain(
		IDXGIAdapter* a_adapter, ID3D11Device** a_device,
		IDXGISwapChain** a_swapChain)
	{
		temporal::RequestedTopology request;
		{
			std::scoped_lock lock(_impl->mutex);
			if (!_impl->topology.StartupRequest()) {
				return;
			}
			request = *_impl->topology.StartupRequest();
		}

		const bool temporalProxyPath =
			a_swapChain && _impl->swapChain.Owns(*a_swapChain);
		if (temporalProxyPath && a_device) {
			_impl->swapChain.SetOutwardD3D11Device(*a_device);
		}
		auto srAdmission = temporal::InitializeSelectedSuperResolution(
			request,
			[&](temporal::SuperResolutionMethod a_method)
				-> temporal::ProviderResult {
				temporal::SuperResolutionInitContext init{};
				init.device = a_device ? *a_device : nullptr;
				if (a_method == temporal::SuperResolutionMethod::kFSR3 ||
					a_method == temporal::SuperResolutionMethod::kFSR4 ||
					a_method == temporal::SuperResolutionMethod::kDLSS) {
					if (!_impl->swapChain.IsBridgeReady()) {
						return {
							.code =
								temporal::ProviderResultCode::kUnavailable,
							.message =
								"Native super resolution has no usable D3D12 "
								"transport after presentation initialization."
						};
					}
					init.device = _impl->swapChain.GetD3D12Device();
					return _impl->SuperResolutionProvider(a_method)
						->Initialize(init);
				}
				return {
					.code = temporal::ProviderResultCode::kUnavailable,
					.message =
						"The selected super-resolution method has no usable provider."
				};
			});
		if (!srAdmission.detail.empty())
			L->warn("{}", srAdmission.detail);
		if (request.upscalingEligible && _impl->streamline.featureDLSS &&
			_impl->streamline.slDLSSGetOptimalSettings) {
			srAdmission.methods[static_cast<std::size_t>(
				temporal::SuperResolutionMethod::kDLSS)] = true;
		}
		if (request.upscalingEligible && temporalProxyPath &&
			_impl->streamline.featureFSR &&
			_impl->streamline.slFSRGetOptimalSettings) {
			srAdmission.methods[static_cast<std::size_t>(
				temporal::SuperResolutionMethod::kFSR3)] = true;
			srAdmission.methods[static_cast<std::size_t>(
				temporal::SuperResolutionMethod::kFSR4)] =
				_impl->streamline
					.ValidateFSRAlgorithm(
						sl::FSRAlgorithm::eFSR4)
					.Succeeded();
		}
		_impl->latencySdkActive.store(
			_impl->latencyHooksInstalled.load(std::memory_order_acquire) &&
				temporalProxyPath && _impl->activePresentation &&
				request.frameGeneration ==
					temporal::FrameGenerationMethod::kDLSSG &&
				_impl->activePresentation->IsReady(),
			std::memory_order_release);

		temporal::SessionTopology session;
		session.valid = true;
		session.proxyInstalled = temporalProxyPath;
		session.bridgePresent = _impl->swapChain.IsBridgeReady();
		session.latencyHooksInstalled =
			_impl->latencyHooksInstalled.load(std::memory_order_acquire);
		session.admittedSr = srAdmission.methods;
		session.rejectionReason = srAdmission.detail;
		session.admittedFg[static_cast<std::size_t>(
			temporal::FrameGenerationMethod::kOff)] = temporalProxyPath;
		const bool fgDisplayEligible = temporalProxyPath;
		session.admittedFg[static_cast<std::size_t>(
			temporal::FrameGenerationMethod::kFSR3)] =
			request.frameGenerationEligible && fgDisplayEligible &&
			_impl->FrameGenerationProvider(
				temporal::FrameGenerationMethod::kFSR3)
				->IsAvailable();
		session.admittedFg[static_cast<std::size_t>(
			temporal::FrameGenerationMethod::kDLSSG)] =
			request.frameGenerationEligible && fgDisplayEligible &&
			_impl->streamlinePresentation.IsAvailable();
		session.admittedFg[static_cast<std::size_t>(
			temporal::FrameGenerationMethod::kFSR4)] =
			request.frameGenerationEligible && fgDisplayEligible &&
			_impl->streamlineFsr4Presentation.IsAvailable();
		if (request.frameGenerationEnabled &&
			request.frameGeneration != temporal::FrameGenerationMethod::kOff &&
			!_impl->swapChain.IsFrameGenerationReady()) {
			session.admittedFg[static_cast<std::size_t>(
				request.frameGeneration)] = false;
		}
		session.activeFg =
			_impl->swapChain.IsFrameGenerationReady()
				? request.frameGeneration
				: temporal::FrameGenerationMethod::kOff;
		if (a_adapter) {
			DXGI_ADAPTER_DESC desc{};
			if (SUCCEEDED(a_adapter->GetDesc(&desc))) {
				session.adapterLuid =
					(static_cast<std::uint64_t>(
						 static_cast<std::uint32_t>(desc.AdapterLuid.HighPart))
						<< 32) |
					desc.AdapterLuid.LowPart;
			}
		}

		bool admitted = false;
		temporal::UpscalingSettings admittedSettings;
		{
			std::scoped_lock lock(_impl->mutex);
			admitted = _impl->topology.Admit(std::move(session));
			if (admitted) {
				const auto& effective = _impl->topology.Effective();
				_impl->effectiveRenderSettings.upscaleMethod =
					static_cast<std::uint32_t>(
						ToFeature(effective.superResolution));
				_impl->effectiveRenderSettings.qualityMode =
					effective.qualityMode;
				_impl->effectiveRenderSettings.enabled =
					effective.superResolutionEnabled;
				_impl->frameGenerationEnabled.store(
					effective.frameGenerationEnabled &&
						effective.frameGeneration !=
							temporal::FrameGenerationMethod::kOff &&
						!_impl->frameGenerationQuarantined,
					std::memory_order_release);
				admittedSettings = _impl->effectiveRenderSettings;
			}
		}
		if (!admitted) {
			PostFailure(temporal::FailureDomain::kConfiguration,
				"The temporal session topology was published more than once.");
		}
		if (admitted && !temporalProxyPath) {
			_impl->creationState.store(TemporalCreationState::kNative,
				std::memory_order_release);
		}
		if (admitted) {
			_impl->renderer.ApplyConfiguration(
				admittedSettings, _impl->rendererEligible);
		}
	}

	void TemporalPipeline::OnD3D11Ready(IDXGIAdapter* a_adapter,
		ID3D11Device* a_device)
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

	void TemporalPipeline::PostFailure(temporal::FailureDomain a_domain,
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
				auto impact = temporal::ClassifyFailure(
					a_domain, effective.superResolution, effective.frameGeneration);
				const auto& session = _impl->topology.Session();
				const bool runtimeActive = session && session->valid;
				if (runtimeActive) {
					_impl->topology.Quarantine(impact,
						_impl->configurationRevision.fetch_add(
							1, std::memory_order_acq_rel) +
							1,
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
					"Temporal trace seq={} real={} engine={} slot={} event={} "
					"hr={:#010x}",
					entry.sequence, entry.realFrame, entry.engineFrame, entry.slot,
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
				_impl->configurationRevision.fetch_add(1, std::memory_order_acq_rel) +
				1;
			std::scoped_lock lock(_impl->mutex);
			_impl->topology.FailSuperResolutionToNative(revision, std::move(a_reason));
			_impl->effectiveRenderSettings.upscaleMethod =
				static_cast<std::uint32_t>(
					features::Upscaling::UpscaleMethod::kTAA);
			_impl->effectiveRenderSettings.enabled = true;
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
		std::uint32_t a_width, std::uint32_t a_height) noexcept
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
		return _impl->resetEpochs.SuperResolutionPending() ||
		       (UsesSharedStreamlineConstants(effective) &&
				   _impl->resetEpochs.FrameGenerationPending());
	}

	bool TemporalPipeline::ArmFrameGenerationReset() noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		return _impl->resetEpochs.ArmFrameGeneration();
	}

	bool TemporalPipeline::FrameGenerationResetPending() const noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		const auto& effective = _impl->topology.Effective();
		return _impl->resetEpochs.FrameGenerationPending() ||
		       (UsesSharedStreamlineConstants(effective) &&
				   _impl->resetEpochs.SuperResolutionPending());
	}

	void TemporalPipeline::ConsumeSuperResolutionReset(bool a_completed) noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		_impl->resetEpochs.ConsumeSuperResolution(a_completed);
	}

	void TemporalPipeline::FreezeFrameConstants(
		std::uint32_t a_slot,
		const temporal::FrameGenerationRequest& a_request) noexcept
	{
		if (a_slot >= _impl->frozenFrameConstants.size()) {
			return;
		}
		std::scoped_lock lock(_impl->mutex);
		auto& frozen = _impl->frozenFrameConstants[a_slot];
		if (frozen && frozen->realFrame == a_request.realFrame &&
			frozen->camera.engineFrame == a_request.camera.engineFrame) {
			return;
		}
		frozen = a_request;
	}

	bool TemporalPipeline::ApplyFrozenFrameConstants(
		temporal::SuperResolutionRequest& a_request) const noexcept
	{
		std::scoped_lock lock(_impl->mutex);
		for (const auto& frozen : _impl->frozenFrameConstants) {
			if (!frozen || frozen->realFrame != a_request.realFrame ||
				frozen->camera.engineFrame != a_request.engineFrame) {
				continue;
			}
			a_request.jitterX = frozen->jitterX;
			a_request.jitterY = frozen->jitterY;
			a_request.frameTimeMilliseconds =
				frozen->frameTimeMilliseconds;
			a_request.color = frozen->color;
			a_request.camera = frozen->camera;
			return true;
		}
		return false;
	}

	bool TemporalPipeline::RecordInputPacket(std::uint64_t a_engineFrame,
		temporal::Extent a_renderExtent,
		temporal::Extent a_outputExtent,
		std::uint32_t a_slot) noexcept
	{
		if (a_slot >= _impl->frames.size()) {
			PostFailure(temporal::FailureDomain::kEngine,
				"Frame slot index is out of range.");
			return false;
		}
		std::string failure;
		bool valid = false;
		{
			std::scoped_lock lock(_impl->mutex);
			auto& frame = _impl->frames[a_slot];
			valid = frame.Capture({ .realFrame = _impl->latency.Frame(),
									  .engineFrame = a_engineFrame,
									  .slot = a_slot },
				a_renderExtent, a_outputExtent);
			if (valid) {
				_impl->currentFrameSlot = a_slot;
				_impl->Trace(TemporalTraceEvent::kInputPacket, frame);
			} else {
				failure = frame.Failure();
			}
		}
		if (!valid) {
			PostFailure(temporal::FailureDomain::kPresentation,
				failure.empty() ? "Frame-generation input packet was rejected" : failure);
		}
		return valid;
	}

	bool TemporalPipeline::PreparePresent(std::uint32_t a_slot) noexcept
	{
		if (a_slot >= _impl->frames.size()) {
			return false;
		}
		std::string failure;
		bool prepared = false;
		{
			std::scoped_lock lock(_impl->mutex);
			auto& frame = _impl->frames[a_slot];
			if (frame.Phase() == temporal::FramePhase::kPresentPrepared) {
				return true;
			}
			prepared = frame.PreparePresent();
			if (prepared) {
				_impl->Trace(TemporalTraceEvent::kPresentPrepared, frame);
			} else {
				failure = frame.Failure();
			}
		}
		if (!prepared) {
			PostFailure(temporal::FailureDomain::kPresentation,
				failure.empty() ? "Frame-generation presentation packet was rejected" : failure);
		}
		return prepared;
	}

	void TemporalPipeline::SetFrameGenerationPrepared(std::uint32_t a_slot,
		bool a_prepared) noexcept
	{
		if (a_slot >= _impl->frames.size()) {
			return;
		}
		std::scoped_lock lock(_impl->mutex);
		_impl->frames[a_slot].SetFrameGenerationPrepared(a_prepared);
	}

	void TemporalPipeline::RecordPresentAttempt(std::uint32_t a_slot, UINT a_flags,
		HRESULT a_result) noexcept
	{
		if (a_slot >= _impl->frames.size()) {
			return;
		}
		std::string failure;
		{
			std::scoped_lock lock(_impl->mutex);
			auto& frame = _impl->frames[a_slot];
			const bool testOnly = (a_flags & DXGI_PRESENT_TEST) != 0;
			const bool retryable = a_result == DXGI_ERROR_WAS_STILL_DRAWING;
			const bool accepted = SUCCEEDED(a_result);
			const bool occluded = a_result == DXGI_STATUS_OCCLUDED;
			const bool consumedFrameGenerationReset =
				frame.FrameGenerationPrepared() && !testOnly && a_result == S_OK;
			_impl->Trace(testOnly ? TemporalTraceEvent::kPresentTest : retryable ? TemporalTraceEvent::kPresentRetry :
																   occluded      ? TemporalTraceEvent::kPresentOccluded :
																   accepted      ? TemporalTraceEvent::kPresentAccepted :
																				   TemporalTraceEvent::kPresentFailed,
				frame, a_result);
			if (!frame.PresentAttempt(testOnly, accepted, retryable) ||
				(!testOnly && accepted && !retryable && !frame.Retire())) {
				failure = frame.Failure();
			} else if (consumedFrameGenerationReset) {
				_impl->resetEpochs.ConsumeFrameGeneration(true);
			}
			if (!testOnly && !retryable) {
				_impl->frozenFrameConstants[a_slot].reset();
			}
		}
		if (!failure.empty()) {
			PostFailure(temporal::FailureDomain::kPresentation, failure);
		}
	}

	void TemporalPipeline::RecordGeneratedFrames(
		std::optional<std::uint32_t> a_count) noexcept
	{
		if (a_count) {
			_impl->generatedFrames.fetch_add(*a_count, std::memory_order_relaxed);
		}
		_impl->generatedFrameCountAvailable.store(a_count.has_value(),
			std::memory_order_relaxed);
	}

	void TemporalPipeline::RecordPresentedFrames(
		std::optional<std::uint32_t> a_count) noexcept
	{
		if (a_count) {
			_impl->providerPresentedFrames.fetch_add(*a_count,
				std::memory_order_relaxed);
		}
		_impl->providerPresentedFrameCountAvailable.store(a_count.has_value(),
			std::memory_order_relaxed);
	}

	FrameGenerationCpuTimingCollector<>::Scope
	TemporalPipeline::MeasureFrameGenerationCpuPhase(
		FrameGenerationCpuPhase a_phase) noexcept
	{
		return _impl->frameGenerationCpuTimings.Measure(a_phase);
	}

	void TemporalPipeline::RecordFrameGenerationFrameTimeInput(
		float a_milliseconds) noexcept
	{
		_impl->frameGenerationCpuTimings.RecordFrameTimeInput(
			static_cast<double>(a_milliseconds));
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
		status.transitionInFlight = _impl->topology.TransitionInFlight();
		status.failure =
			_impl->failure.empty() ? status.session.rejectionReason : _impl->failure;
		if (status.failureDomain == temporal::FailureDomain::kNone &&
			!status.session.rejectionReason.empty())
			status.failureDomain = temporal::FailureDomain::kSuperResolution;
		return status;
	}

	temporal::EffectiveConfiguration
	TemporalPipeline::GetEffectiveConfiguration() const
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
	TemporalPipeline::GetFrameGenerationDiagnostics(
		bool a_includeLiveEngineState) const noexcept
	{
		auto cpuTimings = _impl->frameGenerationCpuTimings.GetSnapshot();
		if (!_impl->detailedTracing.load(std::memory_order_acquire)) {
			cpuTimings = {};
		}
		const auto* state = a_includeLiveEngineState
			? cs::engine::GetGraphicsState()
			: nullptr;
		const auto& camera = a_includeLiveEngineState
			? cs::engine::GetFrameBuffer()
			: engine::FrameBufferSnapshot{};
		const float fov =
			camera.valid
				? cs::engine::VerticalFieldOfViewFromWorldToClip(
					  camera.data.CurrFrameWorldToClip)
				: 0.0f;
		const std::int64_t frameDelta =
			camera.valid && state
				? static_cast<std::int64_t>(state->frameCount) -
					  static_cast<std::int64_t>(camera.frameCount)
				: 0;
		bool ready = false;
		bool active = false;
		temporal::PresentInputRetirementDiagnostics retirementDiagnostics;
		{
			std::scoped_lock lock(_impl->mutex);
			const auto& configuration = _impl->topology.Effective();
			const bool configured =
				configuration.frameGeneration != temporal::FrameGenerationMethod::kOff;
			const bool effective =
				configuration.frameGenerationEnabled &&
				_impl->frameGenerationEnabled.load(std::memory_order_acquire);
			ready = _impl->swapChain.IsFrameGenerationReady();
			retirementDiagnostics = _impl->swapChain.GetInputRetirementDiagnostics();
			const auto& frame = _impl->frames[_impl->currentFrameSlot];
			active = temporal::IsFrameGenerationActive(
				configured, effective, ready, _impl->latency.Frame(),
				state
					? std::optional<std::uint64_t>{ state->frameCount }
					: frame.Identity().engineFrame
					? std::optional<std::uint64_t>{
						  frame.Identity().engineFrame }
					: std::nullopt,
				frame);
		}
		return {
			.ready = ready,
			.active = active,
			.inputsCaptured = _impl->inputsCaptured.load(std::memory_order_relaxed),
			.hudlessPending =
				_impl->hudlessCapturePending.load(std::memory_order_relaxed),
			.alphaConditioned =
				_impl->alphaConditioned.load(std::memory_order_relaxed),
			.conditionedCaptures =
				_impl->conditionedCaptures.load(std::memory_order_relaxed),
			.rawCaptures = _impl->rawCaptures.load(std::memory_order_relaxed),
			.dispatches =
				_impl->frameGenerationDispatches.load(std::memory_order_relaxed),
			.generatedFrames = _impl->generatedFrames.load(std::memory_order_relaxed),
			.generatedFrameCountAvailable =
				_impl->generatedFrameCountAvailable.load(std::memory_order_relaxed),
			.providerPresentedFrames =
				_impl->providerPresentedFrames.load(std::memory_order_relaxed),
			.providerPresentedFrameCountAvailable =
				_impl->providerPresentedFrameCountAvailable.load(
					std::memory_order_relaxed),
			.capabilities =
				_impl->streamline.GetDLSSGCapabilities(),
			.failures =
				_impl->frameGenerationFailures.load(std::memory_order_relaxed),
			.cameraValid = camera.valid && fov > 0.0f,
			.cameraFrameDelta = frameDelta,
			.cameraFovDegrees =
				static_cast<double>(fov) * 180.0 /
				std::numbers::pi,
			.cpuTiming = cpuTimings,
			.inputRetirement = retirementDiagnostics
		};
	}

	temporal::FrameGenerationCapabilities
	TemporalPipeline::GetFrameGenerationCapabilities() const noexcept
	{
		return _impl->streamline.GetDLSSGCapabilities();
	}

	TemporalPipeline::FrameGenerationCaptureResources
	TemporalPipeline::GetFrameGenerationCaptureResources() const noexcept
	{
		const auto captureTexture = [](const auto* a_texture) {
			return a_texture ? FrameGenerationCaptureResources::
			                       Texture{ .resource = a_texture->texture11.get(),
									   .srv = a_texture->srv11.get(),
									   .uav = a_texture->uav11.get() } :
			                   FrameGenerationCaptureResources::Texture{};
		};
		return { .ready = _impl->swapChain.IsFrameGenerationReady(),
			.motion = captureTexture(_impl->swapChain.GetMotionTexture()),
			.depth = captureTexture(_impl->swapChain.GetDepthTexture()),
			.hudlessColor = captureTexture(_impl->swapChain.GetHudlessTexture()),
			.width = _impl->swapChain.GetWidth(),
			.height = _impl->swapChain.GetHeight(),
			.frameSlot = _impl->swapChain.GetFrameSlot() };
	}

	bool TemporalPipeline::AcquireFrameGenerationInputWrite() noexcept
	{
		return _impl->swapChain.AcquireFrameGenerationInputWrite();
	}

	void TemporalPipeline::SetFrameGenerationInputsReady(bool a_ready) noexcept
	{
		if (!a_ready) {
			std::scoped_lock lock(_impl->mutex);
			for (auto& frame : _impl->frames) {
				frame.Abandon();
			}
			for (auto& frozen : _impl->frozenFrameConstants) {
				frozen.reset();
			}
		}
		_impl->swapChain.SetFrameGenerationInputsReady(a_ready);
	}

	void TemporalPipeline::FailFrameGenerationFrame(const char* a_reason) noexcept
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
		_impl->alphaConditioned.store(a_alphaConditioned, std::memory_order_relaxed);
		if (a_alphaConditioned) {
			_impl->conditionedCaptures.fetch_add(1, std::memory_order_relaxed);
		} else {
			_impl->rawCaptures.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void TemporalPipeline::SetHudlessCapturePending(bool a_pending) noexcept
	{
		_impl->hudlessCapturePending.store(a_pending, std::memory_order_relaxed);
	}

	void TemporalPipeline::RecordFrameGenerationDispatch() noexcept
	{
		_impl->frameGenerationDispatches.fetch_add(1, std::memory_order_relaxed);
	}

	void TemporalPipeline::RecordFrameGenerationFailure() noexcept
	{
		_impl->frameGenerationFailures.fetch_add(1, std::memory_order_relaxed);
	}

	temporal::ProviderResult TemporalPipeline::EvaluateSuperResolution(
		temporal::SuperResolutionMethod a_method,
		const temporal::SuperResolutionRequest& a_request)
	{
		auto* provider = _impl->SuperResolutionProvider(a_method);
		if (!provider) {
			return { .code = temporal::ProviderResultCode::kUnavailable,
				.message = "No external super-resolution provider is active." };
		}
		return _impl->swapChain.EvaluateD3D12SuperResolution(
			*provider, a_request);
	}

	temporal::SuperResolutionSizeResult
	TemporalPipeline::QuerySuperResolutionRenderSize(
		temporal::SuperResolutionMethod a_method,
		const temporal::SuperResolutionSizeRequest& a_request)
	{
		const auto status = GetStatus();
		const auto index = static_cast<std::size_t>(a_method);
		if (!status.session.valid || index >= status.session.admittedSr.size() ||
			!status.session.admittedSr[index]) {
			return {
				.result = {
					.code = temporal::ProviderResultCode::kUnavailable,
					.message =
						"The selected super-resolution provider was not admitted." }
			};
		}

		if (auto* provider = _impl->SuperResolutionProvider(a_method)) {
			return provider->QueryRenderSize(a_request);
		} else {
			return { .result = { .code = temporal::ProviderResultCode::kUnavailable,
						 .message =
							 "No external super-resolution provider is active." } };
		}
	}

	bool TemporalPipeline::IsSuperResolutionRuntimeReady(
		temporal::SuperResolutionMethod a_method) const noexcept
	{
		switch (a_method) {
		case temporal::SuperResolutionMethod::kFSR3:
			return _impl->streamline.featureFSR &&
				_impl->streamline.slFSRGetOptimalSettings;
		case temporal::SuperResolutionMethod::kFSR4:
			return _impl->streamline
				.ValidateFSRAlgorithm(sl::FSRAlgorithm::eFSR4)
				.Succeeded();
		case temporal::SuperResolutionMethod::kDLSS:
			return _impl->streamline.featureDLSS;
		default:
			return true;
		}
	}

	std::unique_ptr<cs::buffer::Texture2D>
	TemporalPipeline::CreateSuperResolutionTexture(
		const D3D11_TEXTURE2D_DESC& a_desc,
		std::string_view a_name)
	{
		if (_impl->swapChain.IsBridgeReady()) {
			return _impl->swapChain.CreateSharedTexture(a_desc, a_name);
		}
		return std::make_unique<cs::buffer::Texture2D>(a_desc);
	}

	temporal::ProviderResult TemporalPipeline::DestroySuperResolutionResources(
		temporal::SuperResolutionMethod a_method) noexcept
	{
		if (auto* provider = _impl->SuperResolutionProvider(a_method)) {
			if (a_method == temporal::SuperResolutionMethod::kDLSS &&
				!_impl->streamline.HasDLSSResources()) {
				return {
					.code = temporal::ProviderResultCode::kSuccess
				};
			}
			if (a_method == temporal::SuperResolutionMethod::kFSR3 ||
				a_method == temporal::SuperResolutionMethod::kFSR4) {
				if (!_impl->streamline.HasFSRResources()) {
					return {
						.code = temporal::ProviderResultCode::kSuccess
					};
				}
			}
			const HRESULT drainResult = _impl->swapChain.Drain();
			if (FAILED(drainResult)) {
				return {
					.code = temporal::ProviderResultCode::kFailure,
					.hresult = drainResult,
					.message =
						"The D3D12 queue did not drain before super-resolution "
						"resource destruction.",
					.failureDomain = temporal::FailureDomain::kTransport
				};
			}
			return provider->DestroyAfterDrain();
		}
		return { .code = temporal::ProviderResultCode::kSuccess };
	}

	temporal::FidelityFXCapabilities
	TemporalPipeline::GetFidelityFXCapabilities() const noexcept
	{
		return _impl->streamline.GetFidelityFXCapabilities();
	}

	FrameGenerationDebugTexture TemporalPipeline::GetFrameGenerationDebugTexture(
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
		return resource && resource->srv11 ? FrameGenerationDebugTexture{ .srv = resource->srv11.get(),
			.width = _impl->swapChain.GetWidth(),
			.height =
				_impl->swapChain.GetHeight() } :
		                                     FrameGenerationDebugTexture{};
	}

}  // namespace cs::render
