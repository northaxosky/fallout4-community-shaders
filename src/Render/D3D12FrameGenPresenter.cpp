#include "D3D12FrameGenPresenter.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <optional>
#include <utility>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Annotation.h"
#include "Render/DXGISwapChainFacadeContract.h"
#include "Render/TemporalPipeline.h"
#include "Render/TemporalRenderer.h"

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.d3d12presenter");

		D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* a_resource,
			D3D12_RESOURCE_STATES a_before,
			D3D12_RESOURCE_STATES a_after)
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = a_resource;
			barrier.Transition.StateBefore = a_before;
			barrier.Transition.StateAfter = a_after;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			return barrier;
		}

		constexpr UINT kInheritedFlags =
			DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		constexpr UINT kBufferCount = 2;
	}  // namespace

	D3D12FrameGenPresenter::D3D12FrameGenPresenter(InteropBridge& a_bridge,
		temporal::IFrameGenerationProvider& a_provider,
		const TemporalPresentationCallbacks& a_callbacks, bool a_hdrContainer) noexcept :
		_bridge(a_bridge),
		_provider(a_provider), _callbacks(a_callbacks), _hdrContainer(a_hdrContainer)
	{}

	D3D12FrameGenPresenter::~D3D12FrameGenPresenter()
	{
		if (FAILED(Release()) && _bridge.IsQuarantined()) {
			// A failed drain means the GPU or SDK may still own these objects.
			for (const auto& backBuffer : _backBuffers) {
				InteropBridge::Retain(backBuffer.get());
			}
			for (const auto& submission : _presentSubmissions) {
				InteropBridge::Retain(submission.allocator.get());
				InteropBridge::Retain(submission.commandList.get());
			}
			InteropBridge::Retain(_chain.chain.get());
			for (const auto* textures : { &_buffers.hudless, &_buffers.ui, &_buffers.depth, &_buffers.motion }) {
				for (const auto& texture : *textures) {
					InteropBridge::Retain(texture);
				}
			}
		}
	}

	temporal::ProviderResult D3D12FrameGenPresenter::Create(InteropBridge& a_bridge,
		temporal::IFrameGenerationProvider& a_provider,
		const TemporalPresentationCallbacks& a_callbacks, const ChainRequest& a_request,
		bool a_hdrContainer, std::unique_ptr<D3D12FrameGenPresenter>& a_presenter) noexcept
	{
		using temporal::FailureDomain;
		using temporal::ProviderResult;
		if (!a_bridge.IsReady()) {
			return ProviderResult::Fail("The interop bridge is unavailable.", FailureDomain::kTransport);
		}
		std::unique_ptr<D3D12FrameGenPresenter> presenter{
			new D3D12FrameGenPresenter(a_bridge, a_provider, a_callbacks, a_hdrContainer)
		};
		ProviderResult failure{ .code = temporal::ProviderResultCode::kSuccess };
		try {
			HRESULT result = presenter->CreateChain(a_request, failure);
			if (SUCCEEDED(result)) {
				result = presenter->RefreshBackBuffers();
			}
			if (SUCCEEDED(result)) {
				a_bridge.CreateSubmissions(presenter->_presentSubmissions, "Present");
				FrameGenerationBuffers buffers;
				result = presenter->CreateBuffers(presenter->_chain.desc.Width, presenter->_chain.desc.Height, buffers);
				if (SUCCEEDED(result)) {
					result = presenter->CommitBuffers(std::move(buffers), presenter->_chain.desc.Width, presenter->_chain.desc.Height);
				}
				if (result == S_FALSE) {
					failure = ProviderResult::Fail("The frame-generation display resources are unavailable.", FailureDomain::kFrameGeneration);
					result = E_FAIL;
				}
			}
			if (FAILED(result)) {
				if (failure.Succeeded()) {
					failure = { .code = temporal::ProviderResultCode::kFailure,
						.hresult = result,
						.message = "The frame-generation presenter could not be created.",
						.failureDomain = FailureDomain::kPresentation };
				}
				return failure;
			}
		} catch (const std::exception& e) {
			return ProviderResult::Fail(e.what(), FailureDomain::kPresentation);
		} catch (...) {
			return ProviderResult::Fail("The frame-generation presenter raised a non-standard exception.", FailureDomain::kPresentation);
		}
		a_presenter = std::move(presenter);
		return ProviderResult::Ok();
	}

	HRESULT D3D12FrameGenPresenter::CreateChain(const ChainRequest& a_request, temporal::ProviderResult& a_failure)
	{
		winrt::com_ptr<IDXGIFactory4> factory;
		DX::ThrowIfFailed(a_request.adapter->GetParent(IID_PPV_ARGS(factory.put())));

		auto& desc = _chain.desc;
		desc = {};
		desc.Width = a_request.width;
		desc.Height = a_request.height;
		desc.Format = a_request.format;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = kBufferCount;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		desc.Flags = a_request.flags & kInheritedFlags;

		const auto created = _provider.CreatePresentation(
			{ .adapter = a_request.adapter,
				.device = _bridge.GetD3D12Device(),
				.queue = _bridge.GetCommandQueue(),
				.factory = factory.get(),
				.window = a_request.window,
				.description = &desc },
			_chain.chain.put());
		if (!created.Succeeded() || !_chain.chain) {
			a_failure = created;
			if (created.Succeeded()) {
				return E_FAIL;
			}
			return FAILED(created.hresult) ? created.hresult : created.sdkResult ? static_cast<HRESULT>(created.sdkResult) :
			                                                                       E_FAIL;
		}
		_frameIndex = _chain.chain->GetCurrentBackBufferIndex();
		if (_hdrContainer && _callbacks.queryHdrOutput) {
			ApplyHdrColorSpace(_callbacks.queryHdrOutput());
		}
		return S_OK;
	}

	void D3D12FrameGenPresenter::RecordTestPresent(UINT a_flags, HRESULT a_result) noexcept
	{
		if (_preparedTransaction) {
			render::TemporalPipeline::Get().RecordPresentAttempt(_frameSlot, a_flags, a_result);
		}
	}

	bool D3D12FrameGenPresenter::IsReady() const noexcept
	{
		return !_faulted && _bridge.IsReady() && _chain.chain && _backBuffers[0] && _backBuffers[1] &&
		       _presentSubmissions[0].allocator && _presentSubmissions[1].allocator &&
		       _presentSubmissions[0].commandList && _presentSubmissions[1].commandList;
	}

	bool D3D12FrameGenPresenter::IsFrameGenerationReady() const noexcept
	{
		return IsReady() && !_failures.Latched() && _provider.IsReady() &&
		       _buffers.hudless[0] && _buffers.hudless[1] &&
		       (!_hdrContainer || (_buffers.ui[0] && _buffers.ui[1])) &&
		       _buffers.depth[0] && _buffers.depth[1] && _buffers.motion[0] &&
		       _buffers.motion[1];
	}

	HRESULT D3D12FrameGenPresenter::RefreshBackBuffers() noexcept
	{
		winrt::com_ptr<ID3D12Resource> refreshed[2];
		for (UINT index = 0; index < 2; ++index) {
			const HRESULT result =
				_chain.chain->GetBuffer(index, IID_PPV_ARGS(refreshed[index].put()));
			if (FAILED(result)) {
				return result;
			}
		}
		for (UINT index = 0; index < 2; ++index) {
			_backBuffers[index] = std::move(refreshed[index]);
		}
		_frameIndex = _chain.chain->GetCurrentBackBufferIndex();
		return S_OK;
	}

	D3D12FrameGenPresenter::SlotTextures D3D12FrameGenPresenter::CreateSlotBuffers(
		const D3D11_TEXTURE2D_DESC& a_desc, std::string_view a_name) const
	{
		SlotTextures buffers;
		for (UINT slot = 0; slot < buffers.size(); ++slot) {
			buffers[slot] = SharedD3D11D3D12Texture::Create(
				_bridge.GetD3D11Device(), _bridge.GetD3D12Device(), a_desc, std::format("{}[{}]", a_name, slot));
		}
		return buffers;
	}

	HRESULT D3D12FrameGenPresenter::CreateBuffers(
		UINT a_width, UINT a_height, FrameGenerationBuffers& a_buffers) const
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS |
		                 D3D11_BIND_RENDER_TARGET;

		FrameGenerationBuffers buffers;
		desc.Format = _hdrContainer ? _chain.desc.Format : DXGI_FORMAT_R8G8B8A8_UNORM;
		buffers.hudless = CreateSlotBuffers(desc, "Upscaling/HUDLess");
		if (_hdrContainer) {
			auto uiDesc = desc;
			uiDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			uiDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			buffers.ui = CreateSlotBuffers(uiDesc, "Upscaling/UI");
		}
		for (UINT slot = 0; slot < 2; ++slot) {
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			buffers.depth[slot] = SharedD3D11D3D12Texture::Create(
				_bridge.GetD3D11Device(), _bridge.GetD3D12Device(), desc,
				std::format("Upscaling/FrameGenerationDepth[{}]", slot));
			desc.Format = DXGI_FORMAT_R16G16_FLOAT;
			buffers.motion[slot] = SharedD3D11D3D12Texture::Create(
				_bridge.GetD3D11Device(), _bridge.GetD3D12Device(), desc,
				std::format("Upscaling/FrameGenerationMotion[{}]", slot));
		}
		const bool complete = buffers.hudless[0] && buffers.hudless[1] &&
		                      (!_hdrContainer || (buffers.ui[0] && buffers.ui[1])) &&
		                      buffers.depth[0] && buffers.depth[1] && buffers.motion[0] && buffers.motion[1];
		if (!complete) {
			return E_OUTOFMEMORY;
		}
		a_buffers = std::move(buffers);
		return S_OK;
	}

	HRESULT D3D12FrameGenPresenter::CommitBuffers(
		FrameGenerationBuffers&& a_buffers, UINT a_width, UINT a_height)
	{
		_failures.Retry();
		_buffers = std::move(a_buffers);
		++_inputResourceGeneration;
		const auto providerResult = _provider.CreateDisplayResources(
			a_width, a_height, PresentContract(), _chain.desc.BufferCount);
		if (!providerResult.Succeeded()) {
			_failures.Latch();
			L->error(
				"Frame-generation provider resources were not recreated; "
				"real-frame presentation remains active");
			return S_FALSE;
		}
		return S_OK;
	}

	temporal::ColorContract D3D12FrameGenPresenter::PresentContract() const
	{
		if (!_hdrContainer) {
			return temporal::kFo4PostTonemapSdrContract;
		}
		const bool hdr = _callbacks.queryHdrOutput && _callbacks.queryHdrOutput().enabled;
		return temporal::ContainerContract(hdr);
	}

	HRESULT D3D12FrameGenPresenter::RestoreProvider(UINT a_width, UINT a_height)
	{
		const auto result = _provider.CreateDisplayResources(
			a_width, a_height, PresentContract(), _chain.desc.BufferCount);
		if (!result.Succeeded()) {
			_failures.Latch();
			FailFrame(result.message.empty() ? "Frame-generation provider restoration failed" : result.message.c_str());
			return S_FALSE;
		}
		_failures.Retry();
		return S_OK;
	}

	void D3D12FrameGenPresenter::ClearInputs(ID3D11DeviceContext& a_context) noexcept
	{
		const float clear[4]{};
		cs::render::annotation::ScopedEvent annotationScope(
			"FrameGeneration::ClearSharedBuffers");
		for (const auto* textures : { &_buffers.hudless, &_buffers.depth, &_buffers.motion }) {
			for (const auto& texture : *textures) {
				if (texture && texture->rtv11) {
					a_context.ClearRenderTargetView(texture->rtv11.get(), clear);
				}
			}
		}
	}

	void D3D12FrameGenPresenter::ResetSubmissions() noexcept
	{
		temporal::ResetPresentationProtocol(
			_allocatorFenceValues, _inputReuseGate, _frameSlot, _presentPrepared,
			_preparedFrameGeneration, _vendorConsumptionPossible,
			_preparedTransaction);
		_presentSubmissionMayBeInFlight = false;
		for (auto& submission : _presentSubmissions) {
			submission.completionValue = 0;
			submission.timing.Discard();
		}
		_nextPresentSubmission = 0;
	}

	temporal::ProviderResult D3D12FrameGenPresenter::ReleaseResources() noexcept
	{
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		_providerGenerationEnabled = false;
		_buffers = {};
		for (auto& backBuffer : _backBuffers) {
			backBuffer = nullptr;
		}
		++_inputResourceGeneration;
		ResetSubmissions();
		// The provider's hooks must see the chain die.
		return _provider.ReleasePresentation(_chain.chain);
	}

	temporal::ProviderResult D3D12FrameGenPresenter::DrainAndDestroy(
		std::atomic_uint64_t& a_drains) noexcept
	{
		if (const auto paused = _provider.Pause(); !paused.Succeeded()) {
			// Destruction follows, so a failed pause is not fatal.
			L->warn("{}", temporal::FormatProviderFailure("Pause before release", paused));
		}
		_retirementDiagnostics.globalDrainAttempts.fetch_add(1, std::memory_order_relaxed);
		if (FAILED(_bridge.Drain())) {
			_retirementDiagnostics.globalDrainFailures.fetch_add(1, std::memory_order_relaxed);
			_retirementDiagnostics.violations.fetch_add(1, std::memory_order_relaxed);
			return temporal::ProviderResult::Fail(
				"GPU work did not drain before provider resource release.",
				temporal::FailureDomain::kTransport);
		}
		a_drains.fetch_add(1, std::memory_order_relaxed);
		return _provider.DestroyAfterDrain();
	}

	HRESULT D3D12FrameGenPresenter::Release() noexcept
	{
		if (_bridge.IsQuarantined()) {
			return DXGI_ERROR_DEVICE_REMOVED;
		}
		// Without a chain there is nothing of the provider's to drain.
		auto result = _chain.chain ? DrainAndDestroy(_retirementDiagnostics.teardownDrains) : temporal::ProviderResult::Ok();
		if (result.Succeeded()) {
			result = ReleaseResources();
		}
		if (!result.Succeeded()) {
			_bridge.MarkQuarantined();
			L->critical(
				"Temporal D3D12 resources quarantined after failed provider "
				"drain: {}",
				result.message.empty() ? "frame-generation display resources could not be released" : result.message);
			return E_FAIL;
		}
		for (auto& submission : _presentSubmissions) {
			submission = {};
		}
		return S_OK;
	}

	void D3D12FrameGenPresenter::FailFrame(const char* a_reason) noexcept
	{
		const char* reason = a_reason ? a_reason : "unknown failure";
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		render::TemporalPipeline::Get().RequestFrameGenerationReset();
		const bool latched = _failures.Failed();
		if (!_bridge.IsQuarantined() && _callbacks.recordFailure) {
			_callbacks.recordFailure(reason);
		}
		try {
			L->error("Frame generation skipped a frame{}: {}",
				latched ? " and latched off until a settings change or resize" : "", reason);
		} catch (...) {
		}
	}

	void D3D12FrameGenPresenter::FailFrame(
		std::string_view a_operation, const temporal::ProviderResult& a_result) noexcept
	{
		const auto reason =
			temporal::FormatProviderFailure(a_operation, a_result);
		FailFrame(reason.c_str());
	}

	bool D3D12FrameGenPresenter::AcquireFrameGenerationInputWrite() noexcept
	{
		if (_failures.Latched() || !_bridge.GetRetirementFence12() || !_bridge.GetRetirementFence11() ||
			!_bridge.GetRetirementQueue()) {
			return false;
		}
		auto* context = _bridge.GetD3D11Context();
		const auto completed = _bridge.GetRetirementFence12()->GetCompletedValue();
		const bool detailed =
			render::TemporalPipeline::Get().DetailedTracingEnabled();
		std::uint64_t waitMicroseconds = 0;
		const auto acquired = _inputReuseGate.Acquire(
			_frameSlot, _inputResourceGeneration,
			static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(_bridge.GetRetirementQueue())),
			completed,
			[&](const temporal::PresentInputRetirementToken& a_token) {
				if (!detailed) {
					return context->Wait(_bridge.GetRetirementFence11(), a_token.value);
				}
				auto timing =
					render::TemporalPipeline::Get().MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kAcquirePresentInputs);
				const auto start = std::chrono::steady_clock::now();
				const HRESULT waitResult =
					context->Wait(_bridge.GetRetirementFence11(), a_token.value);
				waitMicroseconds = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - start)
						.count());
				return waitResult;
			});
		if (acquired.firstAcquire) {
			_retirementDiagnostics.acquisitions.fetch_add(1, std::memory_order_relaxed);
		}
		if (acquired.Succeeded() && acquired.firstAcquire) {
			if (acquired.waitRequired) {
				_retirementDiagnostics.gpuWaits.fetch_add(1, std::memory_order_relaxed);
			} else {
				_retirementDiagnostics.immediateAcquisitions.fetch_add(
					1, std::memory_order_relaxed);
			}
		}
		if (!acquired.Succeeded()) {
			_retirementDiagnostics.waitFailures.fetch_add(1, std::memory_order_relaxed);
			_retirementDiagnostics.violations.fetch_add(1, std::memory_order_relaxed);
		}
		if (temporal::ShouldPublishPresentInputAcquireTelemetry(acquired)) {
			_retirementDiagnostics.lastRealFrame.store(acquired.token.realFrame,
				std::memory_order_relaxed);
			_retirementDiagnostics.lastResourceGeneration.store(
				acquired.token.resourceGeneration, std::memory_order_relaxed);
			_retirementDiagnostics.lastRequiredFence.store(acquired.token.value,
				std::memory_order_relaxed);
			_retirementDiagnostics.lastCompletedFence.store(acquired.completedValue,
				std::memory_order_relaxed);
			_retirementDiagnostics.waitCpuMicroseconds.fetch_add(
				waitMicroseconds, std::memory_order_relaxed);
			_retirementDiagnostics.lastSlot.store(_frameSlot,
				std::memory_order_relaxed);
			_retirementDiagnostics.lastAcquireQueuedGpuWait.store(
				acquired.waitRequired, std::memory_order_relaxed);
		}
		if (!acquired.Succeeded()) {
			FailFrame(
				temporal::PresentInputAcquireFailureMessage(acquired.code));
		}
		return acquired.Succeeded();
	}

	bool D3D12FrameGenPresenter::ProveIdle(ID3D12GraphicsCommandList* a_list) noexcept
	{
		// The provider may still reference its inputs; only an idle GPU proves it does not.
		if (SUCCEEDED(_bridge.Drain())) {
			return true;
		}
		(void)a_list->Close();
		_vendorConsumptionPossible = false;
		_preparedTransaction = false;
		return false;
	}

	const SharedD3D11D3D12Texture* D3D12FrameGenPresenter::ComposeHdrFrameGenerationInputs(
		const PresentFrame& a_frame) noexcept
	{
		// No separate interface means no clean HUD-less frame; skip this frame only.
		if (!a_frame.separated) {
			return nullptr;
		}
		const auto* hudless = _buffers.hudless[_frameSlot].get();
		const auto* ui = _buffers.ui[_frameSlot].get();
		// The provider composites the interface itself over a scene-only back buffer.
		const bool sceneOnly = _provider.PresentsSceneOnlyBackBuffer();
		auto& compose = *a_frame.compose;
		auto& context = *a_frame.context;
		if (!compose.IsReady() || !a_frame.output || !hudless || !ui || !a_frame.proxy || !a_frame.scene ||
			!hudless->srv11 || !hudless->uav11 || !ui->srv11 || !ui->uav11 ||
			!a_frame.scene->srv11 || !a_frame.output->uav11) {
			CS_LOG_EVERY_MS(L, 5000, spdlog::level::err, "HDR frame-generation inputs are unavailable; the frame is not generated.");
			return nullptr;
		}
		const bool written = temporal::WriteFrameGenerationInput(
			[this] { return AcquireFrameGenerationInputWrite(); },
			[&] {
				cs::render::annotation::ScopedEvent annotationScope("FrameGeneration::EncodeHdrInputs");
				compose.Dispatch(context, *a_frame.scene->srv11, nullptr, *hudless->uav11,
					a_frame.width, a_frame.height, a_frame.hdr);
				context.CopyResource(ui->texture11.get(), a_frame.proxy->texture11.get());
				compose.EncodeInterface(context, *ui->uav11, a_frame.width, a_frame.height, a_frame.hdr);
				if (!sceneOnly) {
					compose.CompositeInterface(context, *hudless->srv11, *ui->srv11, *a_frame.output->uav11,
						a_frame.width, a_frame.height);
				}
			});
		if (!written) {
			FailFrame("frame-generation HDR inputs were still in provider use");
			return nullptr;
		}
		return sceneOnly ? hudless : a_frame.output;
	}

	HRESULT D3D12FrameGenPresenter::Present(const PresentFrame& a_frame, const PresentArgs& a_args) noexcept
	{
		try {
			const HRESULT result = PresentImpl(a_frame, a_args);
			if (result == DXGI_ERROR_DEVICE_REMOVED ||
				result == DXGI_ERROR_DEVICE_RESET ||
				result == DXGI_ERROR_DEVICE_HUNG) {
				_bridge.Quarantine(
					"The temporal D3D12 presentation device was lost.");
			}
			return result;
		} catch (const winrt::hresult_error& e) {
			const auto result = static_cast<HRESULT>(e.code());
			_bridge.Quarantine(
				"Temporal D3D12 presentation command submission failed.");
			FailFrame(
				winrt::to_string(e.message()).c_str());
			return result;
		} catch (const std::exception& e) {
			if (_presentSubmissionMayBeInFlight) {
				_bridge.Quarantine(
					"Temporal D3D12 presentation work could not be retired.");
			}
			FailFrame(e.what());
			return E_FAIL;
		} catch (...) {
			if (_presentSubmissionMayBeInFlight) {
				_bridge.Quarantine(
					"Temporal D3D12 presentation work could not be retired.");
			}
			FailFrame("unhandled presentation failure");
			return E_FAIL;
		}
	}

	HRESULT D3D12FrameGenPresenter::PresentImpl(const PresentFrame& a_frame, const PresentArgs& a_args)
	{
		auto& pipeline = render::TemporalPipeline::Get();
		auto* queue = _bridge.GetCommandQueue();
		auto& timer = _bridge.GetGpuTimer();

		if (!_presentPrepared) {
			const bool frameGenerationRequested = pipeline.Renderer().TakeFrameGenerationFrame();
			auto request = frameGenerationRequested &&
			                       _callbacks.queryFrameState ?
			                   _callbacks.queryFrameState() :
			                   temporal::FrameGenerationRequest{};
			if (frameGenerationRequested) {
				pipeline.Renderer().CaptureFrameGenerationFinalDebugSnapshot();
			}
			SubmissionRecord* submission = nullptr;
			{
				auto timing =
					pipeline.MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kAllocatorFenceWait);
				const HRESULT acquireResult = _bridge.AcquireSubmission(
					_presentSubmissions, _nextPresentSubmission, submission);
				if (FAILED(acquireResult)) {
					return acquireResult;
				}
			}
			bool requested = frameGenerationRequested &&
			                 !_failures.Latched() &&
			                 request.enabled;
			// An R8 proxy copied into the R10 chain would be reinterpreted bit for bit.
			const SharedD3D11D3D12Texture* copySource = a_frame.proxy;
			// Frame generation blends in the output encoding so real and generated match.
			const SharedD3D11D3D12Texture* hdrFrameGenerationBackBuffer = nullptr;
			if (_hdrContainer) {
				ApplyHdrColorSpace(a_frame.hdr);
				if (requested) {
					hdrFrameGenerationBackBuffer = ComposeHdrFrameGenerationInputs(a_frame);
					requested = hdrFrameGenerationBackBuffer != nullptr;
				}
				copySource = hdrFrameGenerationBackBuffer ? hdrFrameGenerationBackBuffer : a_frame.ComposeDisplay();
			}
			if (copySource) {
				D3D11_TEXTURE2D_DESC sourceDesc{};
				copySource->texture11->GetDesc(&sourceDesc);
				if (sourceDesc.Format != _chain.desc.Format) {
					CS_LOG_EVERY_MS(L, 5000, spdlog::level::err, "Present source format does not match the swap chain; the frame is not presented.");
					copySource = nullptr;
				}
			}
			cs::render::annotation::SetMarker("FG_D3D11ProductionComplete");
			// D3D11 time across the round trip; minus D3D12::Present is the handoff.
			cs::render::annotation::SpannedEvent handoff;
			handoff.Begin("Render::PresentHandoff");
			_bridge.QueueWaitsForD3D11();
			DX::ThrowIfFailed(submission->allocator->Reset());
			DX::ThrowIfFailed(submission->commandList->Reset(
				submission->allocator.get(), nullptr));

			auto* commandList = submission->commandList.get();
			timer.Begin(submission->timing);
			timer.BeginScope(commandList, submission->timing, "D3D12::Present");
			if (copySource) {
				auto timing =
					render::TemporalPipeline::Get().MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kCopyRecord);
				cs::render::annotation::ScopedEvent copyScope(commandList,
					"FG_CopyRealFrame");
				timer.BeginScope(commandList, submission->timing, "D3D12::Present/CopyRealFrame");
				const std::array barriersBefore{
					Transition(copySource->resource12.get(),
						D3D12_RESOURCE_STATE_COMMON,
						D3D12_RESOURCE_STATE_COPY_SOURCE),
					Transition(_backBuffers[_frameIndex].get(),
						D3D12_RESOURCE_STATE_PRESENT,
						D3D12_RESOURCE_STATE_COPY_DEST)
				};
				commandList->ResourceBarrier(static_cast<UINT>(barriersBefore.size()),
					barriersBefore.data());
				commandList->CopyResource(_backBuffers[_frameIndex].get(),
					copySource->resource12.get());
				const std::array barriersAfter{
					Transition(copySource->resource12.get(),
						D3D12_RESOURCE_STATE_COPY_SOURCE,
						D3D12_RESOURCE_STATE_COMMON),
					Transition(_backBuffers[_frameIndex].get(),
						D3D12_RESOURCE_STATE_COPY_DEST,
						D3D12_RESOURCE_STATE_PRESENT)
				};
				commandList->ResourceBarrier(static_cast<UINT>(barriersAfter.size()),
					barriersAfter.data());
				timer.EndScope(commandList, submission->timing, "D3D12::Present/CopyRealFrame");
			}

			if (requested) {
				const auto validation =
					_provider.ValidateConfiguration(
						request.configuration);
				if (validation.code ==
					temporal::ProviderResultCode::kSkipped) {
					requested = false;
					_vendorConsumptionPossible = false;
				} else if (!validation.Succeeded()) {
					requested = false;
					_vendorConsumptionPossible = false;
					FailFrame(
						"Frame-generation option validation",
						validation);
				}
			}
			_preparedRealFrame = request.realFrame;
			_vendorConsumptionPossible = requested;
			_preparedTransaction =
				requested && pipeline.PreparePresent(_frameSlot);
			if (!_preparedTransaction) {
				requested = false;
				_vendorConsumptionPossible = false;
			}
			pipeline.SetFrameGenerationInterrupted(!requested);
			bool frameGenerationPrepared = false;
			if (requested) {
				cs::render::annotation::ScopedEvent prepareScope(commandList,
					"FG_ConfigurePrepare");
				timer.BeginScope(commandList, submission->timing, "D3D12::Present/FrameGenerationPrepare");
				request.recording =
					temporal::D3D12RecordingContext{ .commandList = commandList,
						.queue = queue,
						.slot = _frameSlot };
				request.depth = temporal::D3D12GpuView{
					.resource = _buffers.depth[_frameSlot]->resource12.get(),
					.state = D3D12_RESOURCE_STATE_COMMON
				};
				request.motionVectors = temporal::D3D12GpuView{
					.resource = _buffers.motion[_frameSlot]->resource12.get(),
					.state = D3D12_RESOURCE_STATE_COMMON
				};
				request.hudlessColor = temporal::D3D12GpuView{
					.resource = _buffers.hudless[_frameSlot]->resource12.get(),
					.state = D3D12_RESOURCE_STATE_COMMON
				};
				request.finalColor = temporal::D3D12GpuView{
					.resource = (hdrFrameGenerationBackBuffer ? hdrFrameGenerationBackBuffer : a_frame.proxy)->resource12.get(),
					.state = D3D12_RESOURCE_STATE_COMMON
				};
				request.outputWidth = _chain.desc.Width;
				request.outputHeight = _chain.desc.Height;
				request.enabled = requested;
				request.uiMode = temporal::UiCompositionMode::kHudlessAndFinal;
				if (hdrFrameGenerationBackBuffer) {
					request.peakLuminanceNits = a_frame.hdr.peakNits;
					request.uiColorAndAlpha = temporal::D3D12GpuView{
						.resource = _buffers.ui[_frameSlot]->resource12.get(),
						.state = D3D12_RESOURCE_STATE_COMMON
					};
					request.uiMode = temporal::UiCompositionMode::kHudlessAndPremultipliedLayer;
					request.color = temporal::ContainerContract(a_frame.hdr.enabled);
				}
				pipeline.RecordFrameGenerationFrameTimeInput(
					request.frameTimeMilliseconds);
				const auto prepareResult = [&] {
					auto timing = pipeline.MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kPrepareFrame);
					return _provider.PrepareFrame(request);
				}();
				timer.EndScope(commandList, submission->timing, "D3D12::Present/FrameGenerationPrepare");
				frameGenerationPrepared = prepareResult.Succeeded();
				_providerGenerationEnabled = frameGenerationPrepared;
				if (!frameGenerationPrepared) {
					_vendorConsumptionPossible = false;
					FailFrame("Prepare frame", prepareResult);
					const auto paused = _provider.Pause();
					if (!paused.Succeeded()) {
						L->error("{}", temporal::FormatProviderFailure("Pause after failed prepare", paused));
						if (!ProveIdle(commandList)) {
							return E_FAIL;
						}
					}
				}
			} else if (_providerGenerationEnabled) {
				const auto disable = _provider.Pause();
				if (!disable.Succeeded()) {
					FailFrame("Disable frame generation", disable);
					if (!ProveIdle(commandList)) {
						return E_FAIL;
					}
				} else {
					_providerGenerationEnabled = false;
				}
			}

			timer.EndScope(commandList, submission->timing, "D3D12::Present");
			timer.Resolve(commandList, submission->timing);
			DX::ThrowIfFailed(commandList->Close());
			ID3D12CommandList* lists[] = { commandList };
			queue->ExecuteCommandLists(1, lists);
			_presentSubmissionMayBeInFlight = true;
			const UINT64 d3d12Done = _bridge.SignalQueue();
			submission->completionValue = d3d12Done;
			timer.Commit(submission->timing);
			_allocatorFenceValues[_frameSlot] = d3d12Done;
			const HRESULT producerWait = _bridge.D3D11WaitsForQueue(d3d12Done);
			handoff.End();
			DX::ThrowIfFailed(producerWait);
			_presentSubmissionMayBeInFlight = false;
			_preparedFrameGeneration =
				requested && frameGenerationPrepared && !_failures.Latched();
			if (_preparedTransaction) {
				pipeline.SetFrameGenerationPrepared(_frameSlot, _preparedFrameGeneration);
			}
			_presentPrepared = true;
		}

		cs::render::annotation::SetMarker("Upscaling/FrameGeneration/Present");
		pipeline.BeginPresentAttempt(a_args.flags);
		const HRESULT presentResult = [&] {
			auto timing = pipeline.MeasureFrameGenerationCpuPhase(
				render::FrameGenerationCpuPhase::kSdkPresent);
			return _chain.Present(a_args);
		}();
		pipeline.EndPresentAttempt(a_args.flags, presentResult);
		auto status = [&] {
			auto timing = pipeline.MeasureFrameGenerationCpuPhase(
				render::FrameGenerationCpuPhase::kCollectPresentStatus);
			return _provider.CollectPresentStatus(a_args.syncInterval, a_args.flags, presentResult);
		}();
		if (status.generatedFrames || status.presentedFrames) {
			pipeline.RecordGeneratedFrames(status.generatedFrames);
			pipeline.RecordPresentedFrames(status.presentedFrames);
		}
		const bool disableProviderAfterRetirement = !status.result.Succeeded();
		std::optional<temporal::PresentInputRetirementToken>
			retirementToken;
		if (_vendorConsumptionPossible &&
			presentResult != DXGI_ERROR_WAS_STILL_DRAWING) {
			const auto& dependency = status.inputCompletion;
			temporal::PresentInputRetirementSubmission retirement;
			temporal::PresentInputRetirementToken token{
				.value = _bridge.NextRetirementValue(),
				.realFrame = _preparedRealFrame,
				.resourceGeneration = _inputResourceGeneration,
				.queueIdentity = static_cast<std::uint64_t>(
					reinterpret_cast<std::uintptr_t>(_bridge.GetRetirementQueue()))
			};
			if (SUCCEEDED(presentResult)) {
				retirement = temporal::EnqueuePresentInputRetirement(
					queue, _bridge.GetRetirementQueue(),
					_bridge.GetPresentingCompletionFence(),
					_bridge.GetRetirementFence12(), token.value,
					dependency.value_or(
						temporal::GpuCompletionDependency{}));
				if (retirement.Succeeded()) {
					retirementToken = token;
					_retirementDiagnostics.signals.fetch_add(
						1, std::memory_order_relaxed);
				}
			}
			if (SUCCEEDED(presentResult) && !retirement.Succeeded()) {
				L->error(
					"{} input retirement failed to {}: HRESULT {:#010x}, "
					"dependency={}, queue-ordered={}, vendor fence value={}, "
					"retirement value={}",
					_provider.Name(),
					temporal::PresentInputRetirementOperationName(
						retirement.operation),
					static_cast<std::uint32_t>(retirement.result),
					dependency.has_value(), dependency && dependency->orderedQueue,
					dependency ? dependency->value : 0, token.value);
				_retirementDiagnostics.signalFailures.fetch_add(
					1, std::memory_order_relaxed);
				_retirementDiagnostics.violations.fetch_add(
					1, std::memory_order_relaxed);
				_bridge.Quarantine(
					"Frame-generation input retirement dependency failed.");
				return retirement.result;
			}
		}
		if (disableProviderAfterRetirement) {
			// Drain quarantines the bridge itself when it cannot prove the GPU idle.
			const HRESULT drainResult = _bridge.Drain();
			if (FAILED(drainResult)) {
				return drainResult;
			}
			const auto disableResult = _provider.Pause();
			if (disableResult.Succeeded()) {
				_providerGenerationEnabled = false;
			} else {
				FailFrame("Disable after present failure", disableResult);
			}
			if (_preparedTransaction) {
				pipeline.SetFrameGenerationPrepared(_frameSlot, false);
			}
		}
		if (_preparedTransaction) {
			pipeline.RecordPresentAttempt(_frameSlot, a_args.flags, presentResult);
		}
		if (disableProviderAfterRetirement) {
			FailFrame("Collect present status", status.result);
		}
		if (presentResult == DXGI_ERROR_WAS_STILL_DRAWING) {
			return presentResult;
		}
		if (FAILED(presentResult) && _vendorConsumptionPossible) {
			_retirementDiagnostics.violations.fetch_add(
				1, std::memory_order_relaxed);
			_bridge.Quarantine(
				"Present failed after vendor input consumption became possible.");
			return presentResult;
		}
		if (_preparedFrameGeneration && !disableProviderAfterRetirement) {
			_failures.Succeeded();
		}
		_inputReuseGate.MarkSubmitted(_frameSlot, retirementToken);
		_presentPrepared = false;
		_preparedFrameGeneration = false;
		_vendorConsumptionPossible = false;
		_preparedTransaction = false;
		_preparedRealFrame = 0;
		if (SUCCEEDED(presentResult)) {
			_frameSlot =
				(_frameSlot + 1) %
				static_cast<UINT>(_presentSubmissions.size());
			_frameIndex = _chain.chain->GetCurrentBackBufferIndex();
		} else {
			render::TemporalPipeline::Get().RequestFrameGenerationReset();
		}
		return presentResult;
	}

	ResizeOutcome D3D12FrameGenPresenter::Resize(UINT a_width, UINT a_height, UINT a_flags) noexcept
	{
		try {
			const UINT oldWidth = _chain.desc.Width;
			const UINT oldHeight = _chain.desc.Height;
			FrameGenerationBuffers pending;
			bool havePending = false;
			if (a_width != oldWidth || a_height != oldHeight) {
				const HRESULT bufferResult = CreateBuffers(a_width, a_height, pending);
				if (FAILED(bufferResult)) {
					return { .result = bufferResult };
				}
				havePending = true;
			}

			const auto releaseResult = DrainAndDestroy(_retirementDiagnostics.resizeDrains);
			if (!releaseResult.Succeeded()) {
				_faulted = true;
				FailFrame(releaseResult.message.empty() ? "Frame-generation display resources could not be released "
														  "for resize" :
														  releaseResult.message.c_str());
				return { .result = E_FAIL };
			}
			_providerGenerationEnabled = false;
			ResetSubmissions();
			for (auto& backBuffer : _backBuffers) {
				backBuffer = nullptr;
			}

			const HRESULT resizeResult = _chain.chain->ResizeBuffers(
				kBufferCount, a_width, a_height, _chain.desc.Format,
				swap_chain_facade::PreservePrivateResizeFlags(a_flags, _chain.desc));
			if (FAILED(resizeResult)) {
				if (FAILED(RefreshBackBuffers())) {
					_faulted = true;
				}
				const auto restored = _provider.CreateDisplayResources(
					oldWidth, oldHeight, PresentContract(), _chain.desc.BufferCount);
				if (!restored.Succeeded()) {
					FailFrame(
						"Frame-generation provider could not be restored "
						"after rejected resize");
				}
				render::TemporalPipeline::Get().RequestFrameGenerationReset();
				return { .result = resizeResult };
			}

			DXGI_SWAP_CHAIN_DESC1 resizedDesc{};
			const HRESULT descResult = _chain.chain->GetDesc1(&resizedDesc);
			if (FAILED(descResult)) {
				_faulted = true;
				FailFrame("resized swap-chain description was unavailable");
				return { .result = descResult };
			}
			const bool sizeChanged =
				resizedDesc.Width != oldWidth || resizedDesc.Height != oldHeight;
			if (sizeChanged &&
				(!havePending || a_width != resizedDesc.Width || a_height != resizedDesc.Height)) {
				const HRESULT bufferResult = CreateBuffers(resizedDesc.Width, resizedDesc.Height, pending);
				if (FAILED(bufferResult)) {
					_faulted = true;
					FailFrame("resized display resources were unavailable");
					return { .result = bufferResult };
				}
			}
			if (FAILED(RefreshBackBuffers())) {
				_faulted = true;
				FailFrame("resized back buffers were unavailable");
				return { .result = E_FAIL };
			}

			_chain.appliedColorSpace.reset();
			_chain.desc = resizedDesc;
			const HRESULT providerResult = sizeChanged ? CommitBuffers(std::move(pending), resizedDesc.Width, resizedDesc.Height) :
			                                             RestoreProvider(resizedDesc.Width, resizedDesc.Height);
			if (providerResult == S_FALSE) {
				FailFrame(
					"Frame-generation provider could not be restored after resize");
			}
			return { .result = resizeResult, .resized = true };
		} catch (const std::exception& e) {
			FailFrame(e.what());
			return { .result = E_FAIL };
		} catch (...) {
			FailFrame("unhandled resize failure");
			return { .result = E_FAIL };
		}
	}
}  // namespace cs::render
