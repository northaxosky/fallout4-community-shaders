#include "DX12SwapChain.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include <REX/TScopeExit.h>
#include "FidelityFX.h"
#include "FidelityFX.h"
#include "Log.h"
#include "Upscaling.h"

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

		void MarkD3D11(ID3D11DeviceContext4* a_context, const wchar_t* a_name) noexcept
		{
			if (!a_context) {
				return;
			}
			winrt::com_ptr<ID3DUserDefinedAnnotation> annotation;
			if (SUCCEEDED(a_context->QueryInterface(IID_PPV_ARGS(annotation.put())))) {
				annotation->SetMarker(a_name);
			}
		}

		void MarkD3D12(ID3D12GraphicsCommandList* a_list, const char* a_name) noexcept
		{
			if (a_list && a_name) {
				a_list->SetMarker(0, a_name, static_cast<UINT>(std::strlen(a_name)));
			}
		}
	}

	std::unique_ptr<SharedD3D11D3D12Texture> SharedD3D11D3D12Texture::Create(
		ID3D11Device5* a_device11,
		ID3D12Device* a_device12,
		const D3D11_TEXTURE2D_DESC& a_desc)
	{
		if (!a_device11 || !a_device12 || !a_desc.Width || !a_desc.Height) {
			return nullptr;
		}

		auto result = std::make_unique<SharedD3D11D3D12Texture>();
		auto desc = a_desc;
		desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		DX::ThrowIfFailed(a_device11->CreateTexture2D(&desc, nullptr, result->texture11.put()));

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
		DX::ThrowIfFailed(openResult);

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
		return result;
	}

	DXGISwapChainProxy::DXGISwapChainProxy(DX12SwapChain& a_owner) noexcept :
		_owner(a_owner)
	{}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID a_iid, void** a_object) noexcept
	{
		if (!a_object) {
			return E_POINTER;
		}
		*a_object = nullptr;
		if (a_iid == __uuidof(IUnknown) || a_iid == __uuidof(IDXGIObject) ||
			a_iid == __uuidof(IDXGIDeviceSubObject) || a_iid == __uuidof(IDXGISwapChain)) {
			*a_object = static_cast<IDXGISwapChain*>(this);
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef() noexcept
	{
		return _references.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release() noexcept
	{
		const auto remaining = _references.fetch_sub(1, std::memory_order_acq_rel) - 1;
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(
		REFGUID a_name,
		UINT a_size,
		const void* a_data) noexcept
	{
		return _owner.SetPrivateData(a_name, a_size, a_data);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(
		REFGUID a_name,
		const IUnknown* a_unknown) noexcept
	{
		return _owner.SetPrivateDataInterface(a_name, a_unknown);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(
		REFGUID a_name,
		UINT* a_size,
		void* a_data) noexcept
	{
		return _owner.GetPrivateData(a_name, a_size, a_data);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(REFIID a_iid, void** a_parent) noexcept
	{
		return _owner.GetParent(a_iid, a_parent);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(REFIID a_iid, void** a_device) noexcept
	{
		return _owner.GetDevice(a_iid, a_device);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT a_syncInterval, UINT a_flags) noexcept
	{
		return _owner.Present(a_syncInterval, a_flags);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(
		UINT a_buffer,
		REFIID a_iid,
		void** a_surface) noexcept
	{
		return _owner.GetBuffer(a_buffer, a_iid, a_surface);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(
		BOOL a_fullscreen,
		IDXGIOutput*) noexcept
	{
		return a_fullscreen ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : S_OK;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(
		BOOL* a_fullscreen,
		IDXGIOutput** a_target) noexcept
	{
		if (a_fullscreen) {
			*a_fullscreen = FALSE;
		}
		if (a_target) {
			*a_target = nullptr;
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept
	{
		return _owner.GetDesc(a_desc);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags) noexcept
	{
		return _owner.ResizeBuffers(a_bufferCount, a_width, a_height, a_format, a_flags);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(const DXGI_MODE_DESC*) noexcept
	{
		return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(IDXGIOutput** a_output) noexcept
	{
		return _owner.GetContainingOutput(a_output);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(
		DXGI_FRAME_STATISTICS* a_stats) noexcept
	{
		return _owner.GetFrameStatistics(a_stats);
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(UINT* a_count) noexcept
	{
		return _owner.GetLastPresentCount(a_count);
	}

	DX12SwapChain::~DX12SwapChain()
	{
		Rollback();
	}

	HRESULT DX12SwapChain::Initialize(
		IDXGIAdapter* a_adapter,
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		const DXGI_SWAP_CHAIN_DESC& a_desc,
		FidelityFX& a_fidelityFX)
	{
		if (_published || !a_device || !a_context || !a_desc.OutputWindow || !a_desc.Windowed ||
			(a_desc.BufferDesc.Format != DXGI_FORMAT_UNKNOWN &&
				a_desc.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)) {
			return E_INVALIDARG;
		}
		Rollback();
		_fidelityFX = &a_fidelityFX;
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
			Rollback();
			return result;
		}

		_proxy = std::make_unique<DXGISwapChainProxy>(*this);
		_published = true;
		ClearSharedBuffers();
		L->info(
			"Published D3D11-facing FidelityFX proxy at {}x{} R8G8B8A8_UNORM",
			_innerDesc.Width,
			_innerDesc.Height);
		return S_OK;
	}

	void DX12SwapChain::Rollback() noexcept
	{
		Upscaling::GetSingleton()->ClearFrameGenerationCaptureState();
		if (_fidelityFX) {
			if (_swapChain && _fidelityFX->IsFrameGenerationContextReady()) {
				_fidelityFX->PresentFrameGeneration(*this, false, 0, 0);
			}
			_fidelityFX->DestroySwapChainContext();
		}
		if (_fenceEvent) {
			CloseHandle(_fenceEvent);
			_fenceEvent = nullptr;
		}
		_proxy.reset();
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
		_fidelityFX = nullptr;
		_frameGenerationInputsReady = false;
		_frameGenerationDisabled = false;
		_published = false;
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

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		DX::ThrowIfFailed(_device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(_queue.put())));
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
		}
		return S_OK;
	}

	HRESULT DX12SwapChain::CreateSwapChain(
		IDXGIAdapter* a_adapter,
		const DXGI_SWAP_CHAIN_DESC& a_desc)
	{
		if (!a_adapter || !_fidelityFX) {
			return E_INVALIDARG;
		}
		winrt::com_ptr<IDXGIFactory4> factory;
		DX::ThrowIfFailed(a_adapter->GetParent(IID_PPV_ARGS(factory.put())));

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
		const HRESULT result = _fidelityFX->CreateSwapChainContext(
			_device12.get(),
			_queue.get(),
			factory.get(),
			a_desc.OutputWindow,
			_innerDesc,
			&swapChain);
		if (SUCCEEDED(result)) {
			_swapChain.attach(swapChain);
			_frameIndex = _swapChain->GetCurrentBackBufferIndex();
			_proxyDesc.BufferDesc.Width = _innerDesc.Width;
			_proxyDesc.BufferDesc.Height = _innerDesc.Height;
			_proxyDesc.BufferDesc.Format = _innerDesc.Format;
			_proxyDesc.BufferCount = 2;
			_proxyDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			_proxyDesc.Windowed = TRUE;
			_proxyDesc.Flags = _innerDesc.Flags;
		}
		return result;
	}

	HRESULT DX12SwapChain::CreateInteropFence()
	{
		DX::ThrowIfFailed(_device12->CreateFence(
			0,
			D3D12_FENCE_FLAG_SHARED,
			IID_PPV_ARGS(_fence12.put())));
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
		auto proxy = SharedD3D11D3D12Texture::Create(_device11.get(), _device12.get(), desc);
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> hudless{
			SharedD3D11D3D12Texture::Create(_device11.get(), _device12.get(), desc),
			SharedD3D11D3D12Texture::Create(_device11.get(), _device12.get(), desc)
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
		auto depth = SharedD3D11D3D12Texture::Create(_device11.get(), _device12.get(), desc);
		desc.Format = DXGI_FORMAT_R16G16_FLOAT;
		auto motion = SharedD3D11D3D12Texture::Create(_device11.get(), _device12.get(), desc);
		if (!depth || !motion ||
			!_fidelityFX->CreateFrameGenerationContext(
				_device12.get(),
				a_width,
				a_height,
				DXGI_FORMAT_R8G8B8A8_UNORM)) {
			_depthBuffer.reset();
			_motionBuffer.reset();
			_frameGenerationDisabled = true;
			L->error("Frame-generation resources were not recreated; real-frame presentation remains active");
			return S_OK;
		}
		_depthBuffer = std::move(depth);
		_motionBuffer = std::move(motion);
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

	bool DX12SwapChain::Owns(IDXGISwapChain* a_swapChain) const noexcept
	{
		return _proxy && a_swapChain == _proxy.get();
	}

	bool DX12SwapChain::IsReady() const noexcept
	{
		return _published && _swapChain && _proxyBuffer && _context11 && _queue &&
			_fence11 && _fence12 && _fenceEvent &&
			_allocators[0] && _allocators[1] &&
			_commandLists[0] && _commandLists[1] &&
			_backBuffers[0] && _backBuffers[1];
	}

	bool DX12SwapChain::IsFrameGenerationReady() const noexcept
	{
		return IsReady() && !_frameGenerationDisabled && _fidelityFX &&
			_fidelityFX->IsFrameGenerationContextReady() &&
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

	SharedD3D11D3D12Texture* DX12SwapChain::GetHudlessTexture() const noexcept
	{
		return _frameIndex < _hudlessBuffers.size()
			? _hudlessBuffers[_frameIndex].get()
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
		return _commandLists[_frameIndex].get();
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
		Upscaling::GetSingleton()->ClearFrameGenerationCaptureState();
		if (_fidelityFX) {
			_fidelityFX->RequestFrameGenerationReset();
		}
		if (firstFailure) {
			Upscaling::GetSingleton()->RecordFrameGenerationFailure();
		}
		try {
			L->error("Frame generation disabled: {}", a_reason ? a_reason : "unknown failure");
		} catch (...) {
		}
	}

	HRESULT DX12SwapChain::WaitForFrame(UINT a_frameIndex) noexcept
	{
		const auto value = _allocatorFenceValues[a_frameIndex];
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
		if (a_flags & DXGI_PRESENT_TEST) {
			return _swapChain ? _swapChain->Present(a_syncInterval, a_flags) : E_FAIL;
		}
		const REX::TScopeExit clearCapture{ []() noexcept {
			Upscaling::GetSingleton()->ClearFrameGenerationCaptureState();
		} };
		try {
			return PresentImpl(a_syncInterval, a_flags);
		} catch (const std::exception& e) {
			DisableFrameGeneration(e.what());
			return E_FAIL;
		} catch (...) {
			DisableFrameGeneration("unhandled presentation failure");
			return E_FAIL;
		}
	}

	HRESULT DX12SwapChain::PresentImpl(UINT a_syncInterval, UINT a_flags)
	{
		if (!IsReady()) {
			return _swapChain ? _swapChain->Present(a_syncInterval, a_flags)
							  : DXGI_ERROR_INVALID_CALL;
		}

		auto* upscaling = Upscaling::GetSingleton();
		MarkD3D11(_context11.get(), L"FG_D3D11ProductionComplete");
		const UINT64 d3d11Ready = _nextFenceValue++;
		DX::ThrowIfFailed(_context11->Signal(_fence11.get(), d3d11Ready));
		DX::ThrowIfFailed(_queue->Wait(_fence12.get(), d3d11Ready));
		DX::ThrowIfFailed(WaitForFrame(_frameIndex));
		DX::ThrowIfFailed(_allocators[_frameIndex]->Reset());
		DX::ThrowIfFailed(_commandLists[_frameIndex]->Reset(
			_allocators[_frameIndex].get(),
			nullptr));

		auto* commandList = _commandLists[_frameIndex].get();
		MarkD3D12(commandList, "FG_CopyRealFrame");
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

		const auto [renderWidth, renderHeight] = upscaling->GetRenderSize();
		const bool requested =
			_frameGenerationInputsReady && !_frameGenerationDisabled &&
			upscaling->ShouldUseFrameGenerationThisFrame();
		MarkD3D12(commandList, "FG_ConfigurePrepare");
		if (!_fidelityFX->PresentFrameGeneration(
				*this,
				requested,
				renderWidth,
				renderHeight) && requested) {
			DisableFrameGeneration("FidelityFX configure or prepare failed");
		}

		DX::ThrowIfFailed(commandList->Close());
		ID3D12CommandList* lists[] = { commandList };
		_queue->ExecuteCommandLists(1, lists);
		const HRESULT presentResult = _swapChain->Present(a_syncInterval, a_flags);

		const UINT64 d3d12Done = _nextFenceValue++;
		const HRESULT signalResult = _queue->Signal(_fence12.get(), d3d12Done);
		if (FAILED(signalResult)) {
			_published = false;
			DisableFrameGeneration("D3D12 completion signal failed");
			return FAILED(presentResult) ? presentResult : signalResult;
		}
		_allocatorFenceValues[_frameIndex] = d3d12Done;
		const HRESULT waitResult = _context11->Wait(_fence11.get(), d3d12Done);
		if (FAILED(waitResult)) {
			WaitForFrame(_frameIndex);
			_published = false;
			DisableFrameGeneration("D3D11 completion wait failed");
			return FAILED(presentResult) ? presentResult : waitResult;
		}
		_frameIndex = _swapChain->GetCurrentBackBufferIndex();
		_frameGenerationInputsReady = false;
		ClearSharedBuffers(false);
		if (presentResult != S_OK) {
			_fidelityFX->RequestFrameGenerationReset();
		}
		return presentResult;
	}

	void DX12SwapChain::ClearSharedBuffers(bool a_clearFrameGenerationInputs) noexcept
	{
		if (!_context11) {
			return;
		}
		const float clear[4]{};
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

	HRESULT DX12SwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc = _proxyDesc;
		return S_OK;
	}

	HRESULT DX12SwapChain::ResizeBuffers(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags) noexcept
	{
		Upscaling::GetSingleton()->ClearFrameGenerationCaptureState();
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
		const UINT effectiveCount = a_bufferCount ? a_bufferCount : 2;
		if (effectiveCount != 2) {
			return DXGI_ERROR_UNSUPPORTED;
		}
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

		if (_fidelityFX->IsFrameGenerationContextReady()) {
			_fidelityFX->PresentFrameGeneration(*this, false, 0, 0);
		}
		if (!_fidelityFX->WaitForPresents()) {
			DisableFrameGeneration("FidelityFX presents did not quiesce for resize");
			return E_FAIL;
		}
		_frameGenerationInputsReady = false;

		const UINT64 d3d11Idle = _nextFenceValue++;
		DX::ThrowIfFailed(_context11->Signal(_fence11.get(), d3d11Idle));
		DX::ThrowIfFailed(_queue->Wait(_fence12.get(), d3d11Idle));
		DX::ThrowIfFailed(WaitForGpu());
		for (auto& backBuffer : _backBuffers) {
			backBuffer = nullptr;
		}

		const HRESULT resizeResult = _swapChain->ResizeBuffers(
			effectiveCount,
			targetWidth,
			targetHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			a_flags);
		if (FAILED(resizeResult)) {
			const HRESULT refreshResult = RefreshBackBuffers();
			if (FAILED(refreshResult)) {
				_published = false;
			}
			_fidelityFX->RequestFrameGenerationReset();
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

		_innerDesc = resizedDesc;
		if (sizeChanged) {
			_proxyBuffer = std::move(pendingProxy);
			_hudlessBuffers = std::move(pendingHudless);
			RecreateFrameGenerationResources(_innerDesc.Width, _innerDesc.Height);
		} else {
			_frameGenerationInputsReady = false;
		}
		_proxyDesc.BufferDesc.Width = _innerDesc.Width;
		_proxyDesc.BufferDesc.Height = _innerDesc.Height;
		_proxyDesc.BufferDesc.Format = _innerDesc.Format;
		_proxyDesc.BufferCount = 2;
		_proxyDesc.Flags = _innerDesc.Flags;
		_allocatorFenceValues[0] = 0;
		_allocatorFenceValues[1] = 0;
		ClearSharedBuffers();
		_fidelityFX->RequestFrameGenerationReset();
		return S_OK;
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
