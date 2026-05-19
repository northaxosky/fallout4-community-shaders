#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#include <xefg_swapchain.h>
#include <xefg_swapchain_d3d12.h>
#include <xell.h>
#include <xell_d3d12.h>

namespace cs::features::framegeneration
{

class XeSSFG
{
public:
	static XeSSFG* GetSingleton()
	{
		static XeSSFG singleton;
		return &singleton;
	}

	bool LoadLibraries();
	bool CreateContexts(ID3D12Device* a_device);
	bool InitSwapChain(ID3D12CommandQueue* a_cmdQueue, IDXGIFactory5* a_factory,
		const DXGI_SWAP_CHAIN_DESC1& a_desc, HWND a_hwnd, IDXGISwapChain4** a_outSwapChain);

	void BeginFrame(uint32_t a_frameId);
	void SetMarker(xell_latency_marker_type_t a_marker, uint32_t a_frameId);
	void TagResources(uint32_t a_frameId,
		ID3D12GraphicsCommandList* a_cmdList,
		ID3D12Resource* a_depth,
		ID3D12Resource* a_motionVectors,
		ID3D12Resource* a_hudlessColor,
		float2 a_screenSize, float2 a_jitter,
		float a_frameTimeDelta,
		const float* a_viewMatrix, const float* a_projMatrix,
		bool a_reset);
	void SetEnabled(uint32_t a_enabled);

	bool initialized = false;
	HMODULE fgModule = nullptr;
	HMODULE xellModule = nullptr;
	xefg_swapchain_handle_t xefgCtx = nullptr;
	xell_context_handle_t xellCtx = nullptr;

private:
	// XeLL function pointers (resolved via GetProcAddress)
	decltype(&xellD3D12CreateContext) pfn_xellD3D12CreateContext = nullptr;
	decltype(&xellDestroyContext) pfn_xellDestroyContext = nullptr;
	decltype(&xellSetSleepMode) pfn_xellSetSleepMode = nullptr;
	decltype(&xellSleep) pfn_xellSleep = nullptr;
	decltype(&xellAddMarkerData) pfn_xellAddMarkerData = nullptr;
	decltype(&xellSetLoggingCallback) pfn_xellSetLoggingCallback = nullptr;

	// XeSS-FG function pointers (resolved via GetProcAddress)
	decltype(&xefgSwapChainD3D12CreateContext) pfn_xefgSwapChainD3D12CreateContext = nullptr;
	decltype(&xefgSwapChainSetLatencyReduction) pfn_xefgSwapChainSetLatencyReduction = nullptr;
	decltype(&xefgSwapChainSetLoggingCallback) pfn_xefgSwapChainSetLoggingCallback = nullptr;
	decltype(&xefgSwapChainD3D12InitFromSwapChainDesc) pfn_xefgSwapChainD3D12InitFromSwapChainDesc = nullptr;
	decltype(&xefgSwapChainD3D12GetSwapChainPtr) pfn_xefgSwapChainD3D12GetSwapChainPtr = nullptr;
	decltype(&xefgSwapChainSetEnabled) pfn_xefgSwapChainSetEnabled = nullptr;
	decltype(&xefgSwapChainGetProperties) pfn_xefgSwapChainGetProperties = nullptr;
	decltype(&xefgSwapChainD3D12TagFrameResource) pfn_xefgSwapChainD3D12TagFrameResource = nullptr;
	decltype(&xefgSwapChainTagFrameConstants) pfn_xefgSwapChainTagFrameConstants = nullptr;
	decltype(&xefgSwapChainSetPresentId) pfn_xefgSwapChainSetPresentId = nullptr;
	decltype(&xefgSwapChainDestroy) pfn_xefgSwapChainDestroy = nullptr;
};

}
