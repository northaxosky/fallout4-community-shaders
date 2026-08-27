#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

namespace cs::features
{
	class FidelityFX;

	struct SharedD3D11D3D12Texture
	{
		winrt::com_ptr<ID3D11Texture2D> texture11;
		winrt::com_ptr<ID3D11ShaderResourceView> srv11;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav11;
		winrt::com_ptr<ID3D11RenderTargetView> rtv11;
		winrt::com_ptr<ID3D12Resource> resource12;

		static std::unique_ptr<SharedD3D11D3D12Texture> Create(
			ID3D11Device5* a_device11,
			ID3D12Device* a_device12,
			const D3D11_TEXTURE2D_DESC& a_desc);
	};

	class DX12SwapChain;

	class DXGISwapChainProxy final : public IDXGISwapChain
	{
	public:
		explicit DXGISwapChainProxy(DX12SwapChain& a_owner) noexcept;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_iid, void** a_object) noexcept override;
		ULONG STDMETHODCALLTYPE AddRef() noexcept override;
		ULONG STDMETHODCALLTYPE Release() noexcept override;
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a_name, UINT a_size, const void* a_data) noexcept override;
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a_name, const IUnknown* a_unknown) noexcept override;
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data) noexcept override;
		HRESULT STDMETHODCALLTYPE GetParent(REFIID a_iid, void** a_parent) noexcept override;
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID a_iid, void** a_device) noexcept override;
		HRESULT STDMETHODCALLTYPE Present(UINT a_syncInterval, UINT a_flags) noexcept override;
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface) noexcept override;
		HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL a_fullscreen, IDXGIOutput* a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* a_fullscreen, IDXGIOutput** a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override;
		HRESULT STDMETHODCALLTYPE ResizeBuffers(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept override;
		HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** a_output) noexcept override;
		HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept override;
		HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* a_count) noexcept override;

	private:
		std::atomic<ULONG> _references{ 1 };
		DX12SwapChain& _owner;
	};

	class DX12SwapChain
	{
	public:
		DX12SwapChain() = default;
		~DX12SwapChain();

		HRESULT Initialize(
			IDXGIAdapter* a_adapter,
			ID3D11Device* a_device,
			ID3D11DeviceContext* a_context,
			const DXGI_SWAP_CHAIN_DESC& a_desc,
			FidelityFX& a_fidelityFX);
		void Rollback() noexcept;

		[[nodiscard]] IDXGISwapChain* GetProxy() const noexcept;
		[[nodiscard]] bool Owns(IDXGISwapChain* a_swapChain) const noexcept;
		[[nodiscard]] bool IsReady() const noexcept;
		[[nodiscard]] bool IsFrameGenerationReady() const noexcept;
		[[nodiscard]] UINT GetWidth() const noexcept;
		[[nodiscard]] UINT GetHeight() const noexcept;

		[[nodiscard]] SharedD3D11D3D12Texture* GetHudlessTexture() const noexcept;
		[[nodiscard]] SharedD3D11D3D12Texture* GetProxyTexture() const noexcept;
		[[nodiscard]] SharedD3D11D3D12Texture* GetDepthTexture() const noexcept;
		[[nodiscard]] SharedD3D11D3D12Texture* GetMotionTexture() const noexcept;
		[[nodiscard]] ID3D12GraphicsCommandList* GetCommandList() const noexcept;
		[[nodiscard]] ID3D12Device* GetD3D12Device() const noexcept;
		[[nodiscard]] IDXGISwapChain4* GetInnerSwapChain() const noexcept;
		[[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const noexcept;

		void SetFrameGenerationInputsReady(bool a_ready) noexcept;
		void SetOutwardD3D11Device(ID3D11Device* a_device) noexcept;
		void DisableFrameGeneration(const char* a_reason) noexcept;

		HRESULT Present(UINT a_syncInterval, UINT a_flags) noexcept;
		HRESULT GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface) noexcept;
		HRESULT GetDevice(REFIID a_iid, void** a_device) noexcept;
		HRESULT GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept;
		HRESULT ResizeBuffers(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept;

		HRESULT SetPrivateData(REFGUID a_name, UINT a_size, const void* a_data) noexcept;
		HRESULT SetPrivateDataInterface(REFGUID a_name, const IUnknown* a_unknown) noexcept;
		HRESULT GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data) noexcept;
		HRESULT GetParent(REFIID a_iid, void** a_parent) noexcept;
		HRESULT GetContainingOutput(IDXGIOutput** a_output) noexcept;
		HRESULT GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept;
		HRESULT GetLastPresentCount(UINT* a_count) noexcept;

	private:
		HRESULT CreateDevices(IDXGIAdapter* a_adapter, ID3D11Device* a_device, ID3D11DeviceContext* a_context);
		HRESULT CreateSwapChain(IDXGIAdapter* a_adapter, const DXGI_SWAP_CHAIN_DESC& a_desc);
		HRESULT CreateInteropFence();
		HRESULT CreateDisplayResources(
			UINT a_width,
			UINT a_height,
			std::unique_ptr<SharedD3D11D3D12Texture>& a_proxy,
			std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2>& a_hudless);
		HRESULT RecreateDisplayResources(UINT a_width, UINT a_height);
		HRESULT RecreateFrameGenerationResources(UINT a_width, UINT a_height);
		HRESULT RefreshBackBuffers();
		HRESULT WaitForFrame(UINT a_frameIndex) noexcept;
		HRESULT WaitForGpu() noexcept;
		HRESULT PresentImpl(UINT a_syncInterval, UINT a_flags);
		HRESULT ResizeBuffersImpl(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags);
		void ClearSharedBuffers(bool a_clearFrameGenerationInputs = true) noexcept;

		winrt::com_ptr<ID3D11Device5> _device11;
		winrt::com_ptr<ID3D11Device> _outwardDevice11;
		winrt::com_ptr<ID3D11DeviceContext4> _context11;
		winrt::com_ptr<ID3D12Device> _device12;
		winrt::com_ptr<ID3D12CommandQueue> _queue;
		winrt::com_ptr<ID3D12CommandAllocator> _allocators[2];
		winrt::com_ptr<ID3D12GraphicsCommandList> _commandLists[2];
		winrt::com_ptr<ID3D12Resource> _backBuffers[2];
		winrt::com_ptr<ID3D12Fence> _fence12;
		winrt::com_ptr<ID3D11Fence> _fence11;
		winrt::com_ptr<IDXGISwapChain4> _swapChain;
		std::unique_ptr<SharedD3D11D3D12Texture> _proxyBuffer;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> _hudlessBuffers;
		std::unique_ptr<SharedD3D11D3D12Texture> _depthBuffer;
		std::unique_ptr<SharedD3D11D3D12Texture> _motionBuffer;
		std::unique_ptr<DXGISwapChainProxy> _proxy;
		FidelityFX* _fidelityFX = nullptr;
		DXGI_SWAP_CHAIN_DESC _proxyDesc{};
		DXGI_SWAP_CHAIN_DESC1 _innerDesc{};
		UINT _frameIndex = 0;
		UINT64 _nextFenceValue = 1;
		UINT64 _allocatorFenceValues[2]{};
		HANDLE _fenceEvent = nullptr;
		bool _frameGenerationInputsReady = false;
		bool _frameGenerationDisabled = false;
		bool _published = false;
	};
}
