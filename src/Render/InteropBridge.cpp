#include "InteropBridge.h"

#include "AgilityBootstrap.h"

#include <algorithm>
#include <exception>
#include <format>
#include <utility>
#include <vector>

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/FrameProfiler.h"
#include "Render/TemporalPipeline.h"

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.interop");

		// Timer records for the bridge's own submissions plus the presenter's.
		constexpr std::uint32_t kTimerRecords = 4;
	}  // namespace

	std::unique_ptr<SharedD3D11D3D12Texture> SharedD3D11D3D12Texture::Create(
		ID3D11Device5* a_device11, ID3D12Device* a_device12,
		const D3D11_TEXTURE2D_DESC& a_desc, std::string_view a_name)
	{
		auto texture = CreateTexture(a_device11, a_device12, a_desc, a_name);
		if (!texture) {
			return nullptr;
		}
		auto result = std::make_unique<SharedD3D11D3D12Texture>();
		result->texture11 = std::move(texture->resource);
		result->srv11 = std::move(texture->srv);
		result->uav11 = std::move(texture->uav);
		result->rtv11 = std::move(texture->rtv);
		result->resource12 = std::move(texture->resource12);
		return result;
	}

	std::unique_ptr<cs::buffer::Texture2D>
	SharedD3D11D3D12Texture::CreateTexture(
		ID3D11Device5* a_device11, ID3D12Device* a_device12,
		const D3D11_TEXTURE2D_DESC& a_desc, std::string_view a_name)
	{
		if (!a_device11 || !a_desc.Width || !a_desc.Height) {
			return nullptr;
		}

		auto desc = a_desc;
		if (a_device12) {
			desc.MiscFlags |=
				D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		}
		const auto check = [&](HRESULT a_result, const char* a_step) {
			if (FAILED(a_result)) {
				L->error(
					"Shared texture {} failed at {}: {}x{} format={} bind={:#x} "
					"misc={:#x} hr={:#010x}",
					a_name, a_step, desc.Width, desc.Height,
					static_cast<unsigned>(desc.Format), desc.BindFlags,
					desc.MiscFlags, static_cast<std::uint32_t>(a_result));
			}
			DX::ThrowIfFailed(a_result);
		};
		winrt::com_ptr<ID3D11Texture2D> resource11;
		check(a_device11->CreateTexture2D(&desc, nullptr, resource11.put()),
			"CreateTexture2D");

		winrt::com_ptr<ID3D12Resource> resource12;
		if (a_device12) {
			winrt::com_ptr<IDXGIResource1> dxgiResource;
			DX::ThrowIfFailed(
				resource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
			HANDLE sharedHandle = nullptr;
			DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
				nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
				&sharedHandle));
			const HRESULT openResult = a_device12->OpenSharedHandle(
				sharedHandle, IID_PPV_ARGS(resource12.put()));
			CloseHandle(sharedHandle);
			check(openResult, "OpenSharedHandle");
		}

		auto result = std::make_unique<cs::buffer::Texture2D>(resource11.detach());
		result->resource12 = std::move(resource12);
		if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
			DX::ThrowIfFailed(a_device11->CreateShaderResourceView(
				result->resource.get(), nullptr, result->srv.put()));
		}
		if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
			DX::ThrowIfFailed(a_device11->CreateUnorderedAccessView(
				result->resource.get(), nullptr, result->uav.put()));
		}
		if (desc.BindFlags & D3D11_BIND_RENDER_TARGET) {
			DX::ThrowIfFailed(a_device11->CreateRenderTargetView(
				result->resource.get(), nullptr, result->rtv.put()));
		}
		cs::render::annotation::SetName(result->resource.get(),
			std::string(a_name) + ".Texture11");
		cs::render::annotation::SetName(result->srv.get(),
			std::string(a_name) + ".SRV");
		cs::render::annotation::SetName(result->uav.get(),
			std::string(a_name) + ".UAV");
		cs::render::annotation::SetName(result->rtv.get(),
			std::string(a_name) + ".RTV");
		cs::render::annotation::SetName(result->resource12.get(),
			std::string(a_name) + ".Resource12");
		return result;
	}

	void InteropBridge::Retain(IUnknown* a_object)
	{
		static std::vector<winrt::com_ptr<IUnknown>> quarantine;
		if (a_object) {
			winrt::com_ptr<IUnknown> held;
			held.copy_from(a_object);
			quarantine.emplace_back(std::move(held));
		}
	}

	void InteropBridge::Retain(const std::unique_ptr<SharedD3D11D3D12Texture>& a_texture)
	{
		if (!a_texture) {
			return;
		}
		Retain(a_texture->texture11.get());
		Retain(a_texture->srv11.get());
		Retain(a_texture->uav11.get());
		Retain(a_texture->rtv11.get());
		Retain(a_texture->resource12.get());
	}

	InteropBridge::~InteropBridge()
	{
		if (FAILED(Rollback()) && _quarantined) {
			Retain(_device11.get());
			Retain(_context11.get());
			Retain(_device12.get());
			Retain(_queue.get());
			Retain(_retirementQueue.get());
			for (const auto& submission : _srSubmissions) {
				Retain(submission.allocator.get());
				Retain(submission.commandList.get());
			}
			for (auto* resource : _gpuTimer.Resources()) {
				Retain(resource);
			}
			Retain(_fence12.get());
			Retain(_fence11.get());
			Retain(_presentingCompletionFence.get());
			Retain(_inputRetirementFence12.get());
			Retain(_inputRetirementFence11.get());
		}
	}

	HRESULT InteropBridge::Initialize(IDXGIAdapter* a_adapter, ID3D11Device* a_device,
		ID3D11DeviceContext* a_context, IInteropRuntime& a_runtime,
		std::function<void()> a_onQuarantine)
	{
		if (_quarantined || !a_device || !a_context) {
			return E_INVALIDARG;
		}
		const HRESULT rollbackResult = Rollback();
		if (FAILED(rollbackResult)) {
			return rollbackResult;
		}
		_runtime = &a_runtime;
		_onQuarantine = std::move(a_onQuarantine);

		HRESULT result = S_OK;
		try {
			CreateDevices(a_adapter, a_device, a_context);
			CreateFences();
			const auto hooks = _runtime->DisablePresentationHooks();
			if (!hooks.Succeeded()) {
				L->error("{}", temporal::FormatProviderFailure(
								   "Inactive presentation hook disable", hooks));
				result = FAILED(hooks.hresult) ? hooks.hresult : E_FAIL;
			}
		} catch (const std::exception& e) {
			L->error("Interop bridge creation failed: {}", e.what());
			result = E_FAIL;
		} catch (...) {
			L->error("Interop bridge creation raised a non-standard exception");
			result = E_FAIL;
		}
		if (FAILED(result)) {
			const HRESULT cleanupResult = Rollback();
			return FAILED(cleanupResult) ? cleanupResult : result;
		}
		L->info("Interop bridge ready on the private D3D12 device");
		return S_OK;
	}

	HRESULT InteropBridge::Rollback() noexcept
	{
		if (_quarantined) {
			return DXGI_ERROR_DEVICE_REMOVED;
		}
		if (_queue && FAILED(Drain())) {
			MarkQuarantined();
			L->critical("Interop bridge resources quarantined after a failed drain");
			return E_FAIL;
		}
		if (_fenceEvent) {
			CloseHandle(_fenceEvent);
			_fenceEvent = nullptr;
		}
		_inputRetirementFence11 = nullptr;
		_inputRetirementFence12 = nullptr;
		_presentingCompletionFence = nullptr;
		_fence11 = nullptr;
		_fence12 = nullptr;
		for (auto& submission : _srSubmissions) {
			submission = {};
		}
		_gpuTimer.Release();
		_rcas.ResetD3D12();
		_retirementQueue = nullptr;
		_queue = nullptr;
		_captureDevice12 = nullptr;
		_device12 = nullptr;
		_deviceFactory = nullptr;
		_context11 = nullptr;
		_device11 = nullptr;
		_runtime = nullptr;
		_onQuarantine = nullptr;
		_superResolution.Retry();
		_nextSrSubmission = 0;
		_nextFenceValue = 1;
		_nextInputRetirementValue = 1;
		return S_OK;
	}

	void InteropBridge::NotifyHostFramePresented(std::uint32_t a_presentFlags) noexcept
	{
		if (_runtime) {
			_runtime->OnHostFramePresented(a_presentFlags);
		}
	}

	void InteropBridge::NotifyHostResize() noexcept
	{
		if (_runtime) {
			_runtime->OnHostResize();
		}
	}

	HRESULT InteropBridge::Drain() noexcept
	{
		if (_quarantined) {
			return DXGI_ERROR_DEVICE_REMOVED;
		}
		if (!_queue && !_retirementQueue) {
			return S_OK;
		}
		if (!_fenceEvent) {
			return S_OK;
		}
		if (_retirementQueue && _inputRetirementFence12) {
			const HRESULT retirementResult = WaitForInputRetirementGpu();
			if (FAILED(retirementResult)) {
				Quarantine(
					"The input-retirement queue did not reach its drain fence.");
				return retirementResult;
			}
		}
		if (!_queue || !_fence12) {
			return S_OK;
		}
		if (_context11 && _fence11 && _fence12) {
			const UINT64 d3d11Idle = _nextFenceValue++;
			const HRESULT signal =
				_context11->Signal(_fence11.get(), d3d11Idle);
			if (FAILED(signal)) {
				Quarantine(
					"The D3D11 producer fence could not be signaled.");
				return signal;
			}
			const HRESULT wait = _queue->Wait(_fence12.get(), d3d11Idle);
			if (FAILED(wait)) {
				Quarantine(
					"The D3D12 queue could not join the producer fence.");
				return wait;
			}
		}
		const HRESULT result = WaitForGpu();
		if (FAILED(result)) {
			Quarantine(
				"The temporal D3D12 queue did not reach its drain fence.");
		}
		return result;
	}

	bool InteropBridge::IsReady() const noexcept
	{
		return !_quarantined && _context11 && _device12 && _queue && _retirementQueue &&
		       _fence11 && _fence12 && _inputRetirementFence11 &&
		       _inputRetirementFence12 && _presentingCompletionFence &&
		       _fenceEvent &&
		       _srSubmissions[0].allocator && _srSubmissions[1].allocator &&
		       _srSubmissions[0].commandList && _srSubmissions[1].commandList;
	}

	void InteropBridge::CreateDevices(IDXGIAdapter* a_adapter,
		ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(_device11.put())));
		DX::ThrowIfFailed(a_context->QueryInterface(IID_PPV_ARGS(_context11.put())));

		winrt::com_ptr<IDXGIAdapter> actualAdapter;
		if (a_adapter) {
			actualAdapter.copy_from(a_adapter);
		} else {
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));
			DX::ThrowIfFailed(dxgiDevice->GetAdapter(actualAdapter.put()));
		}
		features::AgilityBootstrapDiagnostics agilityDiagnostics;
		DX::ThrowIfFailed(features::CreatePrivateD3D12Device(
			actualAdapter.get(), _runtime->D3D12SdkDirectory(),
			_deviceFactory, _device12.put(), &agilityDiagnostics));
		if (agilityDiagnostics.UsedSdkFactory()) {
			const auto& version = agilityDiagnostics.loadedVersion;
			L->info(
				"Created the private D3D12 device through SDK factory {} "
				"at {}; the factory selected {} core {} version "
				"{}.{}.{}.{}",
				features::kPrivateD3D12SdkVersion,
				agilityDiagnostics.sdkDirectory.string(),
				agilityDiagnostics.LoadedPackagedCore() ? "packaged" : "system/other",
				agilityDiagnostics.loadedD3D12Core.string(),
				version.major, version.minor, version.patch,
				version.revision);
		} else {
			const auto level = agilityDiagnostics.status ==
			                           features::AgilityBootstrapStatus::kRuntimeMissing ?
			                       spdlog::level::info :
			                       spdlog::level::warn;
			L->log(
				level,
				"Agility SDK factory {} was not activated "
				"(status={}, activation={:#010x}); using the system "
				"D3D12 runtime",
				features::kPrivateD3D12SdkVersion,
				features::AgilityBootstrapStatusName(
					agilityDiagnostics.status),
				static_cast<std::uint32_t>(
					agilityDiagnostics.activationResult));
		}
		_runtime->UpgradeDevice(_device12, *actualAdapter);
		(void)_runtime->NativeDevice(_device12.get(), _captureDevice12.put());
		if (!_captureDevice12) {
			L->warn(
				"Native D3D12 capture target is unavailable; RenderDoc temporal "
				"captures will remain disabled");
		}
		cs::render::annotation::SetName(_device12.get(),
			"Upscaling/FrameGeneration.Device");

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		DX::ThrowIfFailed(
			_device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(_queue.put())));
		cs::render::annotation::SetName(_queue.get(),
			"Upscaling/FrameGeneration.CommandQueue");
		DX::ThrowIfFailed(_device12->CreateCommandQueue(
			&queueDesc, IID_PPV_ARGS(_retirementQueue.put())));
		cs::render::annotation::SetName(_retirementQueue.get(),
			"Upscaling/FrameGeneration.InputRetirementQueue");
		// Failure leaves the timer disabled; submissions still work.
		_gpuTimer.Initialize(_device12.get(), _queue.get(), kTimerRecords,
			profiling::GetProfiler());
	}

	void InteropBridge::CreateFences()
	{
		DX::ThrowIfFailed(_device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
			IID_PPV_ARGS(_fence12.put())));
		cs::render::annotation::SetName(_fence12.get(),
			"Upscaling/FrameGeneration.Fence12");
		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(_device12->CreateSharedHandle(
			_fence12.get(), nullptr, GENERIC_ALL, nullptr, &sharedHandle));
		const HRESULT openResult =
			_device11->OpenSharedFence(sharedHandle, IID_PPV_ARGS(_fence11.put()));
		CloseHandle(sharedHandle);
		DX::ThrowIfFailed(openResult);
		cs::render::annotation::SetName(_fence11.get(),
			"Upscaling/FrameGeneration.Fence11");
		DX::ThrowIfFailed(_device12->CreateFence(
			0, D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(_presentingCompletionFence.put())));
		cs::render::annotation::SetName(
			_presentingCompletionFence.get(),
			"Upscaling/FrameGeneration.PresentingCompletionFence");
		DX::ThrowIfFailed(_device12->CreateFence(
			0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(_inputRetirementFence12.put())));
		cs::render::annotation::SetName(
			_inputRetirementFence12.get(),
			"Upscaling/FrameGeneration.InputRetirementFence12");
		sharedHandle = nullptr;
		DX::ThrowIfFailed(_device12->CreateSharedHandle(_inputRetirementFence12.get(),
			nullptr, GENERIC_ALL, nullptr,
			&sharedHandle));
		const HRESULT openRetirementResult = _device11->OpenSharedFence(
			sharedHandle, IID_PPV_ARGS(_inputRetirementFence11.put()));
		CloseHandle(sharedHandle);
		DX::ThrowIfFailed(openRetirementResult);
		cs::render::annotation::SetName(
			_inputRetirementFence11.get(),
			"Upscaling/FrameGeneration.InputRetirementFence11");
		_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		DX::ThrowIfFailed(_fenceEvent ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

		CreateSubmissions(_srSubmissions, "SuperResolution");
		if (_runtime->SharpensSuperResolution() &&
			!_rcas.InitializeD3D12(_device12.get())) {
			L->warn(
				"Native D3D12 RCAS initialization failed; DLSS sharpening "
				"requests will fail closed");
		}
	}

	void InteropBridge::CreateSubmissions(SubmissionRing& a_ring, std::string_view a_phase)
	{
		for (UINT index = 0; index < a_ring.size(); ++index) {
			auto& submission = a_ring[index];
			DX::ThrowIfFailed(_device12->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(submission.allocator.put())));
			DX::ThrowIfFailed(_device12->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				submission.allocator.get(), nullptr,
				IID_PPV_ARGS(submission.commandList.put())));
			DX::ThrowIfFailed(submission.commandList->Close());
			submission.timing = _gpuTimer.Reserve();
			cs::render::annotation::SetName(
				submission.allocator.get(),
				std::format(
					"Upscaling/{}Allocator[{}].CommandAllocator",
					a_phase, index));
			cs::render::annotation::SetName(
				submission.commandList.get(),
				std::format(
					"Upscaling/{}CommandList[{}].CommandList",
					a_phase, index));
		}
	}

	void InteropBridge::MarkQuarantined() noexcept
	{
		if (!std::exchange(_quarantined, true)) {
			try {
				L->error(
					"Device removed reasons: D3D12={:#010x} D3D11={:#010x}",
					_device12 ? static_cast<std::uint32_t>(_device12->GetDeviceRemovedReason()) : 0u,
					_device11 ? static_cast<std::uint32_t>(_device11->GetDeviceRemovedReason()) : 0u);
			} catch (...) {
			}
		}
		if (_onQuarantine) {
			_onQuarantine();
		}
	}

	void InteropBridge::Quarantine(std::string_view a_reason) noexcept
	{
		const bool firstFailure = !_quarantined;
		MarkQuarantined();
		if (firstFailure) {
			try {
				render::TemporalPipeline::Get().Quarantine(
					render::temporal::FailureDomain::kTransport,
					std::string(a_reason));
			} catch (...) {
			}
		}
	}

	std::unique_ptr<cs::buffer::Texture2D>
	InteropBridge::CreateSharedTexture(
		const D3D11_TEXTURE2D_DESC& a_desc,
		std::string_view a_name) const
	{
		if (!IsReady()) {
			return nullptr;
		}
		try {
			return SharedD3D11D3D12Texture::CreateTexture(
				_device11.get(), _device12.get(), a_desc, a_name);
		} catch (const std::exception& e) {
			L->error("Could not create shared temporal texture {}: {}", a_name,
				e.what());
		} catch (...) {
			L->error("Could not create shared temporal texture {}", a_name);
		}
		return nullptr;
	}

	void InteropBridge::QueueWaitsForD3D11()
	{
		const UINT64 d3d11Ready = _nextFenceValue++;
		DX::ThrowIfFailed(_context11->Signal(_fence11.get(), d3d11Ready));
		DX::ThrowIfFailed(_queue->Wait(_fence12.get(), d3d11Ready));
	}

	std::uint64_t InteropBridge::SignalQueue()
	{
		const UINT64 value = _nextFenceValue++;
		DX::ThrowIfFailed(_queue->Signal(_fence12.get(), value));
		return value;
	}

	HRESULT InteropBridge::D3D11WaitsForQueue(std::uint64_t a_value) noexcept
	{
		return _context11->Wait(_fence11.get(), a_value);
	}

	temporal::ProviderResult InteropBridge::EvaluateD3D12SuperResolution(
		temporal::ISuperResolutionProvider& a_provider,
		const temporal::SuperResolutionRequest& a_request)
	{
		bool submissionMayBeInFlight = false;
		const auto* recording = std::get_if<temporal::D3D11RecordingContext>(
			&a_request.recording);
		const auto alias = [](const temporal::GpuView& a_view) {
			const auto* view =
				std::get_if<temporal::D3D11GpuView>(&a_view);
			return view && view->resource && view->alias12 ? temporal::D3D12GpuView{
				.resource = view->alias12,
				.state = view->alias12State
			} :
			                                                 temporal::D3D12GpuView{};
		};
		const auto color = alias(a_request.colorInput);
		const auto output = alias(a_request.privateOutput);
		const auto publication = alias(a_request.publicationOutput);
		const auto depth = alias(a_request.depth);
		const auto motion = alias(a_request.motionVectors);
		const auto reactive = alias(a_request.reactiveMask);
		const auto transparency =
			alias(a_request.transparencyCompositionMask);
		if (!IsReady() || !recording || !recording->context ||
			!color.resource || !output.resource || !depth.resource ||
			!motion.resource || !reactive.resource ||
			!transparency.resource ||
			(a_request.postProcessSharpening &&
				(!publication.resource ||
					publication.resource == output.resource))) {
			return {
				.code = temporal::ProviderResultCode::kFailure,
				.hresult = E_INVALIDARG,
				.message =
					"D3D12 super resolution requires shared producer resources.",
				.failureDomain = temporal::FailureDomain::kSuperResolution
			};
		}

		try {
			SubmissionRecord* submission = nullptr;
			DX::ThrowIfFailed(AcquireSubmission(
				_srSubmissions, _nextSrSubmission, submission));
			QueueWaitsForD3D11();
			DX::ThrowIfFailed(submission->allocator->Reset());
			DX::ThrowIfFailed(submission->commandList->Reset(
				submission->allocator.get(), nullptr));
			auto* commandList = submission->commandList.get();
			_gpuTimer.Begin(submission->timing);
			_gpuTimer.BeginScope(commandList, submission->timing, "D3D12::SuperResolution");
			auto request = a_request;
			request.recording = temporal::D3D12RecordingContext{
				.commandList = commandList,
				.queue = _queue.get(),
				.slot = 0
			};
			request.colorInput = color;
			request.privateOutput = output;
			request.depth = depth;
			request.motionVectors = motion;
			request.reactiveMask = reactive;
			request.transparencyCompositionMask = transparency;
			_gpuTimer.BeginScope(commandList, submission->timing, "D3D12::SuperResolution/Provider");
			auto providerResult = a_provider.Record(request);
			_gpuTimer.EndScope(commandList, submission->timing, "D3D12::SuperResolution/Provider");
			if (!providerResult.Succeeded()) {
				DX::ThrowIfFailed(commandList->Close());
				if (providerResult.code ==
						temporal::ProviderResultCode::kFailure &&
					providerResult.failureDomain ==
						temporal::FailureDomain::kNone) {
					providerResult.failureDomain =
						temporal::FailureDomain::kSuperResolution;
				}
				return providerResult;
			}
			if (providerResult.workState !=
				temporal::ProviderWorkState::kRecorded) {
				DX::ThrowIfFailed(commandList->Close());
				return {
					.code =
						temporal::ProviderResultCode::kFailure,
					.message =
						"The D3D12 super-resolution provider did not report "
						"recorded work.",
					.failureDomain =
						temporal::FailureDomain::kSuperResolution
				};
			}
			if (a_request.postProcessSharpening) {
				_gpuTimer.BeginScope(commandList, submission->timing, "D3D12::SuperResolution/Sharpen");
				const bool sharpened = _rcas.RecordSharpen(commandList,
					submission->postProcessDescriptors,
					output.resource, output.state,
					publication.resource, publication.state,
					a_request.outputWidth, a_request.outputHeight,
					a_request.postProcessSharpness);
				_gpuTimer.EndScope(commandList, submission->timing, "D3D12::SuperResolution/Sharpen");
				if (!sharpened) {
					DX::ThrowIfFailed(commandList->Close());
					return {
						.code =
							temporal::ProviderResultCode::kFailure,
						.message =
							"Native D3D12 RCAS recording failed.",
						.failureDomain =
							temporal::FailureDomain::kSuperResolution
					};
				}
				providerResult.publicationOutputReady = true;
			}
			_gpuTimer.EndScope(commandList, submission->timing, "D3D12::SuperResolution");
			_gpuTimer.Resolve(commandList, submission->timing);
			DX::ThrowIfFailed(commandList->Close());
			ID3D12CommandList* lists[]{ commandList };
			_queue->ExecuteCommandLists(1, lists);
			submissionMayBeInFlight = true;
			const UINT64 d3d12Done = SignalQueue();
			submission->completionValue = d3d12Done;
			_gpuTimer.Commit(submission->timing);
			DX::ThrowIfFailed(D3D11WaitsForQueue(d3d12Done));
			providerResult.workState =
				temporal::ProviderWorkState::kOutputReady;
			providerResult.outputDependencyEstablished = true;
			return providerResult;
		} catch (const winrt::hresult_error& e) {
			const auto error = static_cast<HRESULT>(e.code());
			MarkQuarantined();
			L->error("D3D12 super-resolution bridge failed: {}",
				winrt::to_string(e.message()));
			return {
				.code = temporal::ProviderResultCode::kFailure,
				.hresult = error,
				.message = "D3D12 super-resolution submission failed.",
				.failureDomain = temporal::FailureDomain::kTransport,
				.workState = submissionMayBeInFlight ? temporal::ProviderWorkState::kSubmitted : temporal::ProviderWorkState::kNone
			};
		} catch (const std::exception& e) {
			MarkQuarantined();
			L->error("D3D12 super-resolution bridge failed: {}", e.what());
			return {
				.code = temporal::ProviderResultCode::kFailure,
				.message = e.what(),
				.failureDomain = temporal::FailureDomain::kTransport,
				.workState = submissionMayBeInFlight ? temporal::ProviderWorkState::kSubmitted : temporal::ProviderWorkState::kNone
			};
		}
		return {
			.code = temporal::ProviderResultCode::kFailure,
			.message = "D3D12 super-resolution submission failed.",
			.failureDomain = temporal::FailureDomain::kTransport
		};
	}

	HRESULT InteropBridge::AcquireSubmission(
		SubmissionRing& a_ring, UINT& a_cursor, SubmissionRecord*& a_record) noexcept
	{
		a_record = nullptr;
		if (!_fence12 || !_fenceEvent) {
			return E_FAIL;
		}
		const auto completed = _fence12->GetCompletedValue();
		if (completed == UINT64_MAX) {
			Quarantine(
				"The temporal D3D12 submission fence reported device removal.");
			return DXGI_ERROR_DEVICE_REMOVED;
		}
		for (UINT offset = 0; offset < a_ring.size(); ++offset) {
			const UINT index =
				(a_cursor + offset) % static_cast<UINT>(a_ring.size());
			auto& candidate = a_ring[index];
			if (!candidate.completionValue ||
				completed >= candidate.completionValue) {
				_gpuTimer.Collect(candidate.timing);
				a_cursor =
					(index + 1) % static_cast<UINT>(a_ring.size());
				a_record = &candidate;
				return S_OK;
			}
		}

		auto& oldest = a_ring[a_cursor];
		const HRESULT wait = WaitForSubmission(oldest);
		if (FAILED(wait)) {
			Quarantine(
				"A temporal D3D12 command allocator did not retire safely.");
			return wait;
		}
		_gpuTimer.Collect(oldest.timing);
		a_record = &oldest;
		a_cursor =
			(a_cursor + 1) % static_cast<UINT>(a_ring.size());
		return S_OK;
	}

	HRESULT InteropBridge::WaitForSubmission(
		const SubmissionRecord& a_record) noexcept
	{
		if (!a_record.completionValue ||
			_fence12->GetCompletedValue() >= a_record.completionValue) {
			return S_OK;
		}
		const HRESULT result = _fence12->SetEventOnCompletion(
			a_record.completionValue, _fenceEvent);
		if (FAILED(result)) {
			return result;
		}
		return WaitForSingleObject(_fenceEvent, INFINITE) == WAIT_OBJECT_0 ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	}

	HRESULT InteropBridge::WaitForGpu() noexcept
	{
		if (!_queue || !_fence12 || !_fenceEvent) {
			return E_FAIL;
		}
		const UINT64 value = _nextFenceValue++;
		HRESULT result = _queue->Signal(_fence12.get(), value);
		if (FAILED(result)) {
			return result;
		}
		result = _fence12->SetEventOnCompletion(value, _fenceEvent);
		if (FAILED(result)) {
			return result;
		}
		return WaitForSingleObject(_fenceEvent, INFINITE) == WAIT_OBJECT_0 ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	}

	HRESULT InteropBridge::WaitForInputRetirementGpu() noexcept
	{
		if (!_retirementQueue || !_inputRetirementFence12 || !_fenceEvent) {
			return E_FAIL;
		}
		const UINT64 value = _nextInputRetirementValue++;
		HRESULT result =
			_retirementQueue->Signal(_inputRetirementFence12.get(), value);
		if (FAILED(result)) {
			return result;
		}
		result =
			_inputRetirementFence12->SetEventOnCompletion(value, _fenceEvent);
		if (FAILED(result)) {
			return result;
		}
		return WaitForSingleObject(_fenceEvent, INFINITE) == WAIT_OBJECT_0 ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	}
}  // namespace cs::render
