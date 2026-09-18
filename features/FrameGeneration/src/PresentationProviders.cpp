#include "PresentationProviders.h"

#include <algorithm>
#include <utility>

#include <winrt/base.h>

#include "FidelityFX.h"
#include "Streamline.h"

namespace cs::features
{
	namespace
	{
		using render::temporal::ProviderResult;
		using render::temporal::ProviderResultCode;

		ProviderResult Success() { return { .code = ProviderResultCode::kSuccess }; }

		ProviderResult Failure(
			std::string a_message, std::int64_t a_result = 0,
			render::temporal::FailureDomain a_domain =
				render::temporal::FailureDomain::kFrameGeneration)
		{
			return { .code = ProviderResultCode::kFailure,
				.sdkResult = a_result,
				.message = std::move(a_message),
				.failureDomain = a_domain };
		}

		ProviderResult FailureHresult(std::string a_message, HRESULT a_result)
		{
			return { .code = ProviderResultCode::kFailure,
				.hresult = a_result,
				.message = std::move(a_message),
				.failureDomain =
					render::temporal::FailureDomain::kPresentation };
		}
	}  // namespace

	FidelityFXPresentation::FidelityFXPresentation(FidelityFX& a_runtime) noexcept
		: _runtime(a_runtime) {}

	const char* FidelityFXPresentation::Name() const noexcept { return "FSR3-FG"; }

	ProviderResult FidelityFXPresentation::PrepareDevice(ID3D12Device** a_device)
	{
		return a_device && *a_device ? Success() : Failure("FidelityFX received no D3D12 device.");
	}

	ProviderResult
	FidelityFXPresentation::PrepareFactory(IDXGIFactory4** a_factory)
	{
		return a_factory && *a_factory ? Success() : Failure("FidelityFX received no DXGI factory.");
	}

	ProviderResult FidelityFXPresentation::CreatePresentation(
		const render::temporal::PresentationCreateContext& a_context,
		IDXGISwapChain4** a_swapChain)
	{
		if (!a_context.device || !a_context.queue || !a_context.factory ||
			!a_context.window || !a_context.description || !a_swapChain) {
			return Failure(
				"FidelityFX presentation creation received an incomplete context.");
		}
		_device = a_context.device;
		const HRESULT result = _runtime.CreateSwapChainContext(
			a_context.device, a_context.queue, a_context.factory, a_context.window,
			*a_context.description, a_swapChain);
		if (FAILED(result)) {
			return FailureHresult(
				"FidelityFX swap-chain creation failed.", result);
		}
		_swapChain = *a_swapChain;
		return Success();
	}

	ProviderResult FidelityFXPresentation::SetPresentationActive(bool)
	{
		return Success();
	}

	ProviderResult FidelityFXPresentation::CreateDisplayResources(
		std::uint32_t a_width, std::uint32_t a_height, DXGI_FORMAT a_format,
		std::uint32_t)
	{
		return _runtime.CreateFrameGenerationContext(_device, a_width, a_height,
				   a_format) ?
		           Success() :
		           Failure("FidelityFX frame-generation context creation failed.");
	}

	ProviderResult FidelityFXPresentation::PrepareFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		FidelityFX::FrameGenerationCameraSnapshot camera{};
		std::copy_n(a_request.camera.right, 3, camera.right);
		std::copy_n(a_request.camera.up, 3, camera.up);
		std::copy_n(a_request.camera.forward, 3, camera.forward);
		std::copy_n(a_request.camera.position, 3, camera.position);
		camera.nearPlane = a_request.camera.nearPlane;
		camera.farPlane = a_request.camera.farPlane;
		camera.verticalFov = a_request.camera.verticalFov;
		camera.frameTimeDelta = a_request.frameTimeMilliseconds;
		camera.frameCount = a_request.camera.engineFrame;
		camera.valid = a_request.camera.valid;
		if (a_request.enabled && !_runtime.SetFrameGenerationCameraData(camera)) {
			return Failure("FidelityFX rejected the frame camera.");
		}
		const bool prepared = _runtime.PresentFrameGeneration(
			a_request.recording.commandList, _swapChain,
			a_request.hudlessColor.resource, a_request.depth.resource,
			a_request.motionVectors.resource, a_request.enabled,
			a_request.renderWidth, a_request.renderHeight, a_request.outputWidth,
			a_request.outputHeight, a_request.jitterX, a_request.jitterY,
			a_request.color);
		return prepared ? Success() : Failure("FidelityFX frame preparation failed.");
	}

	ProviderResult FidelityFXPresentation::SetGenerationEnabled(bool a_enabled)
	{
		if (a_enabled) {
			return Failure(
				"FidelityFX frame generation can only be enabled by frame "
				"preparation.");
		}
		if (!_runtime.SetFrameGenerationEnabled(false)) {
			return Failure("FidelityFX SDK disable failed.");
		}
		_enabled = false;
		return Success();
	}

	ProviderResult FidelityFXPresentation::CancelFrame(
		const render::temporal::FrameGenerationRequest&)
	{
		return SetGenerationEnabled(false);
	}

	ProviderResult FidelityFXPresentation::Quiesce()
	{
		return SetGenerationEnabled(false);
	}

	render::temporal::PresentInputRetirementMode
	FidelityFXPresentation::GetPresentInputRetirementMode() const noexcept
	{
		return render::temporal::PresentInputRetirementMode::kSynchronousPresentQueue;
	}

	ProviderResult FidelityFXPresentation::CollectPresentStatus(UINT, HRESULT)
	{
		return Success();
	}

	ProviderResult FidelityFXPresentation::Sleep(std::uint32_t)
	{
		return Success();
	}

	ProviderResult
	FidelityFXPresentation::SetLatencyMarker(render::temporal::LatencyMarker,
		std::uint32_t)
	{
		return Success();
	}

	ProviderResult FidelityFXPresentation::ReleaseDisplayResources() noexcept
	{
		const auto release = _runtime.DestroyFrameGenerationContextWithStatus();
		auto result =
			release.succeeded ? Success() : Failure("FidelityFX display resources could not be safely released.", release.globalDrainSdkResult);
		result.globalDrainAttempted = release.globalDrainAttempted;
		result.globalDrainCompleted = release.globalDrainCompleted;
		result.globalDrainCpuMicroseconds = release.globalDrainCpuMicroseconds;
		return result;
	}

	ProviderResult FidelityFXPresentation::DestroyAfterDrain() noexcept
	{
		if (!_runtime.DestroySwapChainContext()) {
			return Failure(
				"FidelityFX swap-chain context could not be safely destroyed.");
		}
		_swapChain = nullptr;
		_device = nullptr;
		return Success();
	}

	bool FidelityFXPresentation::IsAvailable() const noexcept
	{
		return _runtime.IsFrameGenerationModuleReady();
	}

	bool FidelityFXPresentation::IsReady() const noexcept
	{
		return _runtime.IsFrameGenerationContextReady();
	}

	StreamlinePresentation::StreamlinePresentation(
		Streamline& a_runtime, Method a_method) noexcept
		: _runtime(a_runtime), _method(a_method) {}

	const char* StreamlinePresentation::Name() const noexcept
	{
		return _method == Method::kDLSSG ? "DLSS-G" : "FSR-G";
	}

	ProviderResult StreamlinePresentation::PrepareDevice(ID3D12Device** a_device)
	{
		return _runtime.PrepareD3D12Device(a_device) ? Success() : Failure("Streamline could not upgrade and bind the D3D12 device.");
	}

	ProviderResult
	StreamlinePresentation::PrepareFactory(IDXGIFactory4** a_factory)
	{
		return _runtime.PrepareDXGIFactory(a_factory) ? Success() : Failure("Streamline could not upgrade the DXGI factory.");
	}

	ProviderResult StreamlinePresentation::CreatePresentation(
		const render::temporal::PresentationCreateContext& a_context,
		IDXGISwapChain4** a_swapChain)
	{
		if (!a_context.device || !a_context.queue || !a_context.factory ||
			!a_context.window || !a_context.description || !a_swapChain ||
			!_runtime.IsD3D12Session()) {
			return Failure(
				std::string(Name()) +
				" presentation creation received an incompatible context.");
		}
		if (!IsAvailable()) {
			return Failure(
				_method == Method::kDLSSG
					? "DLSS-G, Reflex, or PCL is unavailable."
					: "Native FSR-G or its completion-fence API is unavailable.");
		}
		winrt::com_ptr<IDXGISwapChain1> swapChain;
		const HRESULT result = a_context.factory->CreateSwapChainForHwnd(
			a_context.queue, a_context.window, a_context.description, nullptr,
			nullptr, swapChain.put());
		if (FAILED(result)) {
			return FailureHresult(
				"Streamline swap-chain creation failed.", result);
		}
		const HRESULT queryResult =
			swapChain->QueryInterface(IID_PPV_ARGS(a_swapChain));
		if (FAILED(queryResult)) {
			return FailureHresult(
				"Streamline swap-chain does not expose IDXGISwapChain4.",
				queryResult);
		}
		_queue = a_context.queue;
		_presentationActive = true;
		return Success();
	}

	ProviderResult
	StreamlinePresentation::SetPresentationActive(bool a_active)
	{
		const auto result = _method == Method::kDLSSG
			? _runtime.SetDLSSGPresentationActive(a_active)
			: _runtime.SetFSRGPresentationActive(a_active);
		if (result.Succeeded()) {
			_presentationActive = a_active;
		}
		return result;
	}

	ProviderResult StreamlinePresentation::CreateDisplayResources(
		std::uint32_t a_width, std::uint32_t a_height, DXGI_FORMAT a_format,
		std::uint32_t a_bufferCount)
	{
		if (a_format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			return Failure(
				std::string(Name()) +
				" requires the supported SDR swap-chain format.");
		}
		_width = a_width;
		_height = a_height;
		_bufferCount = a_bufferCount;
		if (_method == Method::kDLSSG) {
			_ready = _runtime.ConfigureDLSSG(false, a_width, a_height, a_width,
				a_height, a_bufferCount, true);
		} else {
			render::temporal::FrameGenerationRequest request;
			request.renderWidth = a_width;
			request.renderHeight = a_height;
			request.outputWidth = a_width;
			request.outputHeight = a_height;
			request.color = {
				.resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
				.viewFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
				.range = render::temporal::ColorRange::kFull,
				.transfer = render::temporal::TransferFunction::kGamma22,
				.primaries =
					render::temporal::ColorPrimaries::kUnspecified,
				.stage = render::temporal::ColorStage::kPostTonemapLut,
				.alpha = render::temporal::AlphaMode::kIgnored,
				.exposure =
					render::temporal::ExposureMode::kAutomatic
			};
			_ready = _runtime.ConfigureFSRG(false, request) &&
				_runtime.CheckFSRGCompletionCapability();
		}
		return _ready
			? Success()
			: Failure(std::string(Name()) +
				  " display-resource preflight failed.");
	}

	ProviderResult StreamlinePresentation::PrepareFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		if (!_ready) {
			return Failure(
				std::string(Name()) + " presentation is not ready.");
		}
		const bool configured = _method == Method::kDLSSG
			? _runtime.ConfigureDLSSG(a_request.enabled, a_request.renderWidth,
				  a_request.renderHeight, a_request.outputWidth,
				  a_request.outputHeight, _bufferCount, true)
			: _runtime.ConfigureFSRG(a_request.enabled, a_request);
		if (!configured) {
			return Failure(std::string(Name()) + " options were rejected.");
		}
		_enabled = a_request.enabled;
		if (!a_request.enabled) {
			return _runtime.ClearFrameGenerationTags(
					   static_cast<std::uint32_t>(a_request.realFrame),
					   a_request.recording.commandList) ?
			           Success() :
			           Failure(std::string(Name()) +
						   " invalid input tags could not be cleared.");
		}
		const bool tagged = _method == Method::kDLSSG
			? _runtime.TagDLSSGFrame(a_request)
			: _runtime.TagFSRGFrame(a_request);
		return tagged
			? Success()
			: Failure(std::string(Name()) +
				  " input preparation failed.");
	}

	ProviderResult StreamlinePresentation::CancelFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		const bool tagsCleared = _runtime.ClearFrameGenerationTags(
			static_cast<std::uint32_t>(a_request.realFrame),
			a_request.recording.commandList);
		bool disabled = false;
		if (_method == Method::kDLSSG) {
			disabled = _runtime.ConfigureDLSSG(false, _width, _height, _width,
				_height, _bufferCount, true);
		} else {
			auto request = a_request;
			request.enabled = false;
			disabled = _runtime.ConfigureFSRG(false, request);
		}
		_enabled = false;
		if (!tagsCleared) {
			return Failure(std::string(Name()) +
				" cancellation could not invalidate the frame tags.");
		}
		return disabled
			? Success()
			: Failure(std::string(Name()) +
				  " cancellation could not disable the SDK.");
	}

	ProviderResult StreamlinePresentation::SetGenerationEnabled(bool a_enabled)
	{
		if (a_enabled && !_ready) {
			return Failure(
				std::string(Name()) + " presentation is not ready.");
		}
		if (!a_enabled &&
			((_method == Method::kDLSSG &&
				 !_runtime.HasDLSSGResources()) ||
				(_method == Method::kFSRG && !_ready))) {
			_enabled = false;
			return Success();
		}
		_enabled = a_enabled;
		if (!a_enabled && !_runtime.ClearCurrentFrameGenerationTags()) {
			return Failure(std::string(Name()) +
				" invalid input tags could not be cleared.");
		}
		if (_method == Method::kDLSSG) {
			return _runtime.ConfigureDLSSG(a_enabled, _width, _height, _width,
					   _height, _bufferCount, true) ?
			           Success() :
			           Failure("DLSS-G enablement change failed.");
		}
		render::temporal::FrameGenerationRequest request;
		request.enabled = a_enabled;
		request.renderWidth = _width;
		request.renderHeight = _height;
		request.outputWidth = _width;
		request.outputHeight = _height;
		request.color = {
			.resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
			.viewFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
			.range = render::temporal::ColorRange::kFull,
			.transfer = render::temporal::TransferFunction::kGamma22,
			.primaries =
				render::temporal::ColorPrimaries::kUnspecified,
			.stage = render::temporal::ColorStage::kPostTonemapLut,
			.alpha = render::temporal::AlphaMode::kIgnored,
			.exposure = render::temporal::ExposureMode::kAutomatic
		};
		return _runtime.ConfigureFSRG(a_enabled, request)
			? Success()
			: Failure("FSR-G enablement change failed.");
	}

	ProviderResult StreamlinePresentation::Quiesce()
	{
		_enabled = false;
		if (!_runtime.ClearCurrentFrameGenerationTags()) {
			return Failure(
				std::string(Name()) +
				" could not invalidate its current input tags.");
		}
		if (_method == Method::kDLSSG) {
			if (!_runtime.HasDLSSGResources()) {
				return Success();
			}
			if (!_runtime.ConfigureDLSSG(false, _width, _height, _width, _height,
					_bufferCount, true)) {
				return Failure("DLSS-G could not be disabled.");
			}
		} else {
			render::temporal::FrameGenerationRequest request;
			request.renderWidth = _width;
			request.renderHeight = _height;
			request.outputWidth = _width;
			request.outputHeight = _height;
			request.color = {
				.resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
				.viewFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
				.range = render::temporal::ColorRange::kFull,
				.transfer = render::temporal::TransferFunction::kGamma22,
				.primaries =
					render::temporal::ColorPrimaries::kUnspecified,
				.stage = render::temporal::ColorStage::kPostTonemapLut,
				.alpha = render::temporal::AlphaMode::kIgnored,
				.exposure =
					render::temporal::ExposureMode::kAutomatic
			};
			if (!_runtime.ConfigureFSRG(false, request)) {
				return Failure("FSR-G could not be disabled.");
			}
		}

		return Success();
	}

	render::temporal::PresentInputRetirementMode
	StreamlinePresentation::GetPresentInputRetirementMode() const noexcept
	{
		return render::temporal::PresentInputRetirementMode::
			kVendorCompletionFence;
	}

	ProviderResult
	StreamlinePresentation::CollectPresentStatus(UINT a_presentFlags,
		HRESULT a_presentResult)
	{
		if (!render::temporal::ShouldObservePresentStatus(a_presentFlags,
				a_presentResult)) {
			return Success();
		}
		if (_method == Method::kDLSSG
				? _runtime.PollDLSSGState()
				: _runtime.PollFSRGState()) {
			return Success();
		}
		return Failure(
			std::string(Name()) + " reported a post-Present failure.");
	}

	std::optional<std::uint32_t>
	StreamlinePresentation::ConsumeGeneratedFrameCount() noexcept
	{
		return std::nullopt;
	}

	std::optional<std::uint32_t>
	StreamlinePresentation::ConsumePresentedFrameCount() noexcept
	{
		return _method == Method::kDLSSG
			? std::optional<std::uint32_t>{
				  _runtime.ConsumeDLSSGPresentedFrameCount()
			  }
			: std::nullopt;
	}

	std::optional<render::temporal::GpuCompletionDependency>
	StreamlinePresentation::ConsumePresentInputCompletionDependency() noexcept
	{
		return _method == Method::kDLSSG
			? _runtime.ConsumeDLSSGInputCompletionDependency()
			: _runtime.ConsumeFSRGInputCompletionDependency();
	}

	ProviderResult StreamlinePresentation::Sleep(std::uint32_t a_frame)
	{
		return _method == Method::kFSRG || _runtime.Sleep(a_frame)
			? Success()
			: Failure("Reflex sleep failed.");
	}

	ProviderResult StreamlinePresentation::SetLatencyMarker(
		render::temporal::LatencyMarker a_marker, std::uint32_t a_frame)
	{
		if (_method == Method::kFSRG) {
			return Success();
		}
		sl::PCLMarker marker = sl::PCLMarker::eSimulationStart;
		switch (a_marker) {
		case render::temporal::LatencyMarker::kInputSample:
			marker = sl::PCLMarker::eControllerInputSample;
			break;
		case render::temporal::LatencyMarker::kSimulationStart:
			marker = sl::PCLMarker::eSimulationStart;
			break;
		case render::temporal::LatencyMarker::kSimulationEnd:
			marker = sl::PCLMarker::eSimulationEnd;
			break;
		case render::temporal::LatencyMarker::kRenderSubmitStart:
			marker = sl::PCLMarker::eRenderSubmitStart;
			break;
		case render::temporal::LatencyMarker::kRenderSubmitEnd:
			marker = sl::PCLMarker::eRenderSubmitEnd;
			break;
		case render::temporal::LatencyMarker::kPresentStart:
			marker = sl::PCLMarker::ePresentStart;
			break;
		case render::temporal::LatencyMarker::kPresentEnd:
			marker = sl::PCLMarker::ePresentEnd;
			break;
		}
		return _runtime.SetLatencyMarker(marker, a_frame) ? Success() : Failure("PCL marker failed.");
	}

	ProviderResult StreamlinePresentation::ReleaseDisplayResources() noexcept
	{
		const auto destroy = _method == Method::kDLSSG
			? _runtime.DestroyDLSSGResources()
			: _runtime.DestroyFSRGResources();
		if (!destroy.Succeeded()) {
			return destroy;
		}
		_ready = false;
		_enabled = false;
		return Success();
	}

	ProviderResult StreamlinePresentation::DestroyAfterDrain() noexcept
	{
		const auto destroy = _method == Method::kDLSSG
			? _runtime.DestroyDLSSGResources()
			: _runtime.DestroyFSRGResources();
		if (!destroy.Succeeded()) {
			return destroy;
		}
		_queue = nullptr;
		_ready = false;
		_enabled = false;
		return Success();
	}

	bool StreamlinePresentation::IsAvailable() const noexcept
	{
		return _method == Method::kDLSSG
			? _runtime.featureDLSSG && _runtime.featurePCL &&
				  _runtime.featureReflex &&
				  _runtime.slSetFeatureLoaded &&
				  _runtime.slDLSSGSetOptions &&
				  _runtime.slDLSSGGetState
			: _runtime.featureFSRG && _runtime.slFSRGSetOptions &&
				  _runtime.slFSRGGetState && _runtime.slFSRGQuiesce &&
				  _runtime.slSetFeatureLoaded &&
				  _runtime.slSetTagForFrame &&
				  _runtime.slSetConstants &&
				  _runtime.slEvaluateFeature;
	}

	bool StreamlinePresentation::IsReady() const noexcept
	{
		return _presentationActive && _ready && IsAvailable();
	}

}  // namespace cs::features
