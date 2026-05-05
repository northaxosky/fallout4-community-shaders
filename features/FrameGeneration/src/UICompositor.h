#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>
#include <atomic>

namespace cs::features::FrameGeneration
{

class UICompositor
{
public:
	static UICompositor* GetSingleton()
	{
		static UICompositor singleton;
		return &singleton;
	}

	// Called from factory hook when real swap chain is created
	void SetRealSwapChain(IDXGISwapChain4* a_swapChain, ID3D12CommandQueue* a_queue);

	// Called from real swap chain Present hook - currently a no-op since DLSS-G presents the full backbuffer.
	void CompositeUI(IDXGISwapChain* a_swapChain);

	// Lazy-opens a D3D12 view of UITextureIsolation's shared D3D11 texture; nullptr if unavailable.
	ID3D12Resource* GetOrOpenPrivateUIResource12();

private:
	UICompositor();
	~UICompositor();

	void InitResources();

	// Real swap chain reference
	IDXGISwapChain4* realSwapChain = nullptr;
	ID3D12CommandQueue* commandQueue = nullptr;
	ID3D12Device* device = nullptr;

	// PSO
	winrt::com_ptr<ID3D12RootSignature> rootSignature;
	winrt::com_ptr<ID3D12PipelineState> pipelineState;

	// Descriptor heaps
	winrt::com_ptr<ID3D12DescriptorHeap> srvHeap;  // GPU-visible, CBV_SRV_UAV
	winrt::com_ptr<ID3D12DescriptorHeap> rtvHeap;   // CPU-only, RTV
	UINT srvIncSize = 0;
	UINT rtvIncSize = 0;
	UINT srvRingIndex = 0;
	UINT rtvRingIndex = 0;

	// Command infrastructure (separate from main render path)
	winrt::com_ptr<ID3D12CommandAllocator> cmdAllocators[2];
	winrt::com_ptr<ID3D12GraphicsCommandList> cmdLists[2];
	winrt::com_ptr<ID3D12Fence> fence;
	UINT64 fenceValues[2] = { 0, 0 };
	HANDLE fenceEvent = nullptr;

	// State
	bool initialized = false;
	CRITICAL_SECTION initCS;

	winrt::com_ptr<ID3D12Resource> privateUIResource12;
	void*                          lastSeenUITexture = nullptr;
	UINT                           lastSeenWidth     = 0;
	UINT                           lastSeenHeight    = 0;
	DXGI_FORMAT                    lastSeenFormat    = DXGI_FORMAT_UNKNOWN;

	// Swap chain info
	DXGI_FORMAT backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	UINT backbufferCount = 2;
};

}
