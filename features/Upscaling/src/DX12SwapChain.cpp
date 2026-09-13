#include "DX12SwapChain.h"
#include "DXGISwapChainFacadeContract.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <vector>

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/RendererContext.h"
#include "Render/TemporalPipeline.h"
#include "Render/TemporalRenderer.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.dx12swapchain");

		D3D12_RESOURCE_BARRIER Transition(
			ID3D12Resource* a_resource,
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

		void ReportRetirementLogFailure(
			const char* a_operation,
			const char* a_detail = nullptr) noexcept
		{
			OutputDebugStringA("FG_RETIRE logging failure: ");
			OutputDebugStringA(a_operation ? a_operation : "unknown");
			if (a_detail) {
				OutputDebugStringA(" (");
				OutputDebugStringA(a_detail);
				OutputDebugStringA(")");
			}
			OutputDebugStringA("\n");
		}

		std::string_view SwapChainFacadeEventName(
			SwapChainFacadeEvent a_event) noexcept
		{
			switch (a_event) {
			case SwapChainFacadeEvent::kQuerySwapChain1:
				return "query_idxgiswapchain1";
			case SwapChainFacadeEvent::kQuerySwapChain2:
				return "query_idxgiswapchain2";
			case SwapChainFacadeEvent::kQuerySwapChain3:
				return "query_idxgiswapchain3";
			case SwapChainFacadeEvent::kQuerySwapChain4:
				return "query_idxgiswapchain4";
			case SwapChainFacadeEvent::kPresent1:
				return "present1";
			case SwapChainFacadeEvent::kPresent1MetadataRejected:
				return "present1_metadata_rejected";
			case SwapChainFacadeEvent::kResizeBuffers1:
				return "resizebuffers1";
			case SwapChainFacadeEvent::kResizeBuffers1QueuesRejected:
				return "resizebuffers1_queues_rejected";
			case SwapChainFacadeEvent::kSourceSizeRejected:
				return "source_size_rejected";
			case SwapChainFacadeEvent::kFrameLatencyRejected:
				return "frame_latency_rejected";
			case SwapChainFacadeEvent::kMatrixTransformRejected:
				return "matrix_transform_rejected";
			case SwapChainFacadeEvent::kRotationRejected:
				return "rotation_rejected";
			default:
				return "unknown";
			}
		}

	}

	std::unique_ptr<SharedD3D11D3D12Texture> SharedD3D11D3D12Texture::Create(
		ID3D11Device5* a_device11,
		ID3D12Device* a_device12,
		const D3D11_TEXTURE2D_DESC& a_desc,
		std::string_view a_name)
	{
		if (!a_device11 || !a_device12 || !a_desc.Width || !a_desc.Height) {
			return nullptr;
		}

		auto result = std::make_unique<SharedD3D11D3D12Texture>();
		auto desc = a_desc;
		desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		const auto check = [&](HRESULT a_result, const char* a_step) {
			if (FAILED(a_result)) {
				L->error("Shared texture {} failed at {}: {}x{} format={} bind={:#x} misc={:#x} hr={:#010x}",
					a_name, a_step, desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
					desc.BindFlags, desc.MiscFlags, static_cast<std::uint32_t>(a_result));
			}
			DX::ThrowIfFailed(a_result);
		};
		check(a_device11->CreateTexture2D(&desc, nullptr, result->texture11.put()), "CreateTexture2D");

		winrt::com_ptr<IDXGIResource1> dxgiResource;
		DX::ThrowIfFailed(result->texture11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
			nullptr,
			DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
			nullptr,
			&sharedHandle));
		const HRESULT openResult =
			a_device12->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(result->resource12.put()));
		CloseHandle(sharedHandle);
		check(openResult, "OpenSharedHandle");

		if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
			DX::ThrowIfFailed(
				a_device11->CreateShaderResourceView(result->texture11.get(), nullptr, result->srv11.put()));
		}
		if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
			DX::ThrowIfFailed(
				a_device11->CreateUnorderedAccessView(result->texture11.get(), nullptr, result->uav11.put()));
		}
		if (desc.BindFlags & D3D11_BIND_RENDER_TARGET) {
			DX::ThrowIfFailed(
				a_device11->CreateRenderTargetView(result->texture11.get(), nullptr, result->rtv11.put()));
		}
		cs::render::annotation::SetName(
			result->texture11.get(), std::string(a_name) + ".Texture11");
		cs::render::annotation::SetName(
			result->srv11.get(), std::string(a_name) + ".SRV");
		cs::render::annotation::SetName(
			result->uav11.get(), std::string(a_name) + ".UAV");
		cs::render::annotation::SetName(
			result->rtv11.get(), std::string(a_name) + ".RTV");
		cs::render::annotation::SetName(
			result->resource12.get(), std::string(a_name) + ".Resource12");
		return result;
	}

	DX12SwapChain::~DX12SwapChain()
	{
		(void)Rollback();
	}

	HRESULT DX12SwapChain::Initialize(
		IDXGIAdapter* a_adapter,
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		const DXGI_SWAP_CHAIN_DESC& a_desc,
		render::temporal::IFrameGenerationProvider& a_provider,
		TemporalPresentationCallbacks a_callbacks)
	{
		if (_published || !a_device || !a_context || !a_desc.OutputWindow || !a_desc.Windowed ||
			(a_desc.BufferDesc.Format != DXGI_FORMAT_UNKNOWN &&
				a_desc.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)) {
			return E_INVALIDARG;
		}
		const HRESULT rollbackResult = Rollback();
		if (FAILED(rollbackResult)) {
			return rollbackResult;
		}
		_provider = &a_provider;
		_callbacks = std::move(a_callbacks);
		_proxyDesc = a_desc;

		HRESULT result = CreateDevices(a_adapter, a_device, a_context);
		if (SUCCEEDED(result)) {
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			winrt::com_ptr<IDXGIAdapter> actualAdapter;
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
		if (SUCCEEDED(result)) {
			result = RecreateFrameGenerationResources(_innerDesc.Width, _innerDesc.Height);
		}
		if (SUCCEEDED(result) && _frameGenerationDisabled) {
			result = E_FAIL;
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
		L->info(
			"Published D3D11-facing {} proxy at {}x{} R8G8B8A8_UNORM",
			_provider->Name(),
			_innerDesc.Width,
			_innerDesc.Height);
		return S_OK;
	}

	HRESULT DX12SwapChain::Rollback() noexcept
	{
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		if (_provider) {
			const auto release = render::temporal::QuiesceDrainAndRelease(
				*_provider,
				[&]() {
					if (!_context11 || !_fence11 || !_queue ||
						!_fence12 || !_fenceEvent) {
						return true;
					}
					const UINT64 d3d11Idle = _nextFenceValue++;
					return SUCCEEDED(_context11->Signal(
							   _fence11.get(), d3d11Idle)) &&
						SUCCEEDED(_queue->Wait(
							_fence12.get(), d3d11Idle)) &&
						SUCCEEDED(WaitForGpu());
				});
			RecordGlobalDrain("teardown", release);
			if (!release.Succeeded()) {
				DisableFrameGeneration(
					release.message.empty()
						? "Frame-generation display resources could not be released"
						: release.message.c_str());
				return E_FAIL;
			}
		}
		if (_provider) {
			const auto destroy = _provider->DestroyAfterDrain();
			if (!destroy.Succeeded()) {
				DisableFrameGeneration(
					destroy.message.empty()
						? "Frame-generation provider destruction failed"
						: destroy.message.c_str());
				return E_FAIL;
			}
		}
		if (_fenceEvent) {
			CloseHandle(_fenceEvent);
			_fenceEvent = nullptr;
		}
		if (_proxy) {
			_proxy->DetachOwner();
			_proxy = nullptr;
		}
		_srTransparency.reset();
		_srReactive.reset();
		_srMotion.reset();
		_srDepth.reset();
		_srOutput.reset();
		_srColorInput.reset();
		_motionBuffer.reset();
		_depthBuffer.reset();
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
		for (auto& commandList : _commandLists) {
			commandList = nullptr;
		}
		for (auto& allocator : _allocators) {
			allocator = nullptr;
		}
		_queue = nullptr;
		_device12 = nullptr;
		_outwardDevice11 = nullptr;
		_context11 = nullptr;
		_device11 = nullptr;
		_provider = nullptr;
		_callbacks = {};
		_frameGenerationInputsReady = false;
		_frameGenerationDisabled = false;
		_nextInputRetirementValue = 1;
		_preparedRealFrame = 0;
		_inputResourceGeneration = 0;
		_retirementEpochActive = false;
		_retirementLastResetFrame = UINT64_MAX;
		_facadeEventLogMask.store(0, std::memory_order_relaxed);
		RearmInputRetirementLogBudget();
		render::temporal::ResetPresentationProtocol(
			_allocatorFenceValues,
			_inputReuseGate,
			_frameSlot,
			_presentPrepared,
			_preparedFrameGeneration,
			_vendorConsumptionPossible,
			_preparedTransaction);
		_published = false;
		_bridgeReady = false;
		return S_OK;
	}

	HRESULT DX12SwapChain::CreateDevices(
		IDXGIAdapter* a_adapter,
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
		DX::ThrowIfFailed(D3D12CreateDevice(
			actualAdapter.get(),
			D3D_FEATURE_LEVEL_12_0,
			IID_PPV_ARGS(_device12.put())));
		if (_provider) {
			ID3D12Device* preparedDevice = _device12.detach();
			const auto prepareDeviceResult =
				_provider->PrepareDevice(&preparedDevice);
			_device12.attach(preparedDevice);
			if (!prepareDeviceResult.Succeeded() || !_device12) {
				return prepareDeviceResult.sdkResult
					? static_cast<HRESULT>(prepareDeviceResult.sdkResult)
					: E_FAIL;
			}
		}
		cs::render::annotation::SetName(
			_device12.get(), "Upscaling/FrameGeneration.Device");

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		DX::ThrowIfFailed(_device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(_queue.put())));
		cs::render::annotation::SetName(
			_queue.get(), "Upscaling/FrameGeneration.CommandQueue");
		for (UINT index = 0; index < 2; ++index) {
			DX::ThrowIfFailed(_device12->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(_allocators[index].put())));
			DX::ThrowIfFailed(_device12->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				_allocators[index].get(),
				nullptr,
				IID_PPV_ARGS(_commandLists[index].put())));
			DX::ThrowIfFailed(_commandLists[index]->Close());
			cs::render::annotation::SetName(
				_allocators[index].get(),
				std::format(
					"Upscaling/FrameGenerationAllocator[{}].CommandAllocator",
					index));
			cs::render::annotation::SetName(
				_commandLists[index].get(),
				std::format(
					"Upscaling/FrameGenerationCommandList[{}].CommandList",
					index));
		}
		return S_OK;
	}

	HRESULT DX12SwapChain::CreateSwapChain(
		IDXGIAdapter* a_adapter,
		const DXGI_SWAP_CHAIN_DESC& a_desc)
	{
		if (!a_adapter || !_provider) {
			return E_INVALIDARG;
		}
		winrt::com_ptr<IDXGIFactory4> factory;
		DX::ThrowIfFailed(a_adapter->GetParent(IID_PPV_ARGS(factory.put())));
		IDXGIFactory4* preparedFactory = factory.detach();
		const auto prepareFactoryResult =
			_provider->PrepareFactory(&preparedFactory);
		factory.attach(preparedFactory);
		if (!prepareFactoryResult.Succeeded() || !factory) {
			return prepareFactoryResult.sdkResult
				? static_cast<HRESULT>(prepareFactoryResult.sdkResult)
				: E_FAIL;
		}

		_innerDesc = {};
		_innerDesc.Width = a_desc.BufferDesc.Width;
		_innerDesc.Height = a_desc.BufferDesc.Height;
		if (!_innerDesc.Width || !_innerDesc.Height) {
			RECT client{};
			if (!GetClientRect(a_desc.OutputWindow, &client)) {
				return HRESULT_FROM_WIN32(GetLastError());
			}
			_innerDesc.Width = static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
			_innerDesc.Height = static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));
		}
		_innerDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		_innerDesc.SampleDesc.Count = 1;
		_innerDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		_innerDesc.BufferCount = 2;
		_innerDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		_innerDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		_innerDesc.Flags = a_desc.Flags &
			(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

		IDXGISwapChain4* swapChain = nullptr;
		const auto providerResult = _provider->CreatePresentation(
			{
				.adapter = a_adapter,
				.device = _device12.get(),
				.queue = _queue.get(),
				.factory = factory.get(),
				.window = a_desc.OutputWindow,
				.description = &_innerDesc
			},
			&swapChain);
		const HRESULT result = providerResult.Succeeded()
			? S_OK
			: providerResult.sdkResult
				? static_cast<HRESULT>(providerResult.sdkResult)
				: E_FAIL;
		if (SUCCEEDED(result) && swapChain) {
			_swapChain.attach(swapChain);
			_frameIndex = _swapChain->GetCurrentBackBufferIndex();
			_proxyDesc = swap_chain_facade::BuildDescription(
				a_desc, _innerDesc);
		}
		return result;
	}

	HRESULT DX12SwapChain::CreateInteropFence()
	{
		DX::ThrowIfFailed(_device12->CreateFence(
			0,
			D3D12_FENCE_FLAG_SHARED,
			IID_PPV_ARGS(_fence12.put())));
		cs::render::annotation::SetName(
			_fence12.get(), "Upscaling/FrameGeneration.Fence12");
		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(_device12->CreateSharedHandle(
			_fence12.get(),
			nullptr,
			GENERIC_ALL,
			nullptr,
			&sharedHandle));
		const HRESULT openResult =
			_device11->OpenSharedFence(sharedHandle, IID_PPV_ARGS(_fence11.put()));
		CloseHandle(sharedHandle);
		DX::ThrowIfFailed(openResult);
		cs::render::annotation::SetName(
			_fence11.get(), "Upscaling/FrameGeneration.Fence11");
		DX::ThrowIfFailed(_device12->CreateFence(
			0,
			D3D12_FENCE_FLAG_SHARED,
			IID_PPV_ARGS(_inputRetirementFence12.put())));
		cs::render::annotation::SetName(
			_inputRetirementFence12.get(),
			"Upscaling/FrameGeneration.InputRetirementFence12");
		sharedHandle = nullptr;
		DX::ThrowIfFailed(_device12->CreateSharedHandle(
			_inputRetirementFence12.get(),
			nullptr,
			GENERIC_ALL,
			nullptr,
			&sharedHandle));
		const HRESULT openRetirementResult =
			_device11->OpenSharedFence(
				sharedHandle,
				IID_PPV_ARGS(_inputRetirementFence11.put()));
		CloseHandle(sharedHandle);
		DX::ThrowIfFailed(openRetirementResult);
		cs::render::annotation::SetName(
			_inputRetirementFence11.get(),
			"Upscaling/FrameGeneration.InputRetirementFence11");
		_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		return _fenceEvent ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	}

	HRESULT DX12SwapChain::CreateDisplayResources(
		UINT a_width,
		UINT a_height,
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
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> hudless{
			SharedD3D11D3D12Texture::Create(
				_device11.get(), _device12.get(), desc, "Upscaling/HUDLess[0]"),
			SharedD3D11D3D12Texture::Create(
				_device11.get(), _device12.get(), desc, "Upscaling/HUDLess[1]")
		};
		if (!proxy || !hudless[0] || !hudless[1]) {
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
		const HRESULT result = CreateDisplayResources(a_width, a_height, proxy, hudless);
		if (FAILED(result)) {
			return result;
		}
		_proxyBuffer = std::move(proxy);
		_hudlessBuffers = std::move(hudless);
		return S_OK;
	}

	HRESULT DX12SwapChain::RecreateFrameGenerationResources(UINT a_width, UINT a_height)
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

		desc.Format = DXGI_FORMAT_R32_FLOAT;
		auto depth = SharedD3D11D3D12Texture::Create(
			_device11.get(), _device12.get(), desc, "Upscaling/FrameGenerationDepth");
		desc.Format = DXGI_FORMAT_R16G16_FLOAT;
		auto motion = SharedD3D11D3D12Texture::Create(
			_device11.get(), _device12.get(), desc, "Upscaling/FrameGenerationMotion");
		if (!depth || !motion) {
			_depthBuffer.reset();
			_motionBuffer.reset();
			_frameGenerationDisabled = true;
			L->error(
				"Frame-generation bridge inputs were not recreated after the native resize");
			return E_OUTOFMEMORY;
		}

		_depthBuffer = std::move(depth);
		_motionBuffer = std::move(motion);
		++_inputResourceGeneration;
		_retirementEpochActive = false;
		_retirementLastResetFrame = UINT64_MAX;
		RearmInputRetirementLogBudget();
		const auto providerResult = _provider
			? _provider->CreateDisplayResources(
				a_width,
				a_height,
				DXGI_FORMAT_R8G8B8A8_UNORM,
				_innerDesc.BufferCount)
			: render::temporal::ProviderResult{};
		if (!providerResult.Succeeded()) {
			_frameGenerationDisabled = true;
			L->error(
				"Frame-generation provider resources were not recreated; real-frame presentation remains active");
			return S_FALSE;
		}
		return S_OK;
	}

	HRESULT DX12SwapChain::RestoreFrameGenerationProvider(
		UINT a_width, UINT a_height)
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
			DisableFrameGeneration(
				result.message.empty()
					? "Frame-generation provider restoration failed"
					: result.message.c_str());
			return S_FALSE;
		}
		_frameGenerationDisabled = false;
		return S_OK;
	}

	HRESULT DX12SwapChain::RecreateSuperResolutionBridge(
		const SuperResolutionExecutionContext& a_context)
	{
		try {
			const auto createLike = [&](ID3D11Resource* a_source,
									   UINT a_bindFlags,
									   std::string_view a_name) {
				winrt::com_ptr<ID3D11Texture2D> texture;
				DX::ThrowIfFailed(a_source->QueryInterface(
					IID_PPV_ARGS(texture.put())));
				D3D11_TEXTURE2D_DESC desc{};
				texture->GetDesc(&desc);
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = a_bindFlags;
				desc.CPUAccessFlags = 0;
				desc.MiscFlags = 0;
				desc.ArraySize = 1;
				desc.MipLevels = 1;
				desc.SampleDesc = { 1, 0 };
				return SharedD3D11D3D12Texture::Create(
					_device11.get(), _device12.get(), desc, a_name);
			};

			_srColorInput = createLike(
				a_context.colorInput, 0, "DLSSBridge.ColorInput");
			_srOutput = createLike(
				a_context.privateOutput,
				D3D11_BIND_UNORDERED_ACCESS,
				"DLSSBridge.Output");
			_srDepth = createLike(
				a_context.depth, 0, "DLSSBridge.Depth");
			_srMotion = createLike(
				a_context.motionVectors, 0, "DLSSBridge.Motion");
			_srReactive = createLike(
				a_context.reactiveMask, 0, "DLSSBridge.Reactive");
			_srTransparency = createLike(
				a_context.transparencyCompositionMask,
				0,
				"DLSSBridge.Transparency");
			if (!_srColorInput || !_srOutput || !_srDepth || !_srMotion ||
				!_srReactive || !_srTransparency) {
				return E_FAIL;
			}
			return S_OK;
		} catch (const winrt::hresult_error& e) {
			L->error(
				"Could not create D3D12 super-resolution bridge resources: {}",
				winrt::to_string(e.message()));
			return e.code();
		} catch (const std::exception& e) {
			L->error(
				"Could not create D3D12 super-resolution bridge resources: {}",
				e.what());
			return E_FAIL;
		}
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
		return _published && _swapChain && _proxyBuffer && _context11 && _queue &&
			_fence11 && _fence12 && _inputRetirementFence11 &&
			_inputRetirementFence12 && _fenceEvent &&
			_allocators[0] && _allocators[1] &&
			_commandLists[0] && _commandLists[1] &&
			_backBuffers[0] && _backBuffers[1];
	}

	bool DX12SwapChain::IsBridgeReady() const noexcept
	{
		return _bridgeReady || IsReady();
	}

	bool DX12SwapChain::IsFrameGenerationReady() const noexcept
	{
		return IsReady() && !_frameGenerationDisabled && _provider &&
			_provider->IsReady() &&
			_hudlessBuffers[0] && _hudlessBuffers[1] &&
			_depthBuffer && _motionBuffer;
	}

	UINT DX12SwapChain::GetWidth() const noexcept
	{
		return _innerDesc.Width;
	}

	UINT DX12SwapChain::GetHeight() const noexcept
	{
		return _innerDesc.Height;
	}

	UINT DX12SwapChain::GetFrameSlot() const noexcept
	{
		return _frameSlot;
	}

	SharedD3D11D3D12Texture* DX12SwapChain::GetHudlessTexture() const noexcept
	{
		return _frameSlot < _hudlessBuffers.size()
			? _hudlessBuffers[_frameSlot].get()
			: nullptr;
	}

	SharedD3D11D3D12Texture* DX12SwapChain::GetProxyTexture() const noexcept
	{
		return _proxyBuffer.get();
	}

	SharedD3D11D3D12Texture* DX12SwapChain::GetDepthTexture() const noexcept
	{
		return _depthBuffer.get();
	}

	SharedD3D11D3D12Texture* DX12SwapChain::GetMotionTexture() const noexcept
	{
		return _motionBuffer.get();
	}

	ID3D12GraphicsCommandList* DX12SwapChain::GetCommandList() const noexcept
	{
		return _commandLists[_frameSlot].get();
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

	FrameGenerationInputRetirementDiagnostics
		DX12SwapChain::GetInputRetirementDiagnostics() const noexcept
	{
		return {
			.acquisitions =
				_retirementAcquisitions.load(std::memory_order_relaxed),
			.immediateAcquisitions =
				_retirementImmediateAcquisitions.load(
					std::memory_order_relaxed),
			.gpuWaits =
				_retirementGpuWaits.load(std::memory_order_relaxed),
			.providerDrains =
				_retirementProviderDrains.load(std::memory_order_relaxed),
			.globalDrainAttempts =
				_retirementGlobalDrainAttempts.load(
					std::memory_order_relaxed),
			.globalDrainFailures =
				_retirementGlobalDrainFailures.load(
					std::memory_order_relaxed),
			.waitFailures =
				_retirementWaitFailures.load(std::memory_order_relaxed),
			.signals =
				_retirementSignals.load(std::memory_order_relaxed),
			.signalFailures =
				_retirementSignalFailures.load(std::memory_order_relaxed),
			.violations =
				_retirementViolations.load(std::memory_order_relaxed),
			.startupDrains =
				_retirementStartupDrains.load(std::memory_order_relaxed),
			.disableDrains =
				_retirementDisableDrains.load(std::memory_order_relaxed),
			.resizeDrains =
				_retirementResizeDrains.load(std::memory_order_relaxed),
			.teardownDrains =
				_retirementTeardownDrains.load(std::memory_order_relaxed),
			.steadyDrains =
				_retirementSteadyDrains.load(std::memory_order_relaxed),
			.lastRealFrame =
				_retirementLastRealFrame.load(std::memory_order_relaxed),
			.lastResourceGeneration =
				_retirementLastResourceGeneration.load(
					std::memory_order_relaxed),
			.lastRequiredFence =
				_retirementLastRequiredFence.load(
					std::memory_order_relaxed),
			.lastCompletedFence =
				_retirementLastCompletedFence.load(
					std::memory_order_relaxed),
			.waitCpuMicroseconds =
				_retirementWaitCpuMicroseconds.load(
					std::memory_order_relaxed),
			.lastSlot =
				_retirementLastSlot.load(std::memory_order_relaxed),
			.lastAcquireQueuedGpuWait =
				_retirementLastAcquireQueuedGpuWait.load(
					std::memory_order_relaxed)
		};
	}

	void DX12SwapChain::SetInputRetirementDetailedTracing(
		bool a_enabled) noexcept
	{
		if (a_enabled) {
			_retirementLogRearmRequested.store(
				true, std::memory_order_release);
		}
	}

	void DX12SwapChain::RearmInputRetirementLogBudget() noexcept
	{
		_retirementLogBudget.Rearm();
		_retirementLogRearmRequested.store(
			false, std::memory_order_release);
	}

	void DX12SwapChain::RecordGlobalDrain(
		std::string_view a_reason,
		const render::temporal::ProviderResult& a_result) noexcept
	{
		if (!a_result.globalDrainAttempted) {
			return;
		}
		_retirementGlobalDrainAttempts.fetch_add(
			1, std::memory_order_relaxed);
		auto* counter = &_retirementTeardownDrains;
		if (a_reason == "startup") {
			counter = &_retirementStartupDrains;
		} else if (a_reason == "disable") {
			counter = &_retirementDisableDrains;
		} else if (a_reason == "resize") {
			counter = &_retirementResizeDrains;
		} else if (a_reason == "steady") {
			counter = &_retirementSteadyDrains;
		}
		if (a_result.globalDrainCompleted) {
			counter->fetch_add(1, std::memory_order_relaxed);
			_retirementProviderDrains.fetch_add(
				1, std::memory_order_relaxed);
		} else {
			_retirementGlobalDrainFailures.fetch_add(
				1, std::memory_order_relaxed);
			_retirementViolations.fetch_add(
				1, std::memory_order_relaxed);
		}
		if (!render::TemporalPipeline::Get().DetailedTracingEnabled()) {
			return;
		}
		try {
			L->info(
				"FG_RETIRE provider={} real_frame={} slot={} operation=global_drain resource_generation={} token=0 queue_id=0x0 required=0 completed=0 source_queue=sdk point={} resources=all_present_inputs cpu_wait=1 gpu_wait=1 wait_us={} result={} sdk_result={} violation_code={} no_reuse_before_complete={}",
				_provider ? _provider->Name() : "none",
				_preparedRealFrame,
				_frameSlot,
				_inputResourceGeneration,
				a_reason,
				a_result.globalDrainCpuMicroseconds,
				a_result.globalDrainCompleted ? "completed" : "failed",
				a_result.sdkResult,
				a_result.globalDrainCompleted
					? "none"
					: "global_drain_failed",
				a_result.globalDrainCompleted);
		} catch (const std::exception& e) {
			ReportRetirementLogFailure("global_drain", e.what());
		} catch (...) {
			ReportRetirementLogFailure("global_drain");
		}
	}

	void DX12SwapChain::LogInputRetirement(
		render::temporal::PresentInputRetirementLogKind a_kind,
		std::string_view a_operation,
		const render::temporal::PresentInputRetirementToken& a_token,
		std::uint32_t a_slot,
		std::uint64_t a_completedValue,
		bool a_cpuWait,
		bool a_gpuWait,
		std::uint64_t a_waitMicroseconds,
		HRESULT a_result,
		std::string_view a_violationCode,
		bool a_violation) noexcept
	{
		const bool detailed =
			render::TemporalPipeline::Get().DetailedTracingEnabled();
		if (_retirementLogRearmRequested.exchange(
				false, std::memory_order_acq_rel)) {
			RearmInputRetirementLogBudget();
		}
		if (!_retirementLogBudget.SetTracingEnabled(detailed)) {
			return;
		}
		if (!_retirementLogBudget.ShouldLog(
				a_kind, a_slot, a_token.value, a_violation)) {
			const auto acquisitions =
				_retirementAcquisitions.load(std::memory_order_relaxed);
			if (!_retirementLogBudget.ShouldLogSummary(acquisitions)) {
				return;
			}
			try {
				const auto snapshot = GetInputRetirementDiagnostics();
				L->info(
					"FG_RETIRE provider={} operation=summary acquisitions={} immediate={} gpu_waits={} provider_drains={} global_drain_attempts={} global_drain_failures={} wait_failures={} signals={} signal_failures={} violations={} startup_drains={} disable_drains={} resize_drains={} teardown_drains={} steady_drains={} wait_us={}",
					_provider ? _provider->Name() : "none",
					snapshot.acquisitions,
					snapshot.immediateAcquisitions,
					snapshot.gpuWaits,
					snapshot.providerDrains,
					snapshot.globalDrainAttempts,
					snapshot.globalDrainFailures,
					snapshot.waitFailures,
					snapshot.signals,
					snapshot.signalFailures,
					snapshot.violations,
					snapshot.startupDrains,
					snapshot.disableDrains,
					snapshot.resizeDrains,
					snapshot.teardownDrains,
					snapshot.steadyDrains,
					snapshot.waitCpuMicroseconds);
			} catch (const std::exception& e) {
				ReportRetirementLogFailure("summary", e.what());
			} catch (...) {
				ReportRetirementLogFailure("summary");
			}
			return;
		}
		try {
			L->info(
				"FG_RETIRE provider={} real_frame={} slot={} operation={} resource_generation={} current_resource_generation={} token={} queue_id={:#x} current_queue_id={:#x} required={} completed={} source_queue={} point={} resources={} cpu_wait={} gpu_wait={} wait_us={} result={:#010x} violation_code={} no_reuse_before_complete={}",
				_provider ? _provider->Name() : "none",
				a_token.realFrame,
				a_slot,
				a_operation,
				a_token.resourceGeneration,
				_inputResourceGeneration,
				a_token.value,
				a_token.queueIdentity,
				static_cast<std::uint64_t>(
					reinterpret_cast<std::uintptr_t>(_queue.get())),
				a_token.value,
				a_completedValue,
				a_token.mode ==
						render::temporal::PresentInputRetirementMode::
							kProviderDrain
					? "sdk"
					: "game_direct",
				a_token.mode ==
						render::temporal::PresentInputRetirementMode::
							kSynchronousPresentQueue
					? "post_present_callback_submission"
					: a_token.mode ==
							  render::temporal::PresentInputRetirementMode::
								  kRecordedCommandList
						? "prepare_and_final_copy"
						: "provider_drain",
				a_token.mode ==
						render::temporal::PresentInputRetirementMode::
							kSynchronousPresentQueue
					? "hudless"
					: a_token.mode ==
							  render::temporal::PresentInputRetirementMode::
								  kRecordedCommandList
						? "depth,motion,final_copy"
						: "all_present_inputs",
				a_cpuWait,
				a_gpuWait,
				a_waitMicroseconds,
				static_cast<std::uint32_t>(a_result),
				a_violationCode,
				!a_violation && SUCCEEDED(a_result));
		} catch (const std::exception& e) {
			ReportRetirementLogFailure(a_operation.data(), e.what());
		} catch (...) {
			ReportRetirementLogFailure(a_operation.data());
		}
	}

	bool DX12SwapChain::EvaluateD3D12SuperResolution(
		render::temporal::ISuperResolutionProvider& a_provider,
		const SuperResolutionExecutionContext& a_context)
	{
		if (!IsBridgeReady() || !a_context.commandContext ||
			!a_context.colorInput || !a_context.privateOutput ||
			!a_context.depth || !a_context.motionVectors ||
			!a_context.reactiveMask ||
			!a_context.transparencyCompositionMask) {
			return false;
		}

		const auto matches = [](const auto& a_shared, ID3D11Resource* a_source) {
			if (!a_shared || !a_source) {
				return false;
			}
			winrt::com_ptr<ID3D11Texture2D> source;
			if (FAILED(a_source->QueryInterface(
				IID_PPV_ARGS(source.put())))) {
				return false;
			}
			D3D11_TEXTURE2D_DESC sourceDesc{};
			D3D11_TEXTURE2D_DESC sharedDesc{};
			source->GetDesc(&sourceDesc);
			a_shared->texture11->GetDesc(&sharedDesc);
			return sourceDesc.Width == sharedDesc.Width &&
				sourceDesc.Height == sharedDesc.Height &&
				sourceDesc.Format == sharedDesc.Format;
		};
		if (!matches(_srColorInput, a_context.colorInput) ||
			!matches(_srOutput, a_context.privateOutput) ||
			!matches(_srDepth, a_context.depth) ||
			!matches(_srMotion, a_context.motionVectors) ||
			!matches(_srReactive, a_context.reactiveMask) ||
			!matches(
				_srTransparency,
				a_context.transparencyCompositionMask)) {
			if (FAILED(RecreateSuperResolutionBridge(a_context))) {
				return false;
			}
		}

		try {
			_context11->CopyResource(
				_srColorInput->texture11.get(), a_context.colorInput);
			_context11->CopyResource(
				_srDepth->texture11.get(), a_context.depth);
			_context11->CopyResource(
				_srMotion->texture11.get(), a_context.motionVectors);
			_context11->CopyResource(
				_srReactive->texture11.get(), a_context.reactiveMask);
			_context11->CopyResource(
				_srTransparency->texture11.get(),
				a_context.transparencyCompositionMask);

			const UINT64 d3d11Ready = _nextFenceValue++;
			DX::ThrowIfFailed(
				_context11->Signal(_fence11.get(), d3d11Ready));
			DX::ThrowIfFailed(
				_queue->Wait(_fence12.get(), d3d11Ready));
			DX::ThrowIfFailed(WaitForFrame(_frameSlot));
			DX::ThrowIfFailed(_allocators[_frameSlot]->Reset());
			DX::ThrowIfFailed(_commandLists[_frameSlot]->Reset(
				_allocators[_frameSlot].get(), nullptr));
			auto* commandList = _commandLists[_frameSlot].get();
			const render::temporal::SuperResolutionRequest request{
				.recording = render::temporal::D3D12RecordingContext{
					.commandList = commandList,
					.queue = _queue.get(),
					.slot = _frameSlot
				},
				.colorInput = render::temporal::D3D12GpuView{
					.resource = _srColorInput->resource12.get()
				},
				.privateOutput = render::temporal::D3D12GpuView{
					.resource = _srOutput->resource12.get()
				},
				.depth = render::temporal::D3D12GpuView{
					.resource = _srDepth->resource12.get()
				},
				.motionVectors = render::temporal::D3D12GpuView{
					.resource = _srMotion->resource12.get()
				},
				.reactiveMask = render::temporal::D3D12GpuView{
					.resource = _srReactive->resource12.get()
				},
				.transparencyCompositionMask =
					render::temporal::D3D12GpuView{
						.resource = _srTransparency->resource12.get()
					},
				.renderWidth = a_context.renderWidth,
				.renderHeight = a_context.renderHeight,
				.outputWidth = a_context.outputWidth,
				.outputHeight = a_context.outputHeight,
				.qualityMode = a_context.qualityMode,
				.providerPreset = a_context.providerPreset,
				.realFrame = a_context.realFrame,
				.engineFrame = a_context.engineFrame,
				.jitterX = a_context.jitterX,
				.jitterY = a_context.jitterY,
				.sharpness = a_context.sharpness,
				.frameTimeMilliseconds =
					a_context.frameTimeMilliseconds,
				.cameraNear = a_context.cameraNear,
				.cameraFar = a_context.cameraFar,
				.cameraVerticalFov = a_context.cameraVerticalFov,
				.resetHistory = a_context.resetHistory,
				.color = a_context.color,
				.camera = a_context.camera
			};
			if (!a_provider.Record(request).Succeeded()) {
				DX::ThrowIfFailed(commandList->Close());
				return false;
			}
			DX::ThrowIfFailed(commandList->Close());
			ID3D12CommandList* lists[]{ commandList };
			_queue->ExecuteCommandLists(1, lists);
			const UINT64 d3d12Done = _nextFenceValue++;
			DX::ThrowIfFailed(
				_queue->Signal(_fence12.get(), d3d12Done));
			_allocatorFenceValues[_frameSlot] = d3d12Done;
			DX::ThrowIfFailed(
				_context11->Wait(_fence11.get(), d3d12Done));
			_context11->CopyResource(
				a_context.privateOutput, _srOutput->texture11.get());
			return true;
		} catch (const winrt::hresult_error& e) {
			L->error(
				"D3D12 super-resolution bridge failed: {}",
				winrt::to_string(e.message()));
		} catch (const std::exception& e) {
			L->error("D3D12 super-resolution bridge failed: {}", e.what());
		}
		return false;
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
		_retirementEpochActive = false;
		_frameGenerationInputsReady = false;
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		render::TemporalPipeline::Get().RequestFrameGenerationReset();
		if (firstFailure) {
			if (_callbacks.recordFailure) {
				_callbacks.recordFailure(a_reason ? a_reason : "Unknown frame-generation failure.");
			}
		}
		try {
			L->error("Frame generation disabled: {}", a_reason ? a_reason : "unknown failure");
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
		if (!_provider || !_inputRetirementFence12 ||
			!_inputRetirementFence11 || !_context11 || !_queue) {
			return false;
		}
		if (!_retirementEpochActive) {
			RearmInputRetirementLogBudget();
			_retirementEpochActive = true;
		}
		render::temporal::ProviderResult result{
			.code = render::temporal::ProviderResultCode::kSuccess
		};
		const auto completed =
			_inputRetirementFence12->GetCompletedValue();
		const bool detailed =
			render::TemporalPipeline::Get().DetailedTracingEnabled();
		std::uint64_t waitMicroseconds = 0;
		const auto acquired = _inputReuseGate.Acquire(
			_frameSlot,
			_inputResourceGeneration,
			static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(_queue.get())),
			completed,
			[&](const render::temporal::PresentInputRetirementToken&
					a_token) {
				if (a_token.mode ==
					render::temporal::PresentInputRetirementMode::
						kSynchronousPresentQueue) {
					if (!detailed) {
						return _context11->Wait(
							_inputRetirementFence11.get(),
							a_token.value);
					}
					const auto start =
						std::chrono::steady_clock::now();
					const HRESULT waitResult = _context11->Wait(
						_inputRetirementFence11.get(),
						a_token.value);
					waitMicroseconds = static_cast<std::uint64_t>(
						std::chrono::duration_cast<
							std::chrono::microseconds>(
							std::chrono::steady_clock::now() - start)
							.count());
					return waitResult;
				}
				auto timing = render::TemporalPipeline::Get()
					.MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::
							kAcquirePresentInputs);
				result = _provider->AcquirePresentInputs();
				RecordGlobalDrain("steady", result);
				return result.Succeeded()
					? S_OK
					: result.sdkResult
						? static_cast<HRESULT>(result.sdkResult)
						: E_FAIL;
			});
		if (acquired.firstAcquire) {
			_retirementAcquisitions.fetch_add(
				1, std::memory_order_relaxed);
		}
		if (acquired.Succeeded() && acquired.firstAcquire) {
			if (acquired.waitRequired) {
				if (acquired.token.mode ==
					render::temporal::PresentInputRetirementMode::
						kSynchronousPresentQueue) {
					_retirementGpuWaits.fetch_add(
						1, std::memory_order_relaxed);
				}
			} else {
				_retirementImmediateAcquisitions.fetch_add(
					1, std::memory_order_relaxed);
			}
		}
		if (!acquired.Succeeded()) {
			_retirementWaitFailures.fetch_add(
				1, std::memory_order_relaxed);
			_retirementViolations.fetch_add(
				1, std::memory_order_relaxed);
		}
		if (render::temporal::ShouldPublishPresentInputAcquireTelemetry(
				acquired)) {
			_retirementLastRealFrame.store(
				acquired.token.realFrame, std::memory_order_relaxed);
			_retirementLastResourceGeneration.store(
				acquired.token.resourceGeneration,
				std::memory_order_relaxed);
			_retirementLastRequiredFence.store(
				acquired.token.value, std::memory_order_relaxed);
			_retirementLastCompletedFence.store(
				acquired.completedValue, std::memory_order_relaxed);
			_retirementWaitCpuMicroseconds.fetch_add(
				waitMicroseconds, std::memory_order_relaxed);
			_retirementLastSlot.store(
				_frameSlot, std::memory_order_relaxed);
			_retirementLastAcquireQueuedGpuWait.store(
				acquired.waitRequired &&
					acquired.token.mode ==
						render::temporal::PresentInputRetirementMode::
							kSynchronousPresentQueue,
				std::memory_order_relaxed);
		}
		auto logToken = acquired.token;
		if (logToken.resourceGeneration == 0) {
			logToken.mode = _provider->GetPresentInputRetirementMode();
			logToken.realFrame =
				render::TemporalPipeline::Get().CurrentRealFrame();
			logToken.resourceGeneration = _inputResourceGeneration;
			logToken.queueIdentity = static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(_queue.get()));
		}
		if (acquired.firstAcquire &&
			logToken.mode !=
				render::temporal::PresentInputRetirementMode::
					kRecordedCommandList) {
			LogInputRetirement(
				render::temporal::PresentInputRetirementLogKind::
					kAcquire,
				acquired.Succeeded()
					? acquired.waitRequired
						? "acquire_wait"
						: "acquire_immediate"
					: "acquire_failed",
				logToken,
				_frameSlot,
				acquired.completedValue,
				acquired.token.mode ==
					render::temporal::PresentInputRetirementMode::
						kProviderDrain,
				acquired.waitRequired &&
					acquired.token.mode ==
						render::temporal::PresentInputRetirementMode::
							kSynchronousPresentQueue,
				waitMicroseconds,
				acquired.result,
				render::temporal::PresentInputAcquireCodeName(
					acquired.code),
				!acquired.Succeeded());
		}
		if (!acquired.Succeeded()) {
			DisableFrameGeneration(
				result.message.empty()
					? render::temporal::
						  PresentInputAcquireFailureMessage(
							  acquired.code)
					: result.message.c_str());
		}
		return acquired.Succeeded();
	}

	HRESULT DX12SwapChain::WaitForFrame(UINT a_slot) noexcept
	{
		if (a_slot >= std::size(_allocatorFenceValues)) {
			return E_INVALIDARG;
		}
		const auto value = _allocatorFenceValues[a_slot];
		if (!value || _fence12->GetCompletedValue() >= value) {
			return S_OK;
		}
		const HRESULT result = _fence12->SetEventOnCompletion(value, _fenceEvent);
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
		return WaitForSingleObject(_fenceEvent, INFINITE) == WAIT_OBJECT_0
			? S_OK
			: HRESULT_FROM_WIN32(GetLastError());
	}

	HRESULT DX12SwapChain::Present(UINT a_syncInterval, UINT a_flags) noexcept
	{
		return PresentInternal(
			a_syncInterval, a_flags, nullptr, false);
	}

	HRESULT DX12SwapChain::Present1(
		UINT a_syncInterval,
		UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept
	{
		return PresentInternal(
			a_syncInterval, a_flags, a_parameters, true);
	}

	HRESULT DX12SwapChain::PresentInternal(
		UINT a_syncInterval,
		UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters,
		bool a_usePresent1) noexcept
	{
		if (a_flags & DXGI_PRESENT_TEST) {
			auto& pipeline = render::TemporalPipeline::Get();
			pipeline.BeginPresentAttempt(a_flags);
			const HRESULT result = InvokeInnerPresent(
				a_syncInterval, a_flags, a_parameters, a_usePresent1);
			pipeline.EndPresentAttempt(a_flags, result);
			if (_preparedTransaction) {
				pipeline.RecordPresentAttempt(
					_frameSlot, a_flags, result);
			}
			return result;
		}
		try {
			const HRESULT result = PresentImpl(
				a_syncInterval,
				a_flags,
				a_parameters,
				a_usePresent1);
			if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
				if (_callbacks.clearCapture) {
					_callbacks.clearCapture();
				}
			}
			return result;
		} catch (const std::exception& e) {
			if (_callbacks.clearCapture) {
				_callbacks.clearCapture();
			}
			DisableFrameGeneration(e.what());
			return E_FAIL;
		} catch (...) {
			if (_callbacks.clearCapture) {
				_callbacks.clearCapture();
			}
			DisableFrameGeneration("unhandled presentation failure");
			return E_FAIL;
		}
	}

	HRESULT DX12SwapChain::InvokeInnerPresent(
		UINT a_syncInterval,
		UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters,
		bool a_usePresent1) noexcept
	{
		if (!_swapChain) {
			return DXGI_ERROR_INVALID_CALL;
		}
		return a_usePresent1
			? _swapChain->Present1(
				a_syncInterval, a_flags, a_parameters)
			: _swapChain->Present(a_syncInterval, a_flags);
	}

	HRESULT DX12SwapChain::PresentImpl(
		UINT a_syncInterval,
		UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters,
		bool a_usePresent1)
	{
		if (!IsReady()) {
			auto& pipeline = render::TemporalPipeline::Get();
			pipeline.BeginPresentAttempt(a_flags);
			const HRESULT result = InvokeInnerPresent(
				a_syncInterval, a_flags, a_parameters, a_usePresent1);
			pipeline.EndPresentAttempt(a_flags, result);
			return result;
		}

		if (!_presentPrepared) {
			const auto frameState = _callbacks.queryFrameState
				? _callbacks.queryFrameState()
				: FrameGenerationFrameState{};
			render::TemporalPipeline::Get()
				.Renderer()
				.CaptureFrameGenerationFinalDebugSnapshot();
			cs::render::annotation::SetMarker("FG_D3D11ProductionComplete");
			const UINT64 d3d11Ready = _nextFenceValue++;
			DX::ThrowIfFailed(_context11->Signal(_fence11.get(), d3d11Ready));
			DX::ThrowIfFailed(_queue->Wait(_fence12.get(), d3d11Ready));
			{
				auto timing = render::TemporalPipeline::Get()
					.MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::
							kAllocatorFenceWait);
				DX::ThrowIfFailed(WaitForFrame(_frameSlot));
			}
			DX::ThrowIfFailed(_allocators[_frameSlot]->Reset());
			DX::ThrowIfFailed(_commandLists[_frameSlot]->Reset(
				_allocators[_frameSlot].get(),
				nullptr));

			auto* commandList = _commandLists[_frameSlot].get();
			{
				auto timing = render::TemporalPipeline::Get()
					.MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kCopyRecord);
				cs::render::annotation::ScopedEvent copyScope(
					commandList, "FG_CopyRealFrame");
				const std::array barriersBefore{
					Transition(_proxyBuffer->resource12.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
					Transition(_backBuffers[_frameIndex].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
				};
				commandList->ResourceBarrier(static_cast<UINT>(barriersBefore.size()), barriersBefore.data());
				commandList->CopyResource(_backBuffers[_frameIndex].get(), _proxyBuffer->resource12.get());
				const std::array barriersAfter{
					Transition(_proxyBuffer->resource12.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
					Transition(_backBuffers[_frameIndex].get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
				};
				commandList->ResourceBarrier(static_cast<UINT>(barriersAfter.size()), barriersAfter.data());
			}

			const bool requested =
				_frameGenerationInputsReady && !_frameGenerationDisabled &&
				frameState.enable;
			_preparedRealFrame = frameState.realFrame;
			_vendorConsumptionPossible = requested;
			bool frameGenerationPrepared = false;
			{
				cs::render::annotation::ScopedEvent prepareScope(
					commandList, "FG_ConfigurePrepare");
				const render::temporal::FrameGenerationRequest request{
					.recording = {
						.commandList = commandList,
						.queue = _queue.get(),
						.slot = _frameSlot
					},
					.depth = {
						.resource = _depthBuffer->resource12.get(),
						.state = D3D12_RESOURCE_STATE_COMMON
					},
					.motionVectors = {
						.resource = _motionBuffer->resource12.get(),
						.state = D3D12_RESOURCE_STATE_COMMON
					},
					.hudlessColor = {
						.resource = _hudlessBuffers[_frameSlot]->resource12.get(),
						.state = D3D12_RESOURCE_STATE_COMMON
					},
					.finalColor = {
						.resource = _proxyBuffer->resource12.get(),
						.state = D3D12_RESOURCE_STATE_COMMON
					},
					.realFrame = frameState.realFrame,
					.renderWidth = frameState.renderWidth,
					.renderHeight = frameState.renderHeight,
					.outputWidth = _innerDesc.Width,
					.outputHeight = _innerDesc.Height,
					.jitterX = frameState.jitterX,
					.jitterY = frameState.jitterY,
					.frameTimeMilliseconds =
						frameState.frameTimeMilliseconds,
					.enabled = requested,
					.resetHistory = frameState.resetHistory,
					.uiMode =
						render::temporal::UiCompositionMode::kHudlessAndFinal,
					.color = frameState.color,
					.camera = frameState.camera
				};
				auto& pipeline = render::TemporalPipeline::Get();
				pipeline.RecordFrameGenerationFrameTimeInput(
					request.frameTimeMilliseconds);
				const auto preparation = [&] {
					auto timing = pipeline.MeasureFrameGenerationCpuPhase(
						render::FrameGenerationCpuPhase::kPrepareFrame);
					return render::temporal::PrepareFrameSafely(
						*_provider, request);
				}();
				const auto& prepareResult = preparation.prepare;
				frameGenerationPrepared = preparation.prepared;
				if (!frameGenerationPrepared) {
					_vendorConsumptionPossible = false;
					DisableFrameGeneration(
						"Prepare frame", prepareResult);
					if (!preparation.safeToPresent) {
						L->error("{}", render::temporal::FormatProviderFailure(
							"Cancel frame", preparation.cancel));
						DX::ThrowIfFailed(commandList->Close());
						_presentPrepared = false;
						_preparedFrameGeneration = false;
						_vendorConsumptionPossible = false;
						_preparedTransaction = false;
						return E_FAIL;
					}
				}
			}

			DX::ThrowIfFailed(commandList->Close());
			ID3D12CommandList* lists[] = { commandList };
			_queue->ExecuteCommandLists(1, lists);
			const UINT64 d3d12Done = _nextFenceValue++;
			DX::ThrowIfFailed(_queue->Signal(_fence12.get(), d3d12Done));
			_allocatorFenceValues[_frameSlot] = d3d12Done;
			const HRESULT producerWait =
				_context11->Wait(_fence11.get(), d3d12Done);
			DX::ThrowIfFailed(producerWait);
			_preparedFrameGeneration =
				requested && frameGenerationPrepared && !_frameGenerationDisabled;
			if (_preparedFrameGeneration &&
				frameState.resetHistory &&
				_retirementLastResetFrame != frameState.realFrame) {
				RearmInputRetirementLogBudget();
				_retirementLastResetFrame = frameState.realFrame;
			}
			_retirementEpochActive = _preparedFrameGeneration;
			if (_preparedFrameGeneration) {
				LogInputRetirement(
					render::temporal::PresentInputRetirementLogKind::
						kStandalone,
					"prepare_copy",
					{
						.mode = render::temporal::
							PresentInputRetirementMode::
								kRecordedCommandList,
						.value = d3d12Done,
						.realFrame = _preparedRealFrame,
						.resourceGeneration = _inputResourceGeneration,
						.queueIdentity = static_cast<std::uint64_t>(
							reinterpret_cast<std::uintptr_t>(_queue.get()))
					},
					_frameSlot,
					_fence12->GetCompletedValue(),
					false,
					true,
					0,
					producerWait);
			}
			_preparedTransaction = _frameGenerationInputsReady &&
				render::TemporalPipeline::Get().PreparePresent(
					_frameSlot, _preparedFrameGeneration);
			_presentPrepared = true;
		}

		cs::render::annotation::SetMarker(
			"Upscaling/FrameGeneration/Present");
		auto& pipeline = render::TemporalPipeline::Get();
		pipeline.BeginPresentAttempt(a_flags);
		const auto retirementMode =
			_provider->GetPresentInputRetirementMode();
		const auto retirement = render::temporal::PresentAndRetireInputs(
			retirementMode,
			_vendorConsumptionPossible,
			_preparedRealFrame,
			_inputResourceGeneration,
			static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(_queue.get())),
			_nextInputRetirementValue,
			[&]() {
				auto timing = pipeline.MeasureFrameGenerationCpuPhase(
					render::FrameGenerationCpuPhase::kSdkPresent);
				return InvokeInnerPresent(
					a_syncInterval,
					a_flags,
					a_parameters,
					a_usePresent1);
			},
			[&](std::uint64_t a_value) {
				return _queue->Signal(
					_inputRetirementFence12.get(), a_value);
			});
		const HRESULT presentResult = retirement.presentResult;
		pipeline.EndPresentAttempt(a_flags, presentResult);
		if (retirement.token &&
			retirementMode ==
				render::temporal::PresentInputRetirementMode::
					kSynchronousPresentQueue) {
			_retirementSignals.fetch_add(1, std::memory_order_relaxed);
		} else if (FAILED(retirement.signalResult)) {
			_retirementSignalFailures.fetch_add(
				1, std::memory_order_relaxed);
			_retirementViolations.fetch_add(
				1, std::memory_order_relaxed);
			const render::temporal::PresentInputRetirementToken failed{
				.mode = retirementMode,
				.value = _nextInputRetirementValue - 1,
				.realFrame = _preparedRealFrame,
				.resourceGeneration = _inputResourceGeneration,
				.queueIdentity = static_cast<std::uint64_t>(
					reinterpret_cast<std::uintptr_t>(_queue.get()))
			};
			LogInputRetirement(
				render::temporal::PresentInputRetirementLogKind::
					kStandalone,
				"signal_failed",
				failed,
				_frameSlot,
				_inputRetirementFence12->GetCompletedValue(),
				false,
				false,
				0,
				retirement.signalResult,
				"signal_failed",
				true);
			_inputReuseGate.MarkSubmitted(
				_frameSlot, failed);
			DisableFrameGeneration(
				"Frame-generation input retirement fence signal failed");
			return retirement.signalResult;
		}
		render::temporal::PresentStatusCollection status;
		if (render::temporal::ShouldObservePresentStatus(
				a_flags, presentResult)) {
			auto timing = pipeline.MeasureFrameGenerationCpuPhase(
				render::FrameGenerationCpuPhase::kCollectPresentStatus);
			status = render::temporal::CollectAcceptedPresentStatus(
				*_provider, a_flags, presentResult);
		}
		if (status.observed) {
			pipeline.RecordGeneratedFrames(
				status.generatedFrames);
			pipeline.RecordPresentedFrames(
				status.presentedFrames);
			if (!status.result.Succeeded()) {
				const auto disableResult =
					_provider->SetGenerationEnabled(false);
				DisableFrameGeneration(
					"Collect present status", status.result);
				if (!disableResult.Succeeded()) {
					L->error("{}", render::temporal::FormatProviderFailure(
						"Disable after present failure", disableResult));
				}
			}
		}
		if (_preparedTransaction) {
			pipeline.RecordPresentAttempt(
				_frameSlot, a_flags, presentResult);
		}
		if (presentResult == DXGI_ERROR_WAS_STILL_DRAWING) {
			if (retirement.token &&
				retirementMode ==
					render::temporal::PresentInputRetirementMode::
						kSynchronousPresentQueue) {
				LogInputRetirement(
					render::temporal::PresentInputRetirementLogKind::
						kStandalone,
					"signal_retry_uncommitted",
					*retirement.token,
					_frameSlot,
					_inputRetirementFence12->GetCompletedValue(),
					false,
					false,
					0,
					retirement.signalResult);
			}
			return presentResult;
		}
		if (retirement.token &&
			retirementMode ==
				render::temporal::PresentInputRetirementMode::
					kSynchronousPresentQueue) {
			LogInputRetirement(
				render::temporal::PresentInputRetirementLogKind::kSignal,
				"signal",
				*retirement.token,
				_frameSlot,
				_inputRetirementFence12->GetCompletedValue(),
				false,
				false,
				0,
				retirement.signalResult);
		}
		_inputReuseGate.MarkSubmitted(
			_frameSlot, retirement.token);
		_presentPrepared = false;
		_preparedFrameGeneration = false;
		_vendorConsumptionPossible = false;
		_preparedTransaction = false;
		_preparedRealFrame = 0;
		_frameGenerationInputsReady = false;
		ClearSharedBuffers(false);
		if (SUCCEEDED(presentResult)) {
			_frameSlot = (_frameSlot + 1) % static_cast<UINT>(std::size(_allocators));
			_frameIndex = _swapChain->GetCurrentBackBufferIndex();
		} else {
			render::TemporalPipeline::Get().RequestFrameGenerationReset();
		}
		return presentResult;
	}

	void DX12SwapChain::ClearSharedBuffers(bool a_clearFrameGenerationInputs) noexcept
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
			if (_depthBuffer && _depthBuffer->rtv11) {
				_context11->ClearRenderTargetView(_depthBuffer->rtv11.get(), clear);
			}
			if (_motionBuffer && _motionBuffer->rtv11) {
				_context11->ClearRenderTargetView(_motionBuffer->rtv11.get(), clear);
			}
		}
	}

	HRESULT DX12SwapChain::GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface) noexcept
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

	HRESULT DX12SwapChain::SetFullscreenState(
		BOOL a_fullscreen,
		IDXGIOutput*) noexcept
	{
		return a_fullscreen
			? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
			: S_OK;
	}

	HRESULT DX12SwapChain::GetFullscreenState(
		BOOL* a_fullscreen,
		IDXGIOutput** a_target) noexcept
	{
		if (!a_fullscreen) {
			if (a_target) {
				*a_target = nullptr;
			}
			return E_POINTER;
		}
		*a_fullscreen = FALSE;
		if (a_target) {
			*a_target = nullptr;
		}
		return S_OK;
	}

	HRESULT DX12SwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc = _proxyDesc;
		return S_OK;
	}

	HRESULT DX12SwapChain::GetDesc1(
		DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc =
			swap_chain_facade::BuildDescription1(_proxyDesc);
		return S_OK;
	}

	HRESULT DX12SwapChain::GetFullscreenDesc(
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc =
			swap_chain_facade::BuildFullscreenDescription(
				_proxyDesc);
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

	UINT DX12SwapChain::GetCurrentBackBufferIndex() noexcept
	{
		return 0;
	}

	HRESULT DX12SwapChain::CheckColorSpaceSupport(
		DXGI_COLOR_SPACE_TYPE a_colorSpace,
		UINT* a_support) noexcept
	{
		if (!a_support) {
			return E_POINTER;
		}
		*a_support = 0;
		if (a_colorSpace !=
			DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
			return S_OK;
		}
		return _swapChain
			? _swapChain->CheckColorSpaceSupport(
				a_colorSpace, a_support)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT DX12SwapChain::SetColorSpace1(
		DXGI_COLOR_SPACE_TYPE a_colorSpace) noexcept
	{
		if (a_colorSpace !=
			DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		return _swapChain
			? _swapChain->SetColorSpace1(a_colorSpace)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT DX12SwapChain::SetHDRMetaData(
		DXGI_HDR_METADATA_TYPE a_type,
		UINT a_size,
		void* a_metadata) noexcept
	{
		if (a_type != DXGI_HDR_METADATA_TYPE_NONE) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		if (a_size || a_metadata) {
			return E_INVALIDARG;
		}
		return _swapChain
			? _swapChain->SetHDRMetaData(a_type, 0, nullptr)
			: DXGI_ERROR_INVALID_CALL;
	}

	void DX12SwapChain::RecordSwapChainFacadeEvent(
		SwapChainFacadeEvent a_event) noexcept
	{
		const auto index = static_cast<std::uint32_t>(a_event);
		if (index >= static_cast<std::uint32_t>(
				SwapChainFacadeEvent::kCount)) {
			return;
		}
		const std::uint32_t bit = 1u << index;
		if (_facadeEventLogMask.fetch_or(
				bit, std::memory_order_relaxed) & bit) {
			return;
		}
		const auto name = SwapChainFacadeEventName(a_event);
		try {
			if (a_event >=
				SwapChainFacadeEvent::kPresent1MetadataRejected &&
				a_event != SwapChainFacadeEvent::kResizeBuffers1) {
				L->warn(
					"SWAPCHAIN_FACADE event={} result=unsupported",
					name);
			} else {
				L->info(
					"SWAPCHAIN_FACADE event={} result=observed",
					name);
			}
		} catch (...) {
			OutputDebugStringA(
				"SWAPCHAIN_FACADE logging failure\n");
		}
	}

	HRESULT DX12SwapChain::ResizeTarget(
		const DXGI_MODE_DESC* a_target) noexcept
	{
		return a_target
			? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
			: E_INVALIDARG;
	}

	HRESULT DX12SwapChain::ResizeBuffers(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags) noexcept
	{
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		try {
			return ResizeBuffersImpl(a_bufferCount, a_width, a_height, a_format, a_flags);
		} catch (const std::exception& e) {
			DisableFrameGeneration(e.what());
			return E_FAIL;
		} catch (...) {
			DisableFrameGeneration("unhandled resize failure");
			return E_FAIL;
		}
	}

	HRESULT DX12SwapChain::ResizeBuffers1(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags,
		const UINT* a_creationNodeMask,
		IUnknown* const* a_presentQueue) noexcept
	{
		if (a_creationNodeMask || a_presentQueue) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		return ResizeBuffers(
			a_bufferCount, a_width, a_height, a_format, a_flags);
	}

	HRESULT DX12SwapChain::ResizeBuffersImpl(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags)
	{
		if (!_swapChain) {
			return DXGI_ERROR_INVALID_CALL;
		}
		if (!swap_chain_facade::SupportsResizeBufferCount(
				a_bufferCount, _proxyDesc)) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		constexpr UINT effectiveCount = 2;
		if (a_format != DXGI_FORMAT_UNKNOWN && a_format != DXGI_FORMAT_R8G8B8A8_UNORM) {
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
				targetWidth = static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
			}
			if (!targetHeight) {
				targetHeight = static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));
			}
		}

		const UINT oldWidth = _innerDesc.Width;
		const UINT oldHeight = _innerDesc.Height;
		std::unique_ptr<SharedD3D11D3D12Texture> pendingProxy;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> pendingHudless;
		if (targetWidth != oldWidth || targetHeight != oldHeight) {
			const HRESULT resourceResult =
				CreateDisplayResources(targetWidth, targetHeight, pendingProxy, pendingHudless);
			if (FAILED(resourceResult)) {
				return resourceResult;
			}
		}

		if (!_provider) {
			DisableFrameGeneration("Frame-generation provider is unavailable for resize");
			return E_FAIL;
		}
		const auto releaseResult = render::temporal::QuiesceDrainAndRelease(
			*_provider,
			[&]() {
				const UINT64 d3d11Idle = _nextFenceValue++;
				return SUCCEEDED(_context11->Signal(
						   _fence11.get(), d3d11Idle)) &&
					SUCCEEDED(_queue->Wait(
						_fence12.get(), d3d11Idle)) &&
					SUCCEEDED(WaitForGpu());
			});
		RecordGlobalDrain("resize", releaseResult);
		if (!releaseResult.Succeeded()) {
			DisableFrameGeneration(
				releaseResult.message.empty()
					? "Frame-generation display resources could not be released for resize"
					: releaseResult.message.c_str());
			return E_FAIL;
		}
		_frameGenerationInputsReady = false;
		render::temporal::ResetPresentationProtocol(
			_allocatorFenceValues,
			_inputReuseGate,
			_frameSlot,
			_presentPrepared,
			_preparedFrameGeneration,
			_vendorConsumptionPossible,
			_preparedTransaction);
		for (auto& backBuffer : _backBuffers) {
			backBuffer = nullptr;
		}

		const HRESULT resizeResult = _swapChain->ResizeBuffers(
			effectiveCount,
			targetWidth,
			targetHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			swap_chain_facade::PreservePrivateResizeFlags(
				a_flags, _innerDesc));
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
			const auto restoration =
				render::temporal::RestoreProviderAndPreserveResizeResult(
					*_provider,
					resizeResult,
					oldDescription,
					std::nullopt);
			if (!restoration.providerResult.Succeeded()) {
				DisableFrameGeneration(
					"Frame-generation provider could not be restored after rejected resize");
			}
			render::TemporalPipeline::Get().RequestFrameGenerationReset();
			return restoration.nativeResizeResult;
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
			(!pendingProxy || !pendingHudless[0] || !pendingHudless[1] ||
				targetWidth != resizedDesc.Width || targetHeight != resizedDesc.Height)) {
			const HRESULT resourceResult = CreateDisplayResources(
				resizedDesc.Width,
				resizedDesc.Height,
				pendingProxy,
				pendingHudless);
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
			resizedDesc,
			_proxyDesc,
			_innerDesc,
			_allocatorFenceValues,
			_inputReuseGate,
			_frameSlot,
			_presentPrepared,
			_preparedFrameGeneration,
			_vendorConsumptionPossible,
			_preparedTransaction);

		const HRESULT providerResult = sizeChanged
			? RecreateFrameGenerationResources(
				_innerDesc.Width, _innerDesc.Height)
			: RestoreFrameGenerationProvider(
				_innerDesc.Width, _innerDesc.Height);
		if (FAILED(providerResult)) {
			_published = false;
			DisableFrameGeneration(
				"Frame-generation bridge resources could not be recreated after resize");
			return render::temporal::CompleteNativeResize(
				resizeResult, providerResult);
		}
		if (providerResult == S_FALSE) {
			DisableFrameGeneration(
				"Frame-generation provider could not be restored after resize");
		}
		ClearSharedBuffers();
		render::TemporalPipeline::Get().AdvanceDisplayGeneration(
			_innerDesc.Width, _innerDesc.Height);
		return render::temporal::CompleteNativeResize(
			resizeResult, providerResult);
	}

	HRESULT DX12SwapChain::SetPrivateData(
		REFGUID a_name,
		UINT a_size,
		const void* a_data) noexcept
	{
		return _swapChain ? _swapChain->SetPrivateData(a_name, a_size, a_data) : E_FAIL;
	}

	HRESULT DX12SwapChain::SetPrivateDataInterface(
		REFGUID a_name,
		const IUnknown* a_unknown) noexcept
	{
		return _swapChain ? _swapChain->SetPrivateDataInterface(a_name, a_unknown) : E_FAIL;
	}

	HRESULT DX12SwapChain::GetPrivateData(
		REFGUID a_name,
		UINT* a_size,
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

	HRESULT DX12SwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept
	{
		return _swapChain ? _swapChain->GetFrameStatistics(a_stats) : E_FAIL;
	}

	HRESULT DX12SwapChain::GetLastPresentCount(UINT* a_count) noexcept
	{
		return _swapChain ? _swapChain->GetLastPresentCount(a_count) : E_FAIL;
	}
}
