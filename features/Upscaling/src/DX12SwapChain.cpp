#include "DX12SwapChain.h"
#include "DXGISwapChainFacadeContract.h"

#include "AgilityBootstrap.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/RendererContext.h"
#include "Render/TemporalPipeline.h"
#include "Render/TemporalRenderer.h"
#include "Streamline.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.dx12swapchain");

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
		if (!a_device11 || !a_device12 || !a_desc.Width || !a_desc.Height) {
			return nullptr;
		}

		auto desc = a_desc;
		desc.MiscFlags |=
			D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
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

		winrt::com_ptr<IDXGIResource1> dxgiResource;
		DX::ThrowIfFailed(
			resource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
			nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
			&sharedHandle));
		winrt::com_ptr<ID3D12Resource> resource12;
		const HRESULT openResult = a_device12->OpenSharedHandle(
			sharedHandle, IID_PPV_ARGS(resource12.put()));
		CloseHandle(sharedHandle);
		check(openResult, "OpenSharedHandle");

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

	DX12SwapChain::~DX12SwapChain()
	{
		if (FAILED(Rollback()) && _quarantined) {
			// A failed drain means the GPU or SDK may still own references to these
			// objects. Keep one process-lifetime quarantine instead of releasing
			// resources whose retirement was not proven.
			static std::vector<winrt::com_ptr<IUnknown>> quarantine;
			const auto retain = [&](IUnknown* a_object) {
				if (a_object) {
					winrt::com_ptr<IUnknown> held;
					held.copy_from(a_object);
					quarantine.emplace_back(std::move(held));
				}
			};
			retain(_device11.get());
			retain(_outwardDevice11.get());
			retain(_context11.get());
			retain(_device12.get());
			retain(_queue.get());
			for (const auto& submission : _srSubmissions) {
				retain(submission.allocator.get());
				retain(submission.commandList.get());
			}
			for (const auto& submission : _presentSubmissions) {
				retain(submission.allocator.get());
				retain(submission.commandList.get());
			}
			for (const auto& backBuffer : _backBuffers) {
				retain(backBuffer.get());
			}
			retain(_fence12.get());
			retain(_fence11.get());
			retain(_inputRetirementFence12.get());
			retain(_inputRetirementFence11.get());
			retain(_swapChain.get());
			const auto retainShared = [&](const auto& a_shared) {
				if (!a_shared) {
					return;
				}
				retain(a_shared->texture11.get());
				retain(a_shared->srv11.get());
				retain(a_shared->uav11.get());
				retain(a_shared->rtv11.get());
				retain(a_shared->resource12.get());
			};
			retainShared(_proxyBuffer);
			for (const auto& resource : _hudlessBuffers) {
				retainShared(resource);
			}
			for (const auto& resource : _depthBuffers) {
				retainShared(resource);
			}
			for (const auto& resource : _motionBuffers) {
				retainShared(resource);
			}
		}
	}

	HRESULT DX12SwapChain::Initialize(
		IDXGIAdapter* a_adapter, ID3D11Device* a_device,
		ID3D11DeviceContext* a_context, const DXGI_SWAP_CHAIN_DESC& a_desc,
		Streamline* a_streamline,
		render::temporal::IFrameGenerationProvider* a_provider,
		TemporalPresentationCallbacks a_callbacks)
	{
		if (_published || _quarantined || !a_device || !a_context ||
			!a_desc.OutputWindow ||
			(a_desc.BufferDesc.Format != DXGI_FORMAT_UNKNOWN &&
				a_desc.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)) {
			return E_INVALIDARG;
		}
		const HRESULT rollbackResult = Rollback();
		if (FAILED(rollbackResult)) {
			return rollbackResult;
		}
		_streamline = a_streamline;
		_provider = a_provider;
		_callbacks = std::move(a_callbacks);
		_creationDesc = a_desc;
		_proxyDesc = a_desc;

		HRESULT result = CreateDevices(a_adapter, a_device, a_context);
		if (SUCCEEDED(result) && _streamline) {
			const auto disableDlssG = _streamline->featureDLSSG
				? _streamline->SetDLSSGPresentationActive(false)
				: render::temporal::ProviderResult{
					  .code =
						  render::temporal::ProviderResultCode::kSuccess
				  };
			const auto disableFsrG = disableDlssG.Succeeded() &&
					_streamline->featureFSRG
				? _streamline->SetFSRGPresentationActive(false)
				: disableDlssG;
			if (!disableFsrG.Succeeded()) {
				if (_callbacks.recordFailure) {
					const auto reason =
						render::temporal::FormatProviderFailure(
							"Inactive presentation hook disable",
							disableFsrG);
					_callbacks.recordFailure(reason.c_str());
				}
				result = FAILED(disableFsrG.hresult)
					? disableFsrG.hresult
					: E_FAIL;
			}
		}
		if (SUCCEEDED(result) && _provider && !_provider->IsAvailable()) {
			if (_callbacks.recordFailure) {
				_callbacks.recordFailure(
					"The selected frame-generation provider is unavailable; "
					"using plain D3D12 presentation.");
			}
			_provider = nullptr;
		}
		winrt::com_ptr<IDXGIAdapter> actualAdapter;
		if (SUCCEEDED(result)) {
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			result = _device11->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()));
			if (SUCCEEDED(result)) {
				result = dxgiDevice->GetAdapter(actualAdapter.put());
			}
			if (SUCCEEDED(result)) {
				result = CreateSwapChain(actualAdapter.get(), a_desc);
			}
		}
		if (SUCCEEDED(result)) {
			result = CreateInteropFence();
		}
		if (SUCCEEDED(result)) {
			result = RecreateDisplayResources(_innerDesc.Width, _innerDesc.Height);
		}
		if (SUCCEEDED(result) && _provider) {
			result =
				RecreateFrameGenerationResources(_innerDesc.Width, _innerDesc.Height);
			if (result != S_OK) {
				if (_callbacks.recordFailure) {
					_callbacks.recordFailure(
						"Frame-generation resource initialization failed; "
						"falling back to plain D3D12 presentation.");
				}
				auto* failedProvider = _provider;
				const auto release =
					render::temporal::RetirePresentationProvider(
						*failedProvider,
						[this]() { return SUCCEEDED(Drain()); },
						[this]() { ReleasePrivatePresentationResources(); });
				RecordGlobalDrain("startup", release);
				if (!release.Succeeded()) {
					_quarantined = true;
					result = E_FAIL;
				} else {
					_providerPresentationActive = false;
					_provider = nullptr;
					result = CreateSwapChain(actualAdapter.get(), a_desc);
					if (SUCCEEDED(result)) {
						result = RecreateDisplayResources(
							_innerDesc.Width, _innerDesc.Height);
					}
				}
			}
		}
		if (SUCCEEDED(result)) {
			result = RefreshBackBuffers();
		}
		if (FAILED(result)) {
			const HRESULT cleanupResult = Rollback();
			return FAILED(cleanupResult) ? cleanupResult : result;
		}

		_proxy.attach(new DXGISwapChainProxy(*this, *_swapChain));
		_published = true;
		ClearSharedBuffers();
		L->info("Published D3D11-facing {} proxy at {}x{} R8G8B8A8_UNORM",
			_provider ? _provider->Name() : "plain D3D12", _innerDesc.Width,
			_innerDesc.Height);
		return S_OK;
	}

	HRESULT DX12SwapChain::Rollback() noexcept
	{
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		if (_proxy) {
			_proxy->DetachOwner();
			_proxy = nullptr;
		}
		_callbacks = {};
		_published = false;
		if (_quarantined) {
			return DXGI_ERROR_DEVICE_REMOVED;
		}
		if (_provider && _providerPresentationActive) {
			const auto release =
				render::temporal::RetirePresentationProvider(
					*_provider, [this]() { return SUCCEEDED(Drain()); },
					[this]() { ReleasePrivatePresentationResources(); });
			RecordGlobalDrain("teardown", release);
			if (!release.Succeeded()) {
				_quarantined = true;
				L->critical(
					"Temporal D3D12 resources quarantined after failed provider "
					"drain: {}",
					release.message.empty()
						? "frame-generation display resources could not be released"
						: release.message);
				return E_FAIL;
			}
		} else if (_queue && FAILED(Drain())) {
			_quarantined = true;
			L->critical(
				"Temporal D3D12 resources quarantined after failed plain "
				"presentation drain");
			return E_FAIL;
		}
		if (_fenceEvent) {
			CloseHandle(_fenceEvent);
			_fenceEvent = nullptr;
		}
		for (auto& motion : _motionBuffers) {
			motion.reset();
		}
		for (auto& depth : _depthBuffers) {
			depth.reset();
		}
		for (auto& hudless : _hudlessBuffers) {
			hudless.reset();
		}
		_proxyBuffer.reset();
		for (auto& backBuffer : _backBuffers) {
			backBuffer = nullptr;
		}
		_swapChain = nullptr;
		_inputRetirementFence11 = nullptr;
		_inputRetirementFence12 = nullptr;
		_fence11 = nullptr;
		_fence12 = nullptr;
		for (auto& submission : _srSubmissions) {
			submission = {};
		}
		for (auto& submission : _presentSubmissions) {
			submission = {};
		}
		_rcas.ResetD3D12();
		_queue = nullptr;
		_device12 = nullptr;
		_deviceFactory = nullptr;
		_adapter = nullptr;
		_outwardDevice11 = nullptr;
		_context11 = nullptr;
		_device11 = nullptr;
		_streamline = nullptr;
		_provider = nullptr;
		_frameGenerationInputsReady = false;
		_frameGenerationDisabled = false;
		_providerGenerationEnabled = false;
		_providerPresentationActive = false;
		_nextSrSubmission = 0;
		_nextPresentSubmission = 0;
		_nextInputRetirementValue = 1;
		_preparedRealFrame = 0;
		_inputResourceGeneration = 0;
		render::temporal::ResetPresentationProtocol(
			_allocatorFenceValues, _inputReuseGate, _frameSlot, _presentPrepared,
			_preparedFrameGeneration, _vendorConsumptionPossible,
			_preparedTransaction);
		_presentSubmissionMayBeInFlight = false;
		_quarantined = false;
		return S_OK;
	}

	HRESULT DX12SwapChain::Drain() noexcept
	{
		if (_quarantined) {
			return DXGI_ERROR_DEVICE_REMOVED;
		}
		if (!_queue) {
			return S_OK;
		}
		if (!_fence12 || !_fenceEvent) {
			return S_OK;
		}
		if (_context11 && _fence11 && _fence12) {
			const UINT64 d3d11Idle = _nextFenceValue++;
			const HRESULT signal =
				_context11->Signal(_fence11.get(), d3d11Idle);
			if (FAILED(signal)) {
				QuarantineTransport(
					"The D3D11 producer fence could not be signaled.");
				return signal;
			}
			const HRESULT wait = _queue->Wait(_fence12.get(), d3d11Idle);
			if (FAILED(wait)) {
				QuarantineTransport(
					"The D3D12 queue could not join the producer fence.");
				return wait;
			}
		}
		const HRESULT result = WaitForGpu();
		if (FAILED(result)) {
			QuarantineTransport(
				"The temporal D3D12 queue did not reach its drain fence.");
		}
		return result;
	}

	HRESULT DX12SwapChain::CreateDevices(IDXGIAdapter* a_adapter,
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context)
	{
		DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(_device11.put())));
		_outwardDevice11.copy_from(a_device);
		DX::ThrowIfFailed(a_context->QueryInterface(IID_PPV_ARGS(_context11.put())));

		winrt::com_ptr<IDXGIAdapter> actualAdapter;
		if (a_adapter) {
			actualAdapter.copy_from(a_adapter);
		} else {
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));
			DX::ThrowIfFailed(dxgiDevice->GetAdapter(actualAdapter.put()));
		}
		_adapter = actualAdapter;
		AgilityBootstrapDiagnostics agilityDiagnostics;
		DX::ThrowIfFailed(CreatePrivateD3D12Device(
			actualAdapter.get(),
			std::filesystem::path(Streamline::PluginDir) / L"D3D12",
			_deviceFactory, _device12.put(), &agilityDiagnostics));
		if (agilityDiagnostics.UsedSdkFactory()) {
			const auto& version = agilityDiagnostics.loadedVersion;
			L->info(
				"Created the private D3D12 device through SDK factory {} "
				"at {}; the factory selected {} core {} version "
				"{}.{}.{}.{}",
				kPrivateD3D12SdkVersion,
				agilityDiagnostics.sdkDirectory.string(),
				agilityDiagnostics.LoadedPackagedCore()
					? "packaged"
					: "system/other",
				agilityDiagnostics.loadedD3D12Core.string(),
				version.major, version.minor, version.patch,
				version.revision);
		} else {
			const auto level = agilityDiagnostics.status ==
					AgilityBootstrapStatus::kRuntimeMissing
				? spdlog::level::info
				: spdlog::level::warn;
			L->log(
				level,
				"Agility SDK factory {} was not activated "
				"(status={}, activation={:#010x}); using the system "
				"D3D12 runtime",
				kPrivateD3D12SdkVersion,
				AgilityBootstrapStatusName(
					agilityDiagnostics.status),
				static_cast<std::uint32_t>(
					agilityDiagnostics.activationResult));
		}
		if (_streamline && _streamline->initialized) {
			_streamline->NotifyD3D12DeviceChange();
			auto rawDevice = _device12;
			ID3D12Device* preparedDevice = _device12.detach();
			const bool prepared =
				_streamline->PrepareD3D12Device(&preparedDevice);
			winrt::com_ptr<ID3D12Device> upgradedDevice;
			upgradedDevice.attach(preparedDevice);
			if (prepared && upgradedDevice) {
				_device12 = std::move(upgradedDevice);
				_streamline->CheckFeatures(actualAdapter.get());
				_streamline->PostDevice();
			} else {
				_device12 = std::move(rawDevice);
				_streamline->deviceRegistered = false;
				_streamline->featureDLSS = false;
				_streamline->featureDLSSG = false;
				_streamline->featureFSR = false;
				_streamline->featureFSRG = false;
				_streamline->featurePCL = false;
				_streamline->featureReflex = false;
				L->error(
					"Streamline D3D12 device preparation failed; retaining raw "
					"D3D12 presentation without Streamline consumers");
			}
		}
		cs::render::annotation::SetName(_device12.get(),
			"Upscaling/FrameGeneration.Device");

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		DX::ThrowIfFailed(
			_device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(_queue.put())));
		cs::render::annotation::SetName(_queue.get(),
			"Upscaling/FrameGeneration.CommandQueue");
		const auto createSubmissions = [&](auto& a_submissions,
										 std::string_view a_phase) {
			for (UINT index = 0; index < a_submissions.size(); ++index) {
				auto& submission = a_submissions[index];
				DX::ThrowIfFailed(_device12->CreateCommandAllocator(
					D3D12_COMMAND_LIST_TYPE_DIRECT,
					IID_PPV_ARGS(submission.allocator.put())));
				DX::ThrowIfFailed(_device12->CreateCommandList(
					0, D3D12_COMMAND_LIST_TYPE_DIRECT,
					submission.allocator.get(), nullptr,
					IID_PPV_ARGS(submission.commandList.put())));
				DX::ThrowIfFailed(submission.commandList->Close());
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
		};
		createSubmissions(_srSubmissions, "SuperResolution");
		createSubmissions(_presentSubmissions, "Present");
		if (_streamline && _streamline->featureDLSS &&
			!_rcas.InitializeD3D12(_device12.get())) {
			L->warn(
				"Native D3D12 RCAS initialization failed; DLSS sharpening "
				"requests will fail closed");
		}
		return S_OK;
	}

	HRESULT DX12SwapChain::CreateSwapChain(IDXGIAdapter* a_adapter,
		const DXGI_SWAP_CHAIN_DESC& a_desc, bool a_allowProviderFallback,
		render::temporal::ProviderResult* a_providerFailure)
	{
		if (!a_adapter) {
			return E_INVALIDARG;
		}
		winrt::com_ptr<IDXGIFactory4> factory;
		DX::ThrowIfFailed(a_adapter->GetParent(IID_PPV_ARGS(factory.put())));
		if (_streamline && _streamline->initialized &&
			_streamline->deviceRegistered) {
			auto rawFactory = factory;
			IDXGIFactory4* preparedFactory = factory.detach();
			const bool prepared =
				_streamline->PrepareDXGIFactory(&preparedFactory);
			winrt::com_ptr<IDXGIFactory4> upgradedFactory;
			upgradedFactory.attach(preparedFactory);
			if (prepared && upgradedFactory) {
				factory = std::move(upgradedFactory);
			} else {
				factory = std::move(rawFactory);
				if (!a_allowProviderFallback) {
					if (a_providerFailure) {
						*a_providerFailure = {
							.code =
								render::temporal::ProviderResultCode::kFailure,
							.message =
								"Streamline DXGI factory preparation failed.",
							.failureDomain =
								render::temporal::FailureDomain::kStreamline
						};
					}
					return E_FAIL;
				}
				_streamline->deviceRegistered = false;
				_streamline->featureDLSS = false;
				_streamline->featureDLSSG = false;
				_streamline->featureFSR = false;
				_streamline->featureFSRG = false;
				_streamline->featurePCL = false;
				_streamline->featureReflex = false;
				L->error(
					"Streamline DXGI factory preparation failed; retaining raw "
					"D3D12 presentation without Streamline consumers");
			}
		}

		_innerDesc = {};
		_innerDesc.Width = a_desc.BufferDesc.Width;
		_innerDesc.Height = a_desc.BufferDesc.Height;
		if (!_innerDesc.Width || !_innerDesc.Height) {
			RECT client{};
			if (!GetClientRect(a_desc.OutputWindow, &client)) {
				return HRESULT_FROM_WIN32(GetLastError());
			}
			_innerDesc.Width =
				static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
			_innerDesc.Height =
				static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));
		}
		_innerDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		_innerDesc.SampleDesc.Count = 1;
		_innerDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		_innerDesc.BufferCount = 2;
		_innerDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		_innerDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		_innerDesc.Flags =
			a_desc.Flags & (DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
							   DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
							   DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

		winrt::com_ptr<IDXGISwapChain4> swapChain;
		HRESULT result = E_FAIL;
		if (_provider) {
			const auto activation = _provider->SetPresentationActive(true);
			if (!activation.Succeeded()) {
				if (a_providerFailure) {
					*a_providerFailure = activation;
				}
				if (!a_allowProviderFallback) {
					return FAILED(activation.hresult)
						? activation.hresult
						: E_FAIL;
				}
				if (_callbacks.recordFailure) {
					const auto reason =
						render::temporal::FormatProviderFailure(
							"Frame-generation presentation activation",
							activation);
					_callbacks.recordFailure(reason.c_str());
				}
				_provider = nullptr;
				_frameGenerationDisabled = true;
			} else {
				_providerPresentationActive = true;
			}
		}
		if (_provider) {
			const auto providerResult =
				_provider->CreatePresentation({ .adapter = a_adapter,
												  .device = _device12.get(),
												  .queue = _queue.get(),
												  .factory = factory.get(),
												  .window = a_desc.OutputWindow,
												  .description = &_innerDesc },
					swapChain.put());
			if (!providerResult.Succeeded() && a_providerFailure) {
				*a_providerFailure = providerResult;
			}
			if (providerResult.Succeeded()) {
				result = S_OK;
			} else if (FAILED(providerResult.hresult)) {
				result = providerResult.hresult;
			} else {
				result = providerResult.sdkResult
					? static_cast<HRESULT>(providerResult.sdkResult)
					: E_FAIL;
			}
			if (FAILED(result) || !swapChain) {
				if (!a_allowProviderFallback) {
					return FAILED(result) ? result : E_FAIL;
				}
				auto* failedProvider = _provider;
				_provider = nullptr;
				_frameGenerationDisabled = true;
				if (_callbacks.recordFailure) {
					const auto reason = render::temporal::FormatProviderFailure(
						"Frame-generation presentation creation", providerResult);
					_callbacks.recordFailure(reason.c_str());
				}
				const auto destroy = failedProvider->DestroyAfterDrain();
				const auto deactivate = destroy.Succeeded()
					? failedProvider->SetPresentationActive(false)
					: destroy;
				if (!deactivate.Succeeded()) {
					_quarantined = true;
					return E_FAIL;
				}
				_providerPresentationActive = false;
			}
		}
		if (!_provider) {
			winrt::com_ptr<IDXGISwapChain1> plainSwapChain;
			result = factory->CreateSwapChainForHwnd(
				_queue.get(), a_desc.OutputWindow, &_innerDesc, nullptr, nullptr,
				plainSwapChain.put());
			if (SUCCEEDED(result)) {
				result =
					plainSwapChain->QueryInterface(IID_PPV_ARGS(swapChain.put()));
			}
		}
		if (SUCCEEDED(result) && swapChain) {
			_swapChain = std::move(swapChain);
			if (!a_desc.Windowed) {
				result = _swapChain->SetFullscreenState(TRUE, nullptr);
			}
		}
		if (SUCCEEDED(result) && _swapChain) {
			_frameIndex = _swapChain->GetCurrentBackBufferIndex();
			_proxyDesc = swap_chain_facade::BuildDescription(a_desc, _innerDesc);
		}
		return result;
	}

	HRESULT DX12SwapChain::CreateInteropFence()
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
		return _fenceEvent ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	}

	HRESULT DX12SwapChain::CreateDisplayResources(
		UINT a_width, UINT a_height,
		std::unique_ptr<SharedD3D11D3D12Texture>& a_proxy,
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2>& a_hudless)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		auto proxy = SharedD3D11D3D12Texture::Create(
			_device11.get(), _device12.get(), desc, "Upscaling/FrameGenerationProxy");
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> hudless;
		if (_provider) {
			hudless = {
				SharedD3D11D3D12Texture::Create(
					_device11.get(), _device12.get(), desc,
					"Upscaling/HUDLess[0]"),
				SharedD3D11D3D12Texture::Create(
					_device11.get(), _device12.get(), desc,
					"Upscaling/HUDLess[1]")
			};
		}
		if (!proxy || (_provider && (!hudless[0] || !hudless[1]))) {
			return E_OUTOFMEMORY;
		}
		a_proxy = std::move(proxy);
		a_hudless = std::move(hudless);
		return S_OK;
	}

	HRESULT DX12SwapChain::RecreateDisplayResources(UINT a_width, UINT a_height)
	{
		std::unique_ptr<SharedD3D11D3D12Texture> proxy;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> hudless;
		const HRESULT result =
			CreateDisplayResources(a_width, a_height, proxy, hudless);
		if (FAILED(result)) {
			return result;
		}
		_proxyBuffer = std::move(proxy);
		_hudlessBuffers = std::move(hudless);
		if (_streamline) {
			_streamline->NotifyDLSSGDisplayChange(
				a_width, a_height);
		}
		return S_OK;
	}

	HRESULT DX12SwapChain::RecreateFrameGenerationResources(UINT a_width,
		UINT a_height)
	{
		_frameGenerationInputsReady = false;
		_frameGenerationDisabled = false;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS |
		                 D3D11_BIND_RENDER_TARGET;

		if (_provider && (!_hudlessBuffers[0] || !_hudlessBuffers[1])) {
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			for (UINT slot = 0; slot < _hudlessBuffers.size(); ++slot) {
				_hudlessBuffers[slot] =
					SharedD3D11D3D12Texture::Create(
						_device11.get(), _device12.get(), desc,
						std::format("Upscaling/HUDLess[{}]", slot));
			}
			if (!_hudlessBuffers[0] || !_hudlessBuffers[1]) {
				_hudlessBuffers = {};
				_frameGenerationDisabled = true;
				return E_OUTOFMEMORY;
			}
		}

		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> depths;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> motions;
		for (UINT slot = 0; slot < depths.size(); ++slot) {
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			depths[slot] = SharedD3D11D3D12Texture::Create(
				_device11.get(), _device12.get(), desc,
				std::format("Upscaling/FrameGenerationDepth[{}]", slot));
			desc.Format = DXGI_FORMAT_R16G16_FLOAT;
			motions[slot] = SharedD3D11D3D12Texture::Create(
				_device11.get(), _device12.get(), desc,
				std::format("Upscaling/FrameGenerationMotion[{}]", slot));
		}
		if (!depths[0] || !depths[1] || !motions[0] || !motions[1]) {
			_depthBuffers = {};
			_motionBuffers = {};
			_frameGenerationDisabled = true;
			L->error(
				"Frame-generation bridge inputs were not recreated after the "
				"native resize");
			return E_OUTOFMEMORY;
		}

		_depthBuffers = std::move(depths);
		_motionBuffers = std::move(motions);
		++_inputResourceGeneration;
		const auto providerResult =
			_provider ? _provider->CreateDisplayResources(a_width, a_height,
							DXGI_FORMAT_R8G8B8A8_UNORM,
							_innerDesc.BufferCount) :
						render::temporal::ProviderResult{};
		if (!providerResult.Succeeded()) {
			_frameGenerationDisabled = true;
			L->error(
				"Frame-generation provider resources were not recreated; "
				"real-frame presentation remains active");
			return S_FALSE;
		}
		return S_OK;
	}

	HRESULT DX12SwapChain::RestoreFrameGenerationProvider(UINT a_width,
		UINT a_height)
	{
		_frameGenerationInputsReady = false;
		if (!_provider) {
			return E_FAIL;
		}
		const render::temporal::ProviderDisplayDescription description{
			.width = a_width,
			.height = a_height,
			.format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.bufferCount = _innerDesc.BufferCount
		};
		const auto result = render::temporal::RestoreProviderAfterResize(
			*_provider, S_OK, description, description);
		if (!result.Succeeded()) {
			DisableFrameGeneration(result.message.empty() ? "Frame-generation provider restoration failed" : result.message.c_str());
			return S_FALSE;
		}
		_frameGenerationDisabled = false;
		return S_OK;
	}

	HRESULT DX12SwapChain::RefreshBackBuffers()
	{
		winrt::com_ptr<ID3D12Resource> refreshed[2];
		for (UINT index = 0; index < 2; ++index) {
			const HRESULT result =
				_swapChain->GetBuffer(index, IID_PPV_ARGS(refreshed[index].put()));
			if (FAILED(result)) {
				return result;
			}
		}
		for (UINT index = 0; index < 2; ++index) {
			_backBuffers[index] = std::move(refreshed[index]);
		}
		_frameIndex = _swapChain->GetCurrentBackBufferIndex();
		return S_OK;
	}

	IDXGISwapChain* DX12SwapChain::GetProxy() const noexcept
	{
		return _proxy.get();
	}

	IDXGISwapChain* DX12SwapChain::AcquireProxy() const noexcept
	{
		auto* proxy = _proxy.get();
		if (proxy) {
			proxy->AddRef();
		}
		return proxy;
	}

	bool DX12SwapChain::Owns(IDXGISwapChain* a_swapChain) const noexcept
	{
		return _proxy && a_swapChain == _proxy.get();
	}

	bool DX12SwapChain::IsReady() const noexcept
	{
		return !_quarantined && _published && _swapChain && _proxyBuffer &&
		       _context11 && _queue &&
		       _fence11 && _fence12 && _inputRetirementFence11 &&
		       _inputRetirementFence12 && _fenceEvent &&
		       _srSubmissions[0].allocator && _srSubmissions[1].allocator &&
		       _srSubmissions[0].commandList &&
		       _srSubmissions[1].commandList &&
		       _presentSubmissions[0].allocator &&
		       _presentSubmissions[1].allocator &&
		       _presentSubmissions[0].commandList &&
		       _presentSubmissions[1].commandList &&
		       _backBuffers[0] && _backBuffers[1];
	}

	bool DX12SwapChain::IsBridgeReady() const noexcept
	{
		return IsReady();
	}

	bool DX12SwapChain::IsFrameGenerationReady() const noexcept
	{
		return IsReady() && !_frameGenerationDisabled && _provider &&
		       _provider->IsReady() && _hudlessBuffers[0] && _hudlessBuffers[1] &&
		       _depthBuffers[0] && _depthBuffers[1] && _motionBuffers[0] &&
		       _motionBuffers[1];
	}

	render::temporal::IFrameGenerationProvider*
	DX12SwapChain::GetPresentationProvider() const noexcept
	{
		return _provider;
	}

	render::temporal::ProviderResult
	DX12SwapChain::CanReplacePresentationProvider(
		render::temporal::IFrameGenerationProvider* a_provider) const noexcept
	{
		if (_quarantined || !_published || !_adapter || !_device12 || !_queue ||
			!_proxy || !_proxyBuffer) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.message =
					"The private D3D12 presentation backend is unavailable.",
				.failureDomain =
					render::temporal::FailureDomain::kPresentation
			};
		}
		if (_presentPrepared || _preparedTransaction ||
			_presentSubmissionMayBeInFlight) {
			return {
				.code = render::temporal::ProviderResultCode::kSkipped,
				.message =
					"A retryable presentation transaction is still pending.",
				.failureDomain =
					render::temporal::FailureDomain::kPresentation
			};
		}
		if (a_provider && !a_provider->IsAvailable()) {
			return {
				.code = render::temporal::ProviderResultCode::kUnavailable,
				.message =
					"The requested frame-generation provider is unavailable.",
				.failureDomain =
					render::temporal::FailureDomain::kFrameGeneration
			};
		}
		return {
			.code = render::temporal::ProviderResultCode::kSuccess
		};
	}

	render::temporal::ProviderResult
	DX12SwapChain::RetireCurrentPresentationProvider()
	{
		if (!_provider) {
			const auto start = std::chrono::steady_clock::now();
			render::temporal::ProviderResult result{
				.code = SUCCEEDED(Drain())
					? render::temporal::ProviderResultCode::kSuccess
					: render::temporal::ProviderResultCode::kFailure,
				.message = "The plain presentation queue did not drain.",
				.failureDomain =
					render::temporal::FailureDomain::kPresentation,
				.globalDrainAttempted = true,
				.globalDrainCompleted = false
			};
			result.globalDrainCompleted = result.Succeeded();
			result.globalDrainCpuMicroseconds =
				static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - start)
						.count());
			RecordGlobalDrain("presentation switch", result);
			if (result.Succeeded()) {
				ReleasePrivatePresentationResources();
			}
			return result;
		}
		const auto result =
			render::temporal::RetirePresentationProvider(
				*_provider, [this]() { return SUCCEEDED(Drain()); },
				[this]() { ReleasePrivatePresentationResources(); });
		RecordGlobalDrain("presentation switch", result);
		if (result.Succeeded()) {
			_providerPresentationActive = false;
		}
		return result;
	}

	void DX12SwapChain::ReleasePrivatePresentationResources() noexcept
	{
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		_frameGenerationInputsReady = false;
		_providerGenerationEnabled = false;
		for (auto& motion : _motionBuffers) {
			motion.reset();
		}
		for (auto& depth : _depthBuffers) {
			depth.reset();
		}
		for (auto& hudless : _hudlessBuffers) {
			hudless.reset();
		}
		for (auto& backBuffer : _backBuffers) {
			backBuffer = nullptr;
		}
		if (_proxy) {
			_proxy->ReplaceInner(nullptr);
		}
		_swapChain = nullptr;
		++_inputResourceGeneration;
		render::temporal::ResetPresentationProtocol(
			_allocatorFenceValues, _inputReuseGate, _frameSlot, _presentPrepared,
			_preparedFrameGeneration, _vendorConsumptionPossible,
			_preparedTransaction);
		_presentSubmissionMayBeInFlight = false;
		for (auto& submission : _srSubmissions) {
			submission.completionValue = 0;
		}
		for (auto& submission : _presentSubmissions) {
			submission.completionValue = 0;
		}
		_nextSrSubmission = 0;
		_nextPresentSubmission = 0;
	}

	render::temporal::ProviderResult
	DX12SwapChain::CreateReplacementPresentation(
		render::temporal::IFrameGenerationProvider* a_provider,
		bool a_allowStreamlineFallback)
	{
		auto creationDesc = _creationDesc;
		creationDesc.BufferDesc.Width = _innerDesc.Width;
		creationDesc.BufferDesc.Height = _innerDesc.Height;
		creationDesc.Windowed = _proxyDesc.Windowed;
		const auto outwardDesc = _proxyDesc;
		_provider = a_provider;
		_providerPresentationActive = false;
		render::temporal::ProviderResult providerFailure;
		const HRESULT createResult =
			CreateSwapChain(
				_adapter.get(), creationDesc,
				a_allowStreamlineFallback, &providerFailure);
		_proxyDesc = outwardDesc;
		if (FAILED(createResult)) {
			if (!providerFailure.Succeeded() &&
				providerFailure.code !=
					render::temporal::ProviderResultCode::kFailure) {
				return providerFailure;
			}
			if (!providerFailure.message.empty() ||
				FAILED(providerFailure.hresult) ||
				providerFailure.sdkResult != 0) {
				return providerFailure;
			}
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.hresult = createResult,
				.message =
					"The native private swap chain could not be created.",
				.failureDomain =
					render::temporal::FailureDomain::kPresentation
			};
		}
		const HRESULT refreshResult = RefreshBackBuffers();
		if (FAILED(refreshResult)) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.hresult = refreshResult,
				.message =
					"The replacement private swap-chain buffers are unavailable.",
				.failureDomain =
					render::temporal::FailureDomain::kPresentation
			};
		}
		if (_provider) {
			const HRESULT resourceResult = RecreateFrameGenerationResources(
				_innerDesc.Width, _innerDesc.Height);
			if (resourceResult != S_OK) {
				return {
					.code = render::temporal::ProviderResultCode::kFailure,
					.hresult =
						FAILED(resourceResult) ? resourceResult : E_FAIL,
					.message =
						"The replacement frame-generation resources are "
						"unavailable.",
					.failureDomain =
						render::temporal::FailureDomain::kFrameGeneration
				};
			}
		} else {
			_frameGenerationDisabled = false;
		}
		if (_proxy) {
			_proxy->ReplaceInner(_swapChain.get());
		}
		_creationDesc.BufferDesc.Width = _innerDesc.Width;
		_creationDesc.BufferDesc.Height = _innerDesc.Height;
		_creationDesc.Windowed = _proxyDesc.Windowed;
		ClearSharedBuffers();
		return {
			.code = render::temporal::ProviderResultCode::kSuccess
		};
	}

	render::temporal::ProviderResult
	DX12SwapChain::ReplacePresentationProvider(
		render::temporal::IFrameGenerationProvider* a_provider)
	{
		const auto preflight = CanReplacePresentationProvider(a_provider);
		if (!preflight.Succeeded() || a_provider == _provider) {
			return preflight;
		}

		const auto retirement = RetireCurrentPresentationProvider();
		if (!retirement.Succeeded()) {
			QuarantineTransport(
				"The current presentation provider could not be retired.");
			return retirement;
		}

		_provider = nullptr;
		const auto targetResult =
			CreateReplacementPresentation(a_provider);
		if (targetResult.Succeeded()) {
			L->info(
				"Replaced the private presentation chain with {}",
				a_provider ? a_provider->Name() : "plain D3D12");
			return {
				.code = render::temporal::ProviderResultCode::kSuccess,
				.globalDrainAttempted = retirement.globalDrainAttempted,
				.globalDrainCompleted = retirement.globalDrainCompleted,
				.globalDrainCpuMicroseconds =
					retirement.globalDrainCpuMicroseconds
			};
		}

		L->error("{}", render::temporal::FormatProviderFailure(
						  "Create requested presentation chain", targetResult));
		auto targetFailure = targetResult;
		if (_provider && _providerPresentationActive) {
			const auto cleanup = RetireCurrentPresentationProvider();
			if (!cleanup.Succeeded()) {
				QuarantineTransport(
					"The failed target presentation provider could not be "
					"retired.");
				return cleanup;
			}
		}
		ReleasePrivatePresentationResources();
		_provider = nullptr;
		const auto recovery =
			CreateReplacementPresentation(nullptr, true);
		if (!recovery.Succeeded()) {
			L->error("{}", render::temporal::FormatProviderFailure(
							  "Recover plain D3D12 presentation", recovery));
			targetFailure.hresult = recovery.hresult;
			targetFailure.sdkResult = recovery.sdkResult;
			targetFailure.message =
				"The requested presentation chain failed and plain D3D12 "
				"presentation could not be recovered.";
			targetFailure.failureDomain =
				recovery.failureDomain ==
						render::temporal::FailureDomain::kNone
					? render::temporal::FailureDomain::kPresentation
					: recovery.failureDomain;
			QuarantineTransport(targetFailure.message);
			return targetFailure;
		}
		L->error(
			"Frame-generation chain replacement failed; recovered plain D3D12 "
			"presentation");
		return targetFailure;
	}

	UINT DX12SwapChain::GetWidth() const noexcept { return _innerDesc.Width; }

	UINT DX12SwapChain::GetHeight() const noexcept { return _innerDesc.Height; }

	UINT DX12SwapChain::GetFrameSlot() const noexcept { return _frameSlot; }

	SharedD3D11D3D12Texture* DX12SwapChain::GetHudlessTexture() const noexcept
	{
		return _frameSlot < _hudlessBuffers.size() ? _hudlessBuffers[_frameSlot].get() : nullptr;
	}

	SharedD3D11D3D12Texture* DX12SwapChain::GetProxyTexture() const noexcept
	{
		return _proxyBuffer.get();
	}

	SharedD3D11D3D12Texture* DX12SwapChain::GetDepthTexture() const noexcept
	{
		return _frameSlot < _depthBuffers.size()
			? _depthBuffers[_frameSlot].get()
			: nullptr;
	}

	SharedD3D11D3D12Texture* DX12SwapChain::GetMotionTexture() const noexcept
	{
		return _frameSlot < _motionBuffers.size()
			? _motionBuffers[_frameSlot].get()
			: nullptr;
	}

	ID3D12GraphicsCommandList* DX12SwapChain::GetCommandList() const noexcept
	{
		return _presentSubmissions[_nextPresentSubmission].commandList.get();
	}

	ID3D12Device* DX12SwapChain::GetD3D12Device() const noexcept
	{
		return _device12.get();
	}

	IDXGISwapChain4* DX12SwapChain::GetInnerSwapChain() const noexcept
	{
		return _swapChain.get();
	}

	ID3D12CommandQueue* DX12SwapChain::GetCommandQueue() const noexcept
	{
		return _queue.get();
	}

	render::temporal::PresentInputRetirementDiagnostics
	DX12SwapChain::GetInputRetirementDiagnostics() const noexcept
	{
		return _retirementDiagnostics.Snapshot();
	}

	void DX12SwapChain::RecordGlobalDrain(
		std::string_view a_reason,
		const render::temporal::ProviderResult& a_result) noexcept
	{
		if (!a_result.globalDrainAttempted) {
			return;
		}
		_retirementDiagnostics.globalDrainAttempts.fetch_add(
			1, std::memory_order_relaxed);
		auto* counter = &_retirementDiagnostics.teardownDrains;
		if (a_reason == "startup") {
			counter = &_retirementDiagnostics.startupDrains;
		} else if (a_reason == "disable") {
			counter = &_retirementDiagnostics.disableDrains;
		} else if (a_reason == "resize") {
			counter = &_retirementDiagnostics.resizeDrains;
		}
		if (a_result.globalDrainCompleted) {
			counter->fetch_add(1, std::memory_order_relaxed);
			_retirementDiagnostics.providerDrains.fetch_add(1,
				std::memory_order_relaxed);
		} else {
			_retirementDiagnostics.globalDrainFailures.fetch_add(
				1, std::memory_order_relaxed);
			_retirementDiagnostics.violations.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void DX12SwapChain::QuarantineTransport(
		std::string_view a_reason) noexcept
	{
		const bool firstFailure = !_quarantined;
		_quarantined = true;
		if (firstFailure) {
			try {
				render::TemporalPipeline::Get().PostFailure(
					render::temporal::FailureDomain::kTransport,
					std::string(a_reason));
			} catch (...) {
			}
		}
	}

	std::unique_ptr<cs::buffer::Texture2D>
	DX12SwapChain::CreateSharedTexture(
		const D3D11_TEXTURE2D_DESC& a_desc,
		std::string_view a_name) const
	{
		if (!IsBridgeReady() || !_device11 || !_device12) {
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

	render::temporal::ProviderResult
	DX12SwapChain::EvaluateD3D12SuperResolution(
		render::temporal::ISuperResolutionProvider& a_provider,
		const render::temporal::SuperResolutionRequest& a_request)
	{
		bool submissionMayBeInFlight = false;
		const auto* recording = std::get_if<render::temporal::D3D11RecordingContext>(
			&a_request.recording);
		const auto alias = [](const render::temporal::GpuView& a_view) {
			const auto* view =
				std::get_if<render::temporal::D3D11GpuView>(&a_view);
			return view && view->resource && view->alias12
				? render::temporal::D3D12GpuView{
					  .resource = view->alias12,
					  .state = view->alias12State }
				: render::temporal::D3D12GpuView{};
		};
		const auto color = alias(a_request.colorInput);
		const auto output = alias(a_request.privateOutput);
		const auto publication = alias(a_request.publicationOutput);
		const auto depth = alias(a_request.depth);
		const auto motion = alias(a_request.motionVectors);
		const auto reactive = alias(a_request.reactiveMask);
		const auto transparency =
			alias(a_request.transparencyCompositionMask);
		if (!IsBridgeReady() || !recording || !recording->context ||
			!color.resource || !output.resource || !depth.resource ||
			!motion.resource || !reactive.resource ||
			!transparency.resource ||
			(a_request.postProcessSharpening &&
				(!publication.resource ||
					publication.resource == output.resource))) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.hresult = E_INVALIDARG,
				.message =
					"D3D12 super resolution requires shared producer resources.",
				.failureDomain = render::temporal::FailureDomain::kTransport
			};
		}

		try {
			SubmissionRecord* submission = nullptr;
			DX::ThrowIfFailed(AcquireSubmission(
				_srSubmissions, _nextSrSubmission, submission));
			const UINT64 d3d11Ready = _nextFenceValue++;
			DX::ThrowIfFailed(_context11->Signal(_fence11.get(), d3d11Ready));
			DX::ThrowIfFailed(_queue->Wait(_fence12.get(), d3d11Ready));
			DX::ThrowIfFailed(submission->allocator->Reset());
			DX::ThrowIfFailed(submission->commandList->Reset(
				submission->allocator.get(), nullptr));
			auto* commandList = submission->commandList.get();
			auto request = a_request;
			request.recording = render::temporal::D3D12RecordingContext{
				.commandList = commandList,
				.queue = _queue.get(),
				.slot = _frameSlot
			};
			request.colorInput = color;
			request.privateOutput = output;
			request.depth = depth;
			request.motionVectors = motion;
			request.reactiveMask = reactive;
			request.transparencyCompositionMask = transparency;
			auto providerResult = a_provider.Record(request);
			if (!providerResult.Succeeded()) {
				DX::ThrowIfFailed(commandList->Close());
				if (providerResult.failureDomain ==
					render::temporal::FailureDomain::kNone) {
					providerResult.failureDomain =
						render::temporal::FailureDomain::kSuperResolution;
				}
				return providerResult;
			}
			if (providerResult.workState !=
				render::temporal::ProviderWorkState::kRecorded) {
				DX::ThrowIfFailed(commandList->Close());
				return {
					.code =
						render::temporal::ProviderResultCode::kFailure,
					.message =
						"The D3D12 super-resolution provider did not report "
						"recorded work.",
					.failureDomain =
						render::temporal::FailureDomain::kSuperResolution
				};
			}
			if (a_request.postProcessSharpening) {
				if (!_rcas.RecordSharpen(commandList,
						submission->postProcessDescriptors,
						output.resource, output.state,
						publication.resource, publication.state,
						a_request.outputWidth, a_request.outputHeight,
						a_request.postProcessSharpness)) {
					DX::ThrowIfFailed(commandList->Close());
					return {
						.code =
							render::temporal::ProviderResultCode::kFailure,
						.message =
							"Native D3D12 RCAS recording failed.",
						.failureDomain =
							render::temporal::FailureDomain::kSuperResolution
					};
				}
				providerResult.publicationOutputReady = true;
			}
			DX::ThrowIfFailed(commandList->Close());
			ID3D12CommandList* lists[]{ commandList };
			_queue->ExecuteCommandLists(1, lists);
			submissionMayBeInFlight = true;
			const UINT64 d3d12Done = _nextFenceValue++;
			DX::ThrowIfFailed(_queue->Signal(_fence12.get(), d3d12Done));
			submission->completionValue = d3d12Done;
			DX::ThrowIfFailed(_context11->Wait(_fence11.get(), d3d12Done));
			providerResult.workState =
				render::temporal::ProviderWorkState::kOutputReady;
			providerResult.outputDependencyEstablished = true;
			return providerResult;
		} catch (const winrt::hresult_error& e) {
			const auto error = static_cast<HRESULT>(e.code());
			_quarantined = true;
			L->error("D3D12 super-resolution bridge failed: {}",
				winrt::to_string(e.message()));
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.hresult = error,
				.message = "D3D12 super-resolution submission failed.",
				.failureDomain = render::temporal::FailureDomain::kTransport,
				.workState = submissionMayBeInFlight
					? render::temporal::ProviderWorkState::kSubmitted
					: render::temporal::ProviderWorkState::kNone
			};
		} catch (const std::exception& e) {
			_quarantined = true;
			L->error("D3D12 super-resolution bridge failed: {}", e.what());
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.message = e.what(),
				.failureDomain = render::temporal::FailureDomain::kTransport,
				.workState = submissionMayBeInFlight
					? render::temporal::ProviderWorkState::kSubmitted
					: render::temporal::ProviderWorkState::kNone
			};
		}
		return {
			.code = render::temporal::ProviderResultCode::kFailure,
			.message = "D3D12 super-resolution submission failed.",
			.failureDomain = render::temporal::FailureDomain::kTransport
		};
	}

	void DX12SwapChain::SetFrameGenerationInputsReady(bool a_ready) noexcept
	{
		_frameGenerationInputsReady = a_ready;
	}

	void DX12SwapChain::SetOutwardD3D11Device(ID3D11Device* a_device) noexcept
	{
		if (a_device) {
			_outwardDevice11.copy_from(a_device);
		}
	}

	void DX12SwapChain::DisableFrameGeneration(const char* a_reason) noexcept
	{
		const bool firstFailure = !_frameGenerationDisabled;
		_frameGenerationDisabled = true;
		_frameGenerationInputsReady = false;
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		render::TemporalPipeline::Get().RequestFrameGenerationReset();
		if (firstFailure && !_quarantined) {
			if (_callbacks.recordFailure) {
				_callbacks.recordFailure(a_reason ? a_reason : "Unknown frame-generation failure.");
			}
		}
		try {
			L->error("Frame generation disabled: {}",
				a_reason ? a_reason : "unknown failure");
		} catch (...) {
		}
	}

	void DX12SwapChain::DisableFrameGeneration(
		std::string_view a_operation,
		const render::temporal::ProviderResult& a_result) noexcept
	{
		const auto reason =
			render::temporal::FormatProviderFailure(a_operation, a_result);
		DisableFrameGeneration(reason.c_str());
	}

	bool DX12SwapChain::AcquireFrameGenerationInputWrite() noexcept
	{
		if (_frameGenerationDisabled || !_provider || !_inputRetirementFence12 ||
			!_inputRetirementFence11 || !_context11 || !_queue) {
			return false;
		}
		const auto completed = _inputRetirementFence12->GetCompletedValue();
		const bool detailed =
			render::TemporalPipeline::Get().DetailedTracingEnabled();
		std::uint64_t waitMicroseconds = 0;
		const auto acquired = _inputReuseGate.Acquire(
			_frameSlot, _inputResourceGeneration,
			static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(_queue.get())),
			completed,
			[&](const render::temporal::PresentInputRetirementToken& a_token) {
				if (!detailed) {
					return _context11->Wait(_inputRetirementFence11.get(), a_token.value);
				}
				auto timing =
					render::TemporalPipeline::Get().MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kAcquirePresentInputs);
				const auto start = std::chrono::steady_clock::now();
				const HRESULT waitResult =
					_context11->Wait(_inputRetirementFence11.get(), a_token.value);
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
		if (render::temporal::ShouldPublishPresentInputAcquireTelemetry(acquired)) {
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
			DisableFrameGeneration(
				render::temporal::PresentInputAcquireFailureMessage(acquired.code));
		}
		return acquired.Succeeded();
	}

	HRESULT DX12SwapChain::AcquireSubmission(
		std::array<SubmissionRecord, 2>& a_records,
		UINT& a_cursor,
		SubmissionRecord*& a_record) noexcept
	{
		a_record = nullptr;
		if (!_fence12 || !_fenceEvent) {
			return E_FAIL;
		}
		const auto completed = _fence12->GetCompletedValue();
		if (completed == UINT64_MAX) {
			QuarantineTransport(
				"The temporal D3D12 submission fence reported device removal.");
			return DXGI_ERROR_DEVICE_REMOVED;
		}
		for (UINT offset = 0; offset < a_records.size(); ++offset) {
			const UINT index =
				(a_cursor + offset) % static_cast<UINT>(a_records.size());
			auto& candidate = a_records[index];
			if (!candidate.completionValue ||
				completed >= candidate.completionValue) {
				a_cursor =
					(index + 1) % static_cast<UINT>(a_records.size());
				a_record = &candidate;
				return S_OK;
			}
		}

		auto& oldest = a_records[a_cursor];
		const HRESULT wait = WaitForSubmission(oldest);
		if (FAILED(wait)) {
			QuarantineTransport(
				"A temporal D3D12 command allocator did not retire safely.");
			return wait;
		}
		a_record = &oldest;
		a_cursor =
			(a_cursor + 1) % static_cast<UINT>(a_records.size());
		return S_OK;
	}

	HRESULT DX12SwapChain::WaitForSubmission(
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
		return WaitForSingleObject(_fenceEvent, INFINITE) == WAIT_OBJECT_0
			? S_OK
			: HRESULT_FROM_WIN32(GetLastError());
	}

	HRESULT DX12SwapChain::WaitForGpu() noexcept
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

	HRESULT DX12SwapChain::Present(UINT a_syncInterval, UINT a_flags) noexcept
	{
		return PresentInternal(a_syncInterval, a_flags, nullptr, false);
	}

	HRESULT
	DX12SwapChain::Present1(UINT a_syncInterval, UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept
	{
		return PresentInternal(a_syncInterval, a_flags, a_parameters, true);
	}

	HRESULT
	DX12SwapChain::PresentInternal(UINT a_syncInterval, UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters,
		bool a_usePresent1) noexcept
	{
		if (a_flags & DXGI_PRESENT_TEST) {
			auto& pipeline = render::TemporalPipeline::Get();
			pipeline.BeginPresentAttempt(a_flags);
			const HRESULT result = InvokeInnerPresent(a_syncInterval, a_flags,
				a_parameters, a_usePresent1);
			pipeline.EndPresentAttempt(a_flags, result);
			if (_preparedTransaction) {
				pipeline.RecordPresentAttempt(_frameSlot, a_flags, result);
			}
			return result;
		}
		try {
			const HRESULT result =
				PresentImpl(a_syncInterval, a_flags, a_parameters, a_usePresent1);
			if (result == DXGI_ERROR_DEVICE_REMOVED ||
				result == DXGI_ERROR_DEVICE_RESET ||
				result == DXGI_ERROR_DEVICE_HUNG) {
				QuarantineTransport(
					"The temporal D3D12 presentation device was lost.");
			}
			if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
				if (_callbacks.clearCapture) {
					_callbacks.clearCapture();
				}
			}
			return result;
		} catch (const winrt::hresult_error& e) {
			if (_callbacks.clearCapture) {
				_callbacks.clearCapture();
			}
			const auto result = static_cast<HRESULT>(e.code());
			QuarantineTransport(
				"Temporal D3D12 presentation command submission failed.");
			DisableFrameGeneration(
				winrt::to_string(e.message()).c_str());
			return result;
		} catch (const std::exception& e) {
			if (_callbacks.clearCapture) {
				_callbacks.clearCapture();
			}
			if (_presentSubmissionMayBeInFlight) {
				QuarantineTransport(
					"Temporal D3D12 presentation work could not be retired.");
			}
			DisableFrameGeneration(e.what());
			return E_FAIL;
		} catch (...) {
			if (_callbacks.clearCapture) {
				_callbacks.clearCapture();
			}
			if (_presentSubmissionMayBeInFlight) {
				QuarantineTransport(
					"Temporal D3D12 presentation work could not be retired.");
			}
			DisableFrameGeneration("unhandled presentation failure");
			return E_FAIL;
		}
	}

	HRESULT
	DX12SwapChain::InvokeInnerPresent(UINT a_syncInterval, UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters,
		bool a_usePresent1) noexcept
	{
		if (!_swapChain) {
			return DXGI_ERROR_INVALID_CALL;
		}
		return a_usePresent1 ? _swapChain->Present1(a_syncInterval, a_flags, a_parameters) : _swapChain->Present(a_syncInterval, a_flags);
	}

	HRESULT DX12SwapChain::PresentImpl(UINT a_syncInterval, UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters,
		bool a_usePresent1)
	{
		if (_quarantined) {
			return DXGI_ERROR_DEVICE_REMOVED;
		}
		if (!IsReady()) {
			auto& pipeline = render::TemporalPipeline::Get();
			pipeline.BeginPresentAttempt(a_flags);
			const HRESULT result = InvokeInnerPresent(a_syncInterval, a_flags,
				a_parameters, a_usePresent1);
			pipeline.EndPresentAttempt(a_flags, result);
			if (_streamline &&
				render::temporal::ShouldObservePresentStatus(
					a_flags, result)) {
				_streamline->ObserveDLSSGPresent(
					a_syncInterval);
			}
			return result;
		}

		if (!_presentPrepared) {
			auto& pipeline = render::TemporalPipeline::Get();
			const bool frameGenerationRequested =
				_provider &&
				pipeline.Renderer().ShouldUseFrameGenerationThisFrame();
			auto request = frameGenerationRequested &&
					_callbacks.queryFrameState
				? _callbacks.queryFrameState()
				: render::temporal::FrameGenerationRequest{};
			if (frameGenerationRequested) {
				pipeline.Renderer().CaptureFrameGenerationFinalDebugSnapshot();
			}
			SubmissionRecord* submission = nullptr;
			{
				auto timing =
					pipeline.MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kAllocatorFenceWait);
				const HRESULT acquireResult = AcquireSubmission(
					_presentSubmissions, _nextPresentSubmission, submission);
				if (FAILED(acquireResult)) {
					return acquireResult;
				}
			}
			cs::render::annotation::SetMarker("FG_D3D11ProductionComplete");
			const UINT64 d3d11Ready = _nextFenceValue++;
			DX::ThrowIfFailed(_context11->Signal(_fence11.get(), d3d11Ready));
			DX::ThrowIfFailed(_queue->Wait(_fence12.get(), d3d11Ready));
			DX::ThrowIfFailed(submission->allocator->Reset());
			DX::ThrowIfFailed(submission->commandList->Reset(
				submission->allocator.get(), nullptr));

			auto* commandList = submission->commandList.get();
			{
				auto timing =
					render::TemporalPipeline::Get().MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kCopyRecord);
				cs::render::annotation::ScopedEvent copyScope(commandList,
					"FG_CopyRealFrame");
				const std::array barriersBefore{
					Transition(_proxyBuffer->resource12.get(),
						D3D12_RESOURCE_STATE_COMMON,
						D3D12_RESOURCE_STATE_COPY_SOURCE),
					Transition(_backBuffers[_frameIndex].get(),
						D3D12_RESOURCE_STATE_PRESENT,
						D3D12_RESOURCE_STATE_COPY_DEST)
				};
				commandList->ResourceBarrier(static_cast<UINT>(barriersBefore.size()),
					barriersBefore.data());
				commandList->CopyResource(_backBuffers[_frameIndex].get(),
					_proxyBuffer->resource12.get());
				const std::array barriersAfter{
					Transition(_proxyBuffer->resource12.get(),
						D3D12_RESOURCE_STATE_COPY_SOURCE,
						D3D12_RESOURCE_STATE_COMMON),
					Transition(_backBuffers[_frameIndex].get(),
						D3D12_RESOURCE_STATE_COPY_DEST,
						D3D12_RESOURCE_STATE_PRESENT)
				};
				commandList->ResourceBarrier(static_cast<UINT>(barriersAfter.size()),
					barriersAfter.data());
			}

			bool requested = frameGenerationRequested &&
				_frameGenerationInputsReady && !_frameGenerationDisabled &&
				request.enabled;
			if (_provider && requested) {
				const auto validation =
					_provider->ValidateConfiguration(
						request.configuration);
				if (validation.code ==
					render::temporal::ProviderResultCode::kSkipped) {
					requested = false;
					_vendorConsumptionPossible = false;
				} else if (!validation.Succeeded()) {
					requested = false;
					_vendorConsumptionPossible = false;
					DisableFrameGeneration(
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
			bool frameGenerationPrepared = false;
			if (_provider && requested) {
				cs::render::annotation::ScopedEvent prepareScope(commandList,
					"FG_ConfigurePrepare");
				request.recording =
					render::temporal::D3D12RecordingContext{ .commandList = commandList,
						.queue = _queue.get(),
						.slot = _frameSlot };
				request.depth = render::temporal::D3D12GpuView{
							.resource = _depthBuffers[_frameSlot]->resource12.get(),
					.state = D3D12_RESOURCE_STATE_COMMON
				};
				request.motionVectors = render::temporal::D3D12GpuView{
							.resource = _motionBuffers[_frameSlot]->resource12.get(),
					.state = D3D12_RESOURCE_STATE_COMMON
				};
				request.hudlessColor = render::temporal::D3D12GpuView{
					.resource = _hudlessBuffers[_frameSlot]->resource12.get(),
					.state = D3D12_RESOURCE_STATE_COMMON
				};
				request.finalColor = render::temporal::D3D12GpuView{
					.resource = _proxyBuffer->resource12.get(),
					.state = D3D12_RESOURCE_STATE_COMMON
				};
				request.outputWidth = _innerDesc.Width;
				request.outputHeight = _innerDesc.Height;
				request.enabled = requested;
				request.uiMode = render::temporal::UiCompositionMode::kHudlessAndFinal;
				pipeline.RecordFrameGenerationFrameTimeInput(
					request.frameTimeMilliseconds);
				const auto preparation = [&] {
					auto timing = pipeline.MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kPrepareFrame);
					return render::temporal::PrepareFrameSafely(*_provider, request);
				}();
				const auto& prepareResult = preparation.prepare;
				frameGenerationPrepared = preparation.prepared;
				_providerGenerationEnabled = frameGenerationPrepared;
				if (!frameGenerationPrepared) {
					_vendorConsumptionPossible = false;
					DisableFrameGeneration("Prepare frame", prepareResult);
					if (!preparation.safeToPresent) {
						L->error("{}", render::temporal::FormatProviderFailure(
										   "Cancel frame", preparation.cancel));
						DX::ThrowIfFailed(commandList->Close());
						_presentPrepared = false;
						_preparedFrameGeneration = false;
						_vendorConsumptionPossible = false;
						_preparedTransaction = false;
						QuarantineTransport(
							"Frame-generation cancellation could not prove that "
							"vendor input references were cleared.");
						return E_FAIL;
					}
				}
			} else if (_provider && _providerGenerationEnabled) {
				const auto disable = _provider->SetGenerationEnabled(false);
				if (!disable.Succeeded()) {
					DisableFrameGeneration("Disable frame generation", disable);
					DX::ThrowIfFailed(commandList->Close());
					QuarantineTransport(
						"Frame-generation disable failed before plain "
						"presentation.");
					return E_FAIL;
				}
				_providerGenerationEnabled = false;
			}

			DX::ThrowIfFailed(commandList->Close());
			ID3D12CommandList* lists[] = { commandList };
			_queue->ExecuteCommandLists(1, lists);
			_presentSubmissionMayBeInFlight = true;
			const UINT64 d3d12Done = _nextFenceValue++;
			DX::ThrowIfFailed(_queue->Signal(_fence12.get(), d3d12Done));
			submission->completionValue = d3d12Done;
			_allocatorFenceValues[_frameSlot] = d3d12Done;
			const HRESULT producerWait = _context11->Wait(_fence11.get(), d3d12Done);
			DX::ThrowIfFailed(producerWait);
			_presentSubmissionMayBeInFlight = false;
			_preparedFrameGeneration =
				requested && frameGenerationPrepared && !_frameGenerationDisabled;
			if (_preparedTransaction) {
				pipeline.SetFrameGenerationPrepared(_frameSlot, _preparedFrameGeneration);
			}
			_presentPrepared = true;
		}

		cs::render::annotation::SetMarker("Upscaling/FrameGeneration/Present");
		auto& pipeline = render::TemporalPipeline::Get();
		pipeline.BeginPresentAttempt(a_flags);
		const HRESULT presentResult = [&] {
			auto timing = pipeline.MeasureFrameGenerationCpuPhase(
				render::FrameGenerationCpuPhase::kSdkPresent);
			return InvokeInnerPresent(a_syncInterval, a_flags, a_parameters,
				a_usePresent1);
		}();
		pipeline.EndPresentAttempt(a_flags, presentResult);
		if (_streamline &&
			render::temporal::ShouldObservePresentStatus(
				a_flags, presentResult)) {
			_streamline->ObserveDLSSGPresent(a_syncInterval);
		}
		render::temporal::PresentStatusCollection status;
		if (_provider &&
			(_vendorConsumptionPossible || _providerGenerationEnabled) &&
			render::temporal::ShouldObservePresentStatus(a_flags, presentResult)) {
			auto timing = pipeline.MeasureFrameGenerationCpuPhase(
				render::FrameGenerationCpuPhase::kCollectPresentStatus);
			status = render::temporal::CollectAcceptedPresentStatus(*_provider, a_flags,
				presentResult);
		}
		if (status.observed) {
			pipeline.RecordGeneratedFrames(status.generatedFrames);
			pipeline.RecordPresentedFrames(status.presentedFrames);
		}
		const bool disableProviderAfterRetirement =
			status.observed && !status.result.Succeeded();
		std::optional<render::temporal::PresentInputRetirementToken>
			retirementToken;
		if (_vendorConsumptionPossible &&
			presentResult != DXGI_ERROR_WAS_STILL_DRAWING) {
			HRESULT retirementResult = S_OK;
			const char* retirementOperation = "join provider completion";
			std::optional<render::temporal::GpuCompletionDependency> dependency;
			if (SUCCEEDED(presentResult)) {
				dependency =
					_provider->ConsumePresentInputCompletionDependency();
				retirementResult = dependency
					? render::temporal::JoinPresentInputCompletion(
						  _queue.get(), *dependency)
					: E_FAIL;
			}
			render::temporal::PresentInputRetirementToken token{
				.value = _nextInputRetirementValue++,
				.realFrame = _preparedRealFrame,
				.resourceGeneration = _inputResourceGeneration,
				.queueIdentity = static_cast<std::uint64_t>(
					reinterpret_cast<std::uintptr_t>(_queue.get()))
			};
			if (SUCCEEDED(retirementResult) && SUCCEEDED(presentResult)) {
				retirementOperation = "signal shared retirement fence";
				retirementResult =
					_queue->Signal(_inputRetirementFence12.get(), token.value);
				if (SUCCEEDED(retirementResult)) {
					retirementToken = token;
					_retirementDiagnostics.signals.fetch_add(
						1, std::memory_order_relaxed);
				}
			}
			if (FAILED(retirementResult)) {
				L->error(
					"{} input retirement failed to {}: HRESULT {:#010x}, "
					"dependency={}, queue-ordered={}, vendor fence value={}, "
					"retirement value={}",
					_provider->Name(), retirementOperation,
					static_cast<std::uint32_t>(retirementResult),
					dependency.has_value(), dependency && dependency->orderedQueue,
					dependency ? dependency->value : 0, token.value);
				_retirementDiagnostics.signalFailures.fetch_add(
					1, std::memory_order_relaxed);
				_retirementDiagnostics.violations.fetch_add(
					1, std::memory_order_relaxed);
				QuarantineTransport(
					"Frame-generation input retirement dependency failed.");
				DisableFrameGeneration(
					"Frame-generation input retirement dependency failed");
				return retirementResult;
			}
		}
		if (disableProviderAfterRetirement) {
			const HRESULT drainResult = Drain();
			if (FAILED(drainResult)) {
				QuarantineTransport(
					"Frame-generation status failure could not drain safely.");
				DisableFrameGeneration(
					"Frame-generation status failure could not drain safely");
				return drainResult;
			}
			const auto disableResult =
				_provider->SetGenerationEnabled(false);
			if (!disableResult.Succeeded()) {
				QuarantineTransport(
					"Frame-generation provider disable failed after Present.");
				L->error("{}", render::temporal::FormatProviderFailure(
								   "Disable after present failure", disableResult));
				DisableFrameGeneration(
					"Disable after present failure", disableResult);
				return E_FAIL;
			}
			_providerGenerationEnabled = false;
			if (_preparedTransaction) {
				pipeline.SetFrameGenerationPrepared(_frameSlot, false);
			}
		}
		if (_preparedTransaction) {
			pipeline.RecordPresentAttempt(_frameSlot, a_flags, presentResult);
		}
		if (disableProviderAfterRetirement) {
			DisableFrameGeneration("Collect present status", status.result);
		}
		if (presentResult == DXGI_ERROR_WAS_STILL_DRAWING) {
			return presentResult;
		}
		if (FAILED(presentResult) && _vendorConsumptionPossible) {
			_retirementDiagnostics.violations.fetch_add(
				1, std::memory_order_relaxed);
			QuarantineTransport(
				"Present failed after vendor input consumption became possible.");
			DisableFrameGeneration(
				"Present failed after vendor input consumption became possible");
			return presentResult;
		}
		_inputReuseGate.MarkSubmitted(_frameSlot, retirementToken);
		_presentPrepared = false;
		_preparedFrameGeneration = false;
		_vendorConsumptionPossible = false;
		_preparedTransaction = false;
		_preparedRealFrame = 0;
		_frameGenerationInputsReady = false;
		ClearSharedBuffers(false);
		if (SUCCEEDED(presentResult)) {
			_frameSlot =
				(_frameSlot + 1) %
				static_cast<UINT>(_presentSubmissions.size());
			_frameIndex = _swapChain->GetCurrentBackBufferIndex();
		} else {
			render::TemporalPipeline::Get().RequestFrameGenerationReset();
		}
		return presentResult;
	}

	void DX12SwapChain::ClearSharedBuffers(
		bool a_clearFrameGenerationInputs) noexcept
	{
		if (!_context11) {
			return;
		}
		const float clear[4]{};
		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/FrameGeneration/ClearSharedBuffers");
		if (_proxyBuffer && _proxyBuffer->rtv11) {
			_context11->ClearRenderTargetView(_proxyBuffer->rtv11.get(), clear);
		}
		if (a_clearFrameGenerationInputs) {
			for (auto& hudless : _hudlessBuffers) {
				if (hudless && hudless->rtv11) {
					_context11->ClearRenderTargetView(hudless->rtv11.get(), clear);
				}
			}
			for (auto& depth : _depthBuffers) {
				if (depth && depth->rtv11) {
					_context11->ClearRenderTargetView(depth->rtv11.get(), clear);
				}
			}
			for (auto& motion : _motionBuffers) {
				if (motion && motion->rtv11) {
					_context11->ClearRenderTargetView(motion->rtv11.get(), clear);
				}
			}
		}
	}

	HRESULT DX12SwapChain::GetBuffer(UINT a_buffer, REFIID a_iid,
		void** a_surface) noexcept
	{
		if (!a_surface) {
			return E_POINTER;
		}
		*a_surface = nullptr;
		if (a_buffer != 0 || !_proxyBuffer || !_proxyBuffer->texture11) {
			return DXGI_ERROR_INVALID_CALL;
		}
		return _proxyBuffer->texture11->QueryInterface(a_iid, a_surface);
	}

	HRESULT DX12SwapChain::GetDevice(REFIID a_iid, void** a_device) noexcept
	{
		if (!a_device) {
			return E_POINTER;
		}
		*a_device = nullptr;
		return _outwardDevice11 ? _outwardDevice11->QueryInterface(a_iid, a_device) : E_NOINTERFACE;
	}

	HRESULT DX12SwapChain::SetFullscreenState(BOOL a_fullscreen,
		IDXGIOutput* a_target) noexcept
	{
		if (!_swapChain) {
			return DXGI_ERROR_INVALID_CALL;
		}
		const HRESULT result =
			_swapChain->SetFullscreenState(a_fullscreen, a_target);
		if (SUCCEEDED(result)) {
			_proxyDesc.Windowed = !a_fullscreen;
			_creationDesc.Windowed = !a_fullscreen;
		}
		return result;
	}

	HRESULT DX12SwapChain::GetFullscreenState(BOOL* a_fullscreen,
		IDXGIOutput** a_target) noexcept
	{
		if (!a_fullscreen) {
			if (a_target) {
				*a_target = nullptr;
			}
			return E_POINTER;
		}
		if (!_swapChain) {
			*a_fullscreen = !_proxyDesc.Windowed;
			if (a_target) {
				*a_target = nullptr;
			}
			return S_OK;
		}
		const HRESULT result =
			_swapChain->GetFullscreenState(a_fullscreen, a_target);
		if (SUCCEEDED(result)) {
			_proxyDesc.Windowed = !*a_fullscreen;
			_creationDesc.Windowed = !*a_fullscreen;
		}
		return result;
	}

	HRESULT DX12SwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc = _proxyDesc;
		return S_OK;
	}

	HRESULT DX12SwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc = swap_chain_facade::BuildDescription1(_proxyDesc);
		return S_OK;
	}

	HRESULT DX12SwapChain::GetFullscreenDesc(
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc = swap_chain_facade::BuildFullscreenDescription(_proxyDesc);
		return S_OK;
	}

	HRESULT DX12SwapChain::GetHwnd(HWND* a_window) noexcept
	{
		if (!a_window) {
			return E_POINTER;
		}
		*a_window = _proxyDesc.OutputWindow;
		return S_OK;
	}

	UINT DX12SwapChain::GetCurrentBackBufferIndex() noexcept { return 0; }

	HRESULT
	DX12SwapChain::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE a_colorSpace,
		UINT* a_support) noexcept
	{
		if (!a_support) {
			return E_POINTER;
		}
		*a_support = 0;
		if (a_colorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
			return S_OK;
		}
		return _swapChain ? _swapChain->CheckColorSpaceSupport(a_colorSpace, a_support) : DXGI_ERROR_INVALID_CALL;
	}

	HRESULT
	DX12SwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE a_colorSpace) noexcept
	{
		if (a_colorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		return _swapChain ? _swapChain->SetColorSpace1(a_colorSpace) : DXGI_ERROR_INVALID_CALL;
	}

	HRESULT DX12SwapChain::SetHDRMetaData(DXGI_HDR_METADATA_TYPE a_type,
		UINT a_size, void* a_metadata) noexcept
	{
		if (a_type != DXGI_HDR_METADATA_TYPE_NONE) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		if (a_size || a_metadata) {
			return E_INVALIDARG;
		}
		return _swapChain ? _swapChain->SetHDRMetaData(a_type, 0, nullptr) : DXGI_ERROR_INVALID_CALL;
	}

	HRESULT DX12SwapChain::ResizeTarget(const DXGI_MODE_DESC* a_target) noexcept
	{
		if (!a_target) {
			return E_INVALIDARG;
		}
		if (!_swapChain) {
			return DXGI_ERROR_INVALID_CALL;
		}
		const HRESULT result = _swapChain->ResizeTarget(a_target);
		if (SUCCEEDED(result)) {
			_proxyDesc.BufferDesc = *a_target;
			_creationDesc.BufferDesc = *a_target;
		}
		return result;
	}

	HRESULT DX12SwapChain::ResizeBuffers(UINT a_bufferCount, UINT a_width,
		UINT a_height, DXGI_FORMAT a_format,
		UINT a_flags) noexcept
	{
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		try {
			return ResizeBuffersImpl(a_bufferCount, a_width, a_height, a_format,
				a_flags);
		} catch (const std::exception& e) {
			DisableFrameGeneration(e.what());
			return E_FAIL;
		} catch (...) {
			DisableFrameGeneration("unhandled resize failure");
			return E_FAIL;
		}
	}

	HRESULT
	DX12SwapChain::ResizeBuffers1(UINT a_bufferCount, UINT a_width, UINT a_height,
		DXGI_FORMAT a_format, UINT a_flags,
		const UINT* a_creationNodeMask,
		IUnknown* const* a_presentQueue) noexcept
	{
		if (a_creationNodeMask || a_presentQueue) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		return ResizeBuffers(a_bufferCount, a_width, a_height, a_format, a_flags);
	}

	HRESULT DX12SwapChain::ResizeBuffersImpl(UINT a_bufferCount, UINT a_width,
		UINT a_height, DXGI_FORMAT a_format,
		UINT a_flags)
	{
		if (!_swapChain) {
			return DXGI_ERROR_INVALID_CALL;
		}
		if (!swap_chain_facade::SupportsResizeBufferCount(a_bufferCount,
				_proxyDesc)) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		constexpr UINT effectiveCount = 2;
		if (a_format != DXGI_FORMAT_UNKNOWN &&
			a_format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			return DXGI_ERROR_UNSUPPORTED;
		}

		UINT targetWidth = a_width;
		UINT targetHeight = a_height;
		if (!targetWidth || !targetHeight) {
			RECT client{};
			if (!GetClientRect(_proxyDesc.OutputWindow, &client)) {
				return HRESULT_FROM_WIN32(GetLastError());
			}
			if (!targetWidth) {
				targetWidth =
					static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
			}
			if (!targetHeight) {
				targetHeight =
					static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));
			}
		}

		const UINT oldWidth = _innerDesc.Width;
		const UINT oldHeight = _innerDesc.Height;
		std::unique_ptr<SharedD3D11D3D12Texture> pendingProxy;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> pendingHudless;
		if (targetWidth != oldWidth || targetHeight != oldHeight) {
			const HRESULT resourceResult = CreateDisplayResources(
				targetWidth, targetHeight, pendingProxy, pendingHudless);
			if (FAILED(resourceResult)) {
				return resourceResult;
			}
		}

		if (_provider) {
			const auto releaseResult =
				render::temporal::QuiesceDrainAndRelease(*_provider, [&]() {
					return SUCCEEDED(Drain());
				});
			RecordGlobalDrain("resize", releaseResult);
			if (!releaseResult.Succeeded()) {
				QuarantineTransport(
					"Frame-generation display resources could not be retired "
					"for resize.");
				DisableFrameGeneration(releaseResult.message.empty()
						? "Frame-generation display resources could not be released "
						  "for resize"
						: releaseResult.message.c_str());
				return E_FAIL;
			}
			_providerGenerationEnabled = false;
		} else {
			const HRESULT drainResult = Drain();
			if (FAILED(drainResult)) {
				_quarantined = true;
				return drainResult;
			}
		}
		_frameGenerationInputsReady = false;
		render::temporal::ResetPresentationProtocol(
			_allocatorFenceValues, _inputReuseGate, _frameSlot, _presentPrepared,
			_preparedFrameGeneration, _vendorConsumptionPossible,
			_preparedTransaction);
		_presentSubmissionMayBeInFlight = false;
		for (auto& submission : _srSubmissions) {
			submission.completionValue = 0;
		}
		for (auto& submission : _presentSubmissions) {
			submission.completionValue = 0;
		}
		_nextSrSubmission = 0;
		_nextPresentSubmission = 0;
		for (auto& backBuffer : _backBuffers) {
			backBuffer = nullptr;
		}

		const HRESULT resizeResult = _swapChain->ResizeBuffers(
			effectiveCount, targetWidth, targetHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
			swap_chain_facade::PreservePrivateResizeFlags(a_flags, _innerDesc));
		if (FAILED(resizeResult)) {
			const HRESULT refreshResult = RefreshBackBuffers();
			if (FAILED(refreshResult)) {
				_published = false;
			}
			const render::temporal::ProviderDisplayDescription oldDescription{
				.width = oldWidth,
				.height = oldHeight,
				.format = DXGI_FORMAT_R8G8B8A8_UNORM,
				.bufferCount = _innerDesc.BufferCount
			};
			if (_provider) {
				const auto restoration =
					render::temporal::RestoreProviderAndPreserveResizeResult(
						*_provider, resizeResult, oldDescription, std::nullopt);
				if (!restoration.providerResult.Succeeded()) {
					DisableFrameGeneration(
						"Frame-generation provider could not be restored "
						"after rejected resize");
				}
			}
			render::TemporalPipeline::Get().RequestFrameGenerationReset();
			return resizeResult;
		}

		DXGI_SWAP_CHAIN_DESC1 resizedDesc{};
		const HRESULT descResult = _swapChain->GetDesc1(&resizedDesc);
		if (FAILED(descResult)) {
			_published = false;
			DisableFrameGeneration("resized swap-chain description was unavailable");
			return descResult;
		}
		const bool sizeChanged =
			resizedDesc.Width != oldWidth || resizedDesc.Height != oldHeight;
		if (sizeChanged &&
			(!pendingProxy ||
				(_provider && (!pendingHudless[0] || !pendingHudless[1])) ||
				targetWidth != resizedDesc.Width ||
				targetHeight != resizedDesc.Height)) {
			const HRESULT resourceResult = CreateDisplayResources(
				resizedDesc.Width, resizedDesc.Height, pendingProxy, pendingHudless);
			if (FAILED(resourceResult)) {
				_published = false;
				DisableFrameGeneration("resized display resources were unavailable");
				return resourceResult;
			}
		}

		const HRESULT refreshResult = RefreshBackBuffers();
		if (FAILED(refreshResult)) {
			_published = false;
			DisableFrameGeneration("resized back buffers were unavailable");
			return refreshResult;
		}

		if (sizeChanged) {
			_proxyBuffer = std::move(pendingProxy);
			_hudlessBuffers = std::move(pendingHudless);
		}
		render::temporal::CommitResizeBridgeState(
			resizedDesc, _proxyDesc, _innerDesc, _allocatorFenceValues,
			_inputReuseGate, _frameSlot, _presentPrepared, _preparedFrameGeneration,
			_vendorConsumptionPossible, _preparedTransaction);
		if (_streamline) {
			_streamline->NotifyDLSSGDisplayChange(
				_innerDesc.Width, _innerDesc.Height);
		}
		_creationDesc.BufferDesc.Width = _innerDesc.Width;
		_creationDesc.BufferDesc.Height = _innerDesc.Height;
		_creationDesc.BufferDesc.Format = _proxyDesc.BufferDesc.Format;

		const HRESULT providerResult = !_provider
			? S_OK
			: sizeChanged
				? RecreateFrameGenerationResources(
					  _innerDesc.Width, _innerDesc.Height)
				: RestoreFrameGenerationProvider(
					  _innerDesc.Width, _innerDesc.Height);
		if (FAILED(providerResult)) {
			_published = false;
			DisableFrameGeneration(
				"Frame-generation bridge resources could not be "
				"recreated after resize");
			return render::temporal::CompleteNativeResize(resizeResult, providerResult);
		}
		if (providerResult == S_FALSE) {
			DisableFrameGeneration(
				"Frame-generation provider could not be restored after resize");
		}
		ClearSharedBuffers();
		render::TemporalPipeline::Get().AdvanceDisplayGeneration(_innerDesc.Width,
			_innerDesc.Height);
		return render::temporal::CompleteNativeResize(resizeResult, providerResult);
	}

	HRESULT DX12SwapChain::SetPrivateData(REFGUID a_name, UINT a_size,
		const void* a_data) noexcept
	{
		return _swapChain ? _swapChain->SetPrivateData(a_name, a_size, a_data) : E_FAIL;
	}

	HRESULT
	DX12SwapChain::SetPrivateDataInterface(REFGUID a_name,
		const IUnknown* a_unknown) noexcept
	{
		return _swapChain ? _swapChain->SetPrivateDataInterface(a_name, a_unknown) : E_FAIL;
	}

	HRESULT DX12SwapChain::GetPrivateData(REFGUID a_name, UINT* a_size,
		void* a_data) noexcept
	{
		return _swapChain ? _swapChain->GetPrivateData(a_name, a_size, a_data) : E_FAIL;
	}

	HRESULT DX12SwapChain::GetParent(REFIID a_iid, void** a_parent) noexcept
	{
		return _swapChain ? _swapChain->GetParent(a_iid, a_parent) : E_FAIL;
	}

	HRESULT DX12SwapChain::GetContainingOutput(IDXGIOutput** a_output) noexcept
	{
		return _swapChain ? _swapChain->GetContainingOutput(a_output) : E_FAIL;
	}

	HRESULT
	DX12SwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept
	{
		return _swapChain ? _swapChain->GetFrameStatistics(a_stats) : E_FAIL;
	}

	HRESULT DX12SwapChain::GetLastPresentCount(UINT* a_count) noexcept
	{
		return _swapChain ? _swapChain->GetLastPresentCount(a_count) : E_FAIL;
	}
}  // namespace cs::features
