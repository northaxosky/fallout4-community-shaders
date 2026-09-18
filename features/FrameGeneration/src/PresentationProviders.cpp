#include "PresentationProviders.h"

#include <utility>

#include <winrt/base.h>

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

	StreamlinePresentation::StreamlinePresentation(
		Streamline& a_runtime, Method a_method) noexcept
		: _runtime(a_runtime), _method(a_method) {}

	const char* StreamlinePresentation::Name() const noexcept
	{
		switch (_method) {
		case Method::kDLSSG:
			return "DLSS-G";
		case Method::kFSRG:
			return "FSR 3 Frame Generation";
		case Method::kFSR4:
			return "FSR 4 ML Frame Generation";
		}
		return "Unknown Frame Generation";
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
			!a_context.window || !a_context.description || !a_swapChain) {
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
		_presentQueue.copy_from(a_context.queue);
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
			_ready = _runtime.ConfigureDLSSG(
				false, _configuration, a_width, a_height,
				a_width, a_height, a_bufferCount, true);
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
			const auto algorithm = _method == Method::kFSR4
				? sl::FSRGAlgorithm::eFSR4
				: sl::FSRGAlgorithm::eFSR3;
			_ready =
				_runtime.ValidateFSRGAlgorithm(algorithm).Succeeded() &&
				_runtime.ConfigureFSRG(false, request, algorithm) &&
				_runtime.CheckFSRGCompletionCapability(algorithm);
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
			? (_configuration = a_request.configuration,
				  _runtime.ConfigureDLSSG(a_request.enabled,
					  _configuration, a_request.renderWidth,
					  a_request.renderHeight, a_request.outputWidth,
					  a_request.outputHeight, _bufferCount, true))
			: _runtime.ConfigureFSRG(a_request.enabled, a_request,
				  _method == Method::kFSR4
					  ? sl::FSRGAlgorithm::eFSR4
					  : sl::FSRGAlgorithm::eFSR3);
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
			_configuration = a_request.configuration;
			disabled = _runtime.ConfigureDLSSG(false, _configuration,
				_width, _height, _width, _height, _bufferCount, true);
		} else {
			auto request = a_request;
			request.enabled = false;
			disabled = _runtime.ConfigureFSRG(false, request,
				_method == Method::kFSR4
					? sl::FSRGAlgorithm::eFSR4
					: sl::FSRGAlgorithm::eFSR3);
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
				(_method != Method::kDLSSG && !_ready))) {
			_enabled = false;
			return Success();
		}
		_enabled = a_enabled;
		if (!a_enabled && !_runtime.ClearCurrentFrameGenerationTags()) {
			return Failure(std::string(Name()) +
				" invalid input tags could not be cleared.");
		}
		if (_method == Method::kDLSSG) {
			return _runtime.ConfigureDLSSG(a_enabled, _configuration,
					   _width, _height, _width, _height, _bufferCount, true) ?
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
		return _runtime.ConfigureFSRG(a_enabled, request,
				   _method == Method::kFSR4
					   ? sl::FSRGAlgorithm::eFSR4
					   : sl::FSRGAlgorithm::eFSR3)
			? Success()
			: Failure(std::string(Name()) +
				  " enablement change failed.");
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
			if (!_runtime.ConfigureDLSSG(false, _configuration,
					_width, _height, _width, _height, _bufferCount, true)) {
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
			if (!_runtime.ConfigureFSRG(false, request,
					_method == Method::kFSR4
						? sl::FSRGAlgorithm::eFSR4
						: sl::FSRGAlgorithm::eFSR3)) {
				return Failure(std::string(Name()) +
					" could not be disabled.");
			}
		}

		return Success();
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
				? _runtime.LastDLSSGStateSucceeded()
				: _runtime.PollFSRGState(
					  _method == Method::kFSR4
						  ? sl::FSRGAlgorithm::eFSR4
						  : sl::FSRGAlgorithm::eFSR3)) {
			return Success();
		}
		return Failure(
			std::string(Name()) + " reported a post-Present failure.");
	}

	ProviderResult StreamlinePresentation::ValidateConfiguration(
		const render::temporal::FrameGenerationConfiguration&
			a_configuration) const
	{
		return _method == Method::kDLSSG
			? _runtime.ValidateDLSSGConfiguration(
				  a_configuration)
			: _method == Method::kFSR4
				? _runtime.ValidateFSRGAlgorithm(
					  sl::FSRGAlgorithm::eFSR4)
				: Success();
	}

	render::temporal::FrameGenerationCapabilities
	StreamlinePresentation::GetCapabilities() const noexcept
	{
		return _method == Method::kDLSSG
			? _runtime.GetDLSSGCapabilities()
			: render::temporal::IFrameGenerationProvider::
				  GetCapabilities();
	}

	std::optional<std::uint32_t>
	StreamlinePresentation::ConsumeGeneratedFrameCount() noexcept
	{
		return _method == Method::kDLSSG
			? std::optional<std::uint32_t>{
				  _runtime.ConsumeDLSSGGeneratedFrameCount()
			  }
			: std::nullopt;
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
		if (_method == Method::kDLSSG) {
			if (!_presentationActive || !_presentQueue) {
				return std::nullopt;
			}
			// D3D12 DLSS-G orders its readers on the queue used to create presentation.
			render::temporal::GpuCompletionDependency dependency;
			dependency.orderedQueue = _presentQueue;
			return dependency;
		}
		return _runtime.ConsumeFSRGInputCompletionDependency();
	}

	ProviderResult StreamlinePresentation::Sleep(std::uint32_t a_frame)
	{
		return _method != Method::kDLSSG || _runtime.Sleep(a_frame)
			? Success()
			: Failure("Reflex sleep failed.");
	}

	ProviderResult StreamlinePresentation::SetLatencyMarker(
		render::temporal::LatencyMarker a_marker, std::uint32_t a_frame)
	{
		if (_method != Method::kDLSSG) {
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
		_ready = false;
		_enabled = false;
		_presentQueue = nullptr;
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
				  (_method != Method::kFSR4 ||
					  _runtime.ValidateFSRGAlgorithm(
						  sl::FSRGAlgorithm::eFSR4)
						  .Succeeded()) &&
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
