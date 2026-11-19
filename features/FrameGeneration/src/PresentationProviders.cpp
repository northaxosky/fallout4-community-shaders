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

		ProviderResult Failure(std::string a_message)
		{
			return ProviderResult::Fail(
				std::move(a_message),
				render::temporal::FailureDomain::kFrameGeneration);
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
				_method == Method::kDLSSG ? "DLSS-G, Reflex, or PCL is unavailable." : "Native FSR-G or its completion-fence API is unavailable.");
		}
		winrt::com_ptr<IDXGIFactory4> factory;
		factory.copy_from(a_context.factory);
		IDXGIFactory4* upgraded = factory.detach();
		const bool prepared = _runtime.PrepareDXGIFactory(&upgraded);
		factory.attach(upgraded);
		if (!prepared || !factory) {
			return Failure("Streamline could not upgrade the DXGI factory.");
		}
		if (auto active = SetPresentationActive(true); !active.Succeeded()) {
			return active;
		}
		winrt::com_ptr<IDXGISwapChain1> swapChain;
		const HRESULT result = factory->CreateSwapChainForHwnd(
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
		return ProviderResult::Ok();
	}

	ProviderResult StreamlinePresentation::ReleasePresentation(
		winrt::com_ptr<IDXGISwapChain4>& a_swapChain) noexcept
	{
		// The chain's destruction must still reach the registered hooks.
		a_swapChain = nullptr;
		_presentQueue = nullptr;
		return _presentationActive ? SetPresentationActive(false) : ProviderResult::Ok();
	}

	ProviderResult
	StreamlinePresentation::SetPresentationActive(bool a_active)
	{
		const auto result = _method == Method::kDLSSG ? _runtime.SetDLSSGPresentationActive(a_active) : _runtime.SetFSRGPresentationActive(a_active);
		if (result.Succeeded()) {
			_presentationActive = a_active;
		}
		return result;
	}

	ProviderResult StreamlinePresentation::CreateDisplayResources(
		std::uint32_t a_width, std::uint32_t a_height,
		const render::temporal::ColorContract& a_color, std::uint32_t a_bufferCount)
	{
		_color = a_color;
		_width = a_width;
		_height = a_height;
		_bufferCount = a_bufferCount;
		if (_method == Method::kDLSSG) {
			_ready = _runtime.ConfigureDLSSG(
				false, _configuration, a_width, a_height,
				a_width, a_height, a_bufferCount, _color, true);
		} else {
			render::temporal::FrameGenerationRequest request;
			request.renderWidth = a_width;
			request.renderHeight = a_height;
			request.outputWidth = a_width;
			request.outputHeight = a_height;
			request.color = _color;
			request.peakLuminanceNits = _peakLuminanceNits;
			const auto algorithm = _method == Method::kFSR4 ? sl::FSRGAlgorithm::eFSR4 : sl::FSRGAlgorithm::eFSR3;
			_ready =
				_runtime.ValidateFSRGAlgorithm(algorithm).Succeeded() &&
				_runtime.ConfigureFSRG(false, request, algorithm) &&
				_runtime.CheckFSRGCompletionCapability(algorithm);
		}
		return _ready ? ProviderResult::Ok() : Failure(std::string(Name()) + " display-resource preflight failed.");
	}

	ProviderResult StreamlinePresentation::PrepareFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		if (!_ready) {
			return Failure(
				std::string(Name()) + " presentation is not ready.");
		}
		_peakLuminanceNits = a_request.peakLuminanceNits;
		_color = a_request.color;
		const bool configured = _method == Method::kDLSSG ? (_configuration = a_request.configuration,
																_runtime.ConfigureDLSSG(a_request.enabled,
																	_configuration, a_request.renderWidth,
																	a_request.renderHeight, a_request.outputWidth,
																	a_request.outputHeight, _bufferCount, _color, true)) :
		                                                    _runtime.ConfigureFSRG(a_request.enabled, a_request,
																_method == Method::kFSR4 ? sl::FSRGAlgorithm::eFSR4 : sl::FSRGAlgorithm::eFSR3);
		if (!configured) {
			return Failure(std::string(Name()) + " options were rejected.");
		}
		_enabled = a_request.enabled;
		if (!a_request.enabled) {
			return _runtime.ClearFrameGenerationTags(
					   static_cast<std::uint32_t>(a_request.realFrame),
					   a_request.recording.commandList) ?
			           ProviderResult::Ok() :
			           Failure(std::string(Name()) +
							   " invalid input tags could not be cleared.");
		}
		const bool tagged = _method == Method::kDLSSG ? _runtime.TagDLSSGFrame(a_request) : _runtime.TagFSRGFrame(a_request);
		return tagged ? ProviderResult::Ok() : Failure(std::string(Name()) + " input preparation failed.");
	}

	ProviderResult StreamlinePresentation::Pause()
	{
		_enabled = false;
		if (!_runtime.ClearCurrentFrameGenerationTags()) {
			return Failure(
				std::string(Name()) +
				" could not invalidate its current input tags.");
		}
		if (_method == Method::kDLSSG) {
			if (!_runtime.HasDLSSGResources()) {
				return ProviderResult::Ok();
			}
			if (!_runtime.ConfigureDLSSG(false, _configuration,
					_width, _height, _width, _height, _bufferCount, _color, true)) {
				return Failure("DLSS-G could not be disabled.");
			}
		} else {
			render::temporal::FrameGenerationRequest request;
			request.renderWidth = _width;
			request.renderHeight = _height;
			request.outputWidth = _width;
			request.outputHeight = _height;
			request.color = _color;
			request.peakLuminanceNits = _peakLuminanceNits;
			if (!_runtime.ConfigureFSRG(false, request,
					_method == Method::kFSR4 ? sl::FSRGAlgorithm::eFSR4 : sl::FSRGAlgorithm::eFSR3)) {
				return Failure(std::string(Name()) +
							   " could not be disabled.");
			}
		}

		return ProviderResult::Ok();
	}

	render::temporal::PresentStatus StreamlinePresentation::CollectPresentStatus(
		UINT a_syncInterval, UINT a_presentFlags, HRESULT a_presentResult)
	{
		if (!render::temporal::ShouldObservePresentStatus(a_presentFlags,
				a_presentResult)) {
			return {};
		}
		if (_method == Method::kDLSSG) {
			_runtime.ObserveDLSSGPresent(a_syncInterval);
		}
		if (!_enabled) {
			return {};
		}
		render::temporal::PresentStatus status;
		if (_method == Method::kDLSSG) {
			status.generatedFrames = _runtime.ConsumeDLSSGGeneratedFrameCount();
			status.presentedFrames = _runtime.ConsumeDLSSGPresentedFrameCount();
			if (_presentationActive && _presentQueue) {
				// D3D12 DLSS-G orders its readers on the queue used to create presentation.
				status.inputCompletion.emplace();
				status.inputCompletion->orderedQueue = _presentQueue;
			}
		} else {
			status.inputCompletion = _runtime.ConsumeFSRGInputCompletionDependency();
		}
		const bool succeeded = _method == Method::kDLSSG ? _runtime.LastDLSSGStateSucceeded() : _runtime.PollFSRGState(_method == Method::kFSR4 ? sl::FSRGAlgorithm::eFSR4 : sl::FSRGAlgorithm::eFSR3);
		if (!succeeded) {
			status.result = Failure(
				std::string(Name()) + " reported a post-Present failure.");
		}
		return status;
	}

	ProviderResult StreamlinePresentation::DestroyAfterDrain() noexcept
	{
		const auto destroy = _method == Method::kDLSSG ? _runtime.DestroyDLSSGResources() : _runtime.DestroyFSRGResources();
		if (!destroy.Succeeded()) {
			return destroy;
		}
		_ready = false;
		_enabled = false;
		return ProviderResult::Ok();
	}

	ProviderResult StreamlinePresentation::ValidateConfiguration(
		const render::temporal::FrameGenerationConfiguration&
			a_configuration) const
	{
		return _method == Method::kDLSSG ? _runtime.ValidateDLSSGConfiguration(
											   a_configuration) :
		       _method == Method::kFSR4  ? _runtime.ValidateFSRGAlgorithm(
											   sl::FSRGAlgorithm::eFSR4) :
		                                   ProviderResult::Ok();
	}

	bool StreamlinePresentation::PresentsSceneOnlyBackBuffer() const noexcept
	{
		return _method != Method::kDLSSG;
	}

	bool StreamlinePresentation::IsAvailable() const noexcept
	{
		return _method == Method::kDLSSG ? _runtime.featureDLSSG && _runtime.featurePCL &&
		                                       _runtime.featureReflex &&
		                                       _runtime.slSetFeatureLoaded &&
		                                       _runtime.slDLSSGSetOptions &&
		                                       _runtime.slDLSSGGetState :
		                                   _runtime.featureFSRG && _runtime.slFSRGSetOptions &&
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
