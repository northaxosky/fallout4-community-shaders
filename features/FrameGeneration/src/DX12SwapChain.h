#pragma once

#include <Windows.Foundation.h>
#include <stdio.h>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include <d3d11_4.h>
#include <d3d12.h>

#include "Buffer.h"

namespace cs::features::FrameGeneration
{

class WrappedResource
{
public:
	WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device);
	~WrappedResource();

	WrappedResource(const WrappedResource&) = delete;
	WrappedResource& operator=(const WrappedResource&) = delete;

	ID3D11Texture2D* resource11 = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11UnorderedAccessView* uav = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	winrt::com_ptr<ID3D12Resource> resource;
};

struct DXGISwapChainProxy : IDXGISwapChain4
{
public:
	DXGISwapChainProxy(IDXGISwapChain4* a_swapChain);

	IDXGISwapChain4* swapChain;

	/****IUnknown****/
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
	virtual ULONG STDMETHODCALLTYPE AddRef() override;
	virtual ULONG STDMETHODCALLTYPE Release() override;

	/****IDXGIObject****/
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData) override;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown) override;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData) override;
	virtual HRESULT STDMETHODCALLTYPE GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent) override;

	/****IDXGIDeviceSubObject****/
	virtual HRESULT STDMETHODCALLTYPE GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice) override;

	/****IDXGISwapChain****/
	virtual HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
	virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface) override;
	virtual HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget) override;
	virtual HRESULT STDMETHODCALLTYPE GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget) override;
	virtual HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc) override;
	virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
	virtual HRESULT STDMETHODCALLTYPE ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters) override;
	virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput) override;
	virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats) override;
	virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(_Out_ UINT* pLastPresentCount) override;

	/****IDXGISwapChain1****/
	virtual HRESULT STDMETHODCALLTYPE GetDesc1(_Out_ DXGI_SWAP_CHAIN_DESC1* pDesc) override;
	virtual HRESULT STDMETHODCALLTYPE GetFullscreenDesc(_Out_ DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override;
	virtual HRESULT STDMETHODCALLTYPE GetHwnd(_Out_ HWND* pHwnd) override;
	virtual HRESULT STDMETHODCALLTYPE GetCoreWindow(_In_ REFIID refiid, _COM_Outptr_ void** ppUnk) override;
	virtual HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags, _In_ const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
	virtual BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override;
	virtual HRESULT STDMETHODCALLTYPE GetRestrictToOutput(_Out_ IDXGIOutput** ppRestrictToOutput) override;
	virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor(_In_ const DXGI_RGBA* pColor) override;
	virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor(_Out_ DXGI_RGBA* pColor) override;
	virtual HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) override;
	virtual HRESULT STDMETHODCALLTYPE GetRotation(_Out_ DXGI_MODE_ROTATION* pRotation) override;

	/****IDXGISwapChain2****/
	virtual HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override;
	virtual HRESULT STDMETHODCALLTYPE GetSourceSize(_Out_ UINT* pWidth, _Out_ UINT* pHeight) override;
	virtual HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override;
	virtual HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(_Out_ UINT* pMaxLatency) override;
	virtual HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override;
	virtual HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) override;
	virtual HRESULT STDMETHODCALLTYPE GetMatrixTransform(_Out_ DXGI_MATRIX_3X2_F* pMatrix) override;

	/****IDXGISwapChain3****/
	virtual UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override;
	virtual HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, _Out_ UINT* pColorSpaceSupport) override;
	virtual HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
	virtual HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, _In_reads_(BufferCount) const UINT* pCreationNodeMask, _In_reads_(BufferCount) IUnknown* const* ppPresentQueue) override;

	/****IDXGISwapChain4****/
	virtual HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, _In_reads_opt_(Size) void* pMetaData) override;
};

class DX12SwapChain
{
public:
	static DX12SwapChain* GetSingleton()
	{
		static DX12SwapChain singleton;
		return &singleton;
	}

	winrt::com_ptr<ID3D12Device> d3d12Device;
	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	winrt::com_ptr<ID3D12CommandAllocator> commandAllocators[2];
	winrt::com_ptr<ID3D12GraphicsCommandList4> commandLists[2];

	IDXGISwapChain4* swapChain;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc;

	WrappedResource* swapChainBufferProxy;

	WrappedResource* swapChainBufferWrapped[2];

	winrt::com_ptr<ID3D11Device5> d3d11Device;
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context;

	winrt::com_ptr<ID3D11Fence> d3d11Fence;
	winrt::com_ptr<ID3D12Fence> d3d12Fence;

	winrt::com_ptr<ID3D12Resource> swapChainBuffers[2];

	UINT frameIndex = 0;
	UINT64 fenceValue = 0;

	LARGE_INTEGER qpf;

	DXGISwapChainProxy* swapChainProxy = nullptr;

	void CreateD3D12Device(IDXGIAdapter* a_adapter);
	void CreateD3D12CommandQueues();
	void CreateSwapChain(IDXGIFactory5* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC swapChainDesc);
	void CreateSwapChainFSR3(IDXGIFactory5* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc);
	void CreateSwapChainDLSSG(IDXGIFactory5* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc);
	bool CreateSwapChainXeSS(IDXGIFactory5* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc);

	void CreateInterop();
	void RecreateWrappedBuffers();
	void OnPreResize();
	void WaitForGPU();

	DXGISwapChainProxy* GetSwapChainProxy();
	void SetD3D11Device(ID3D11Device* a_d3d11Device);
	void SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context);

	HRESULT GetBuffer(void** ppSurface);
	HRESULT Present(UINT SyncInterval, UINT Flags);
	HRESULT GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice);
};

}
