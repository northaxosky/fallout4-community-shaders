#include "DX11Hooks.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "Upscaling.h"
#include "DX12SwapChain.h"
#include "FidelityFX.h"
#include "Streamline.h"
#include <nvsdk_ngx.h>

#include "ENB/ENBSeriesAPI.h"
#include "XeSSFG.h"
#include "UICompositor.h"

namespace cs::features::FrameGeneration
{
	bool enbLoaded = false;


decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;
decltype(&IDXGIFactory::CreateSwapChain) ptrCreateSwapChain;

// Real swap chain hooks for DLSS-G UI compositing
using PFN_CreateSwapChainForHwnd = HRESULT(WINAPI*)(IDXGIFactory2*, IUnknown*, HWND,
	const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
static PFN_CreateSwapChainForHwnd ptrCreateSwapChainForHwnd = nullptr;

using PFN_RealPresent = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
static PFN_RealPresent ptrRealPresent = nullptr;

static HRESULT WINAPI hk_RealPresent(IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
{
	UICompositor::GetSingleton()->CompositeUI(This);
	return ptrRealPresent(This, SyncInterval, Flags);
}

static HRESULT WINAPI hk_CreateSwapChainForHwnd(IDXGIFactory2* This, IUnknown* pDevice, HWND hWnd,
	const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
	IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
	REX::INFO("[DLSSG-UI] CreateSwapChainForHwnd hook fired (factory={:#x})", (uintptr_t)This);

	HRESULT hr = ptrCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
	if (FAILED(hr) || !ppSwapChain || !*ppSwapChain)
		return hr;

	REX::INFO("[DLSSG-UI] Real swap chain created: {:#x}, format={}, {}x{}",
		(uintptr_t)*ppSwapChain, pDesc ? (int)pDesc->Format : -1,
		pDesc ? pDesc->Width : 0, pDesc ? pDesc->Height : 0);

	// Hook Present (vtable index 8) on the real swap chain
	IDXGISwapChain1* realSC = *ppSwapChain;
	*(uintptr_t*)&ptrRealPresent = Detours::X64::DetourClassVTable(
		*(uintptr_t*)realSC, &hk_RealPresent, 8);

	// Store in compositor
	IDXGISwapChain4* realSC4 = nullptr;
	realSC->QueryInterface(IID_PPV_ARGS(&realSC4));

	ID3D12CommandQueue* cmdQueue = nullptr;
	pDevice->QueryInterface(IID_PPV_ARGS(&cmdQueue));

	if (realSC4 && cmdQueue) {
		UICompositor::GetSingleton()->SetRealSwapChain(realSC4, cmdQueue);
		REX::INFO("[DLSSG-UI] Real swap chain Present hooked (vtable index 8)");
	} else {
		REX::WARN("[DLSSG-UI] Failed to QI swap chain ({:#x}) or command queue ({:#x})",
			(uintptr_t)realSC4, (uintptr_t)cmdQueue);
	}

	if (realSC4) realSC4->Release();
	if (cmdQueue) cmdQueue->Release();

	return hr;
}

HRESULT WINAPI hk_IDXGIFactory_CreateSwapChain(IDXGIFactory2* This, _In_ ID3D11Device* a_device, _In_ DXGI_SWAP_CHAIN_DESC* pDesc, _COM_Outptr_ IDXGISwapChain** ppSwapChain)
{
	// ENB path: ENB's wrapped factory calls CreateSwapChain — we intercept to insert our D3D12 proxy
	auto upscaling = Upscaling::GetSingleton();

	IDXGIDevice* dxgiDevice = nullptr;
	DX::ThrowIfFailed(a_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

	IDXGIAdapter* adapter = nullptr;
	DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter));
	dxgiDevice->Release();

	auto proxy = DX12SwapChain::GetSingleton();

	proxy->SetD3D11Device(a_device);

	ID3D11DeviceContext* context;
	a_device->GetImmediateContext(&context);
	proxy->SetD3D11DeviceContext(context);
	context->Release();

	// For DLSS-G: init Streamline BEFORE D3D12 device
	if (upscaling->activeFrameGenType == Upscaling::FrameGenType::kDLSSG) {
		auto dlssg = StreamlineFG::GetSingleton();
		dlssg->InitStreamline();
	}

	proxy->CreateD3D12Device(adapter);
	adapter->Release();

	// XeSS-FG: create contexts after D3D12 device
	if (upscaling->activeFrameGenType == Upscaling::FrameGenType::kXeSSFG) {
		auto xess = XeSSFG::GetSingleton();
		if (!xess->CreateContexts(proxy->d3d12Device.get())) {
			REX::WARN("[FG] XeSS-FG context creation failed (ENB path), falling back to FSR3");
			upscaling->activeFrameGenType = Upscaling::FrameGenType::kFSR3;
		}
	}

	// DLSS-G: upgrade device+factory via Streamline
	IDXGIFactory5* factory = (IDXGIFactory5*)This;
	if (upscaling->activeFrameGenType == Upscaling::FrameGenType::kDLSSG) {
		auto dlssg = StreamlineFG::GetSingleton();

		// Hook CreateSwapChainForHwnd on original factory BEFORE Streamline wraps it (ENB path)
		if (!ptrCreateSwapChainForHwnd) {
			*(uintptr_t*)&ptrCreateSwapChainForHwnd = Detours::X64::DetourClassVTable(
				*(uintptr_t*)factory, &hk_CreateSwapChainForHwnd, 15);
			REX::INFO("[DLSSG-UI] Hooked IDXGIFactory2::CreateSwapChainForHwnd (vtable 15) on original factory (ENB path)");
		}

		ID3D12Device* rawDevice = proxy->d3d12Device.get();
		dlssg->slUpgradeInterface((void**)&rawDevice);
		proxy->d3d12Device.copy_from(rawDevice);

		IDXGIFactory* rawFactory = (IDXGIFactory*)factory;
		dlssg->slUpgradeInterface((void**)&rawFactory);
		factory = (IDXGIFactory5*)rawFactory;

		dlssg->SetD3DDevice(proxy->d3d12Device.get());
	}

	proxy->CreateD3D12CommandQueues();
	proxy->CreateSwapChain(factory, *pDesc);

	if (upscaling->activeFrameGenType == Upscaling::FrameGenType::kDLSSG) {
		auto dlssg = StreamlineFG::GetSingleton();
		if (!dlssg->CheckAndEnableDLSSG()) {
			REX::WARN("[FG] DLSS-G enable failed, falling back to FSR3");
			upscaling->activeFrameGenType = Upscaling::FrameGenType::kFSR3;
		}
	}

	proxy->CreateInterop();

	*ppSwapChain = proxy->GetSwapChainProxy();

	return S_OK;
}

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	auto upscaling = Upscaling::GetSingleton();

	if (pSwapChainDesc->Windowed) {
		// Log GPU info
		if (pAdapter) {
			DXGI_ADAPTER_DESC adapterDesc;
			if (SUCCEEDED(pAdapter->GetDesc(&adapterDesc))) {
				std::string gpuName;
				for (int i = 0; i < 128 && adapterDesc.Description[i]; i++)
					gpuName += static_cast<char>(adapterDesc.Description[i]);
				REX::INFO("[FG] GPU: {} (VRAM: {}MB, VendorId: {:#x}, DeviceId: {:#x})",
					gpuName, adapterDesc.DedicatedVideoMemory / (1024 * 1024),
					adapterDesc.VendorId, adapterDesc.DeviceId);
			}
		}

		REX::INFO("[Frame Generation] Frame Generation enabled, using D3D12 proxy");

		auto fidelityFX = FidelityFX::GetSingleton();

		bool hasBackend = fidelityFX->module;

		// For DLSS-G, tentatively enable — actual init after D3D12 device creation
		if (upscaling->settings.frameGenType == 1) {
			upscaling->activeFrameGenType = Upscaling::FrameGenType::kDLSSG;
			hasBackend = true;
		} else if (upscaling->settings.frameGenType == 2) {
			auto xess = XeSSFG::GetSingleton();
			if (xess->fgModule && xess->xellModule) {
				upscaling->activeFrameGenType = Upscaling::FrameGenType::kXeSSFG;
				hasBackend = true;
			}
		}

		if (hasBackend) {
			upscaling->d3d12Interop = true;
			upscaling->refreshRate = Upscaling::GetRefreshRate(pSwapChainDesc->OutputWindow);

			IDXGIFactory4* dxgiFactory;
			pAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

			const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
			pFeatureLevels = &featureLevel;
			FeatureLevels = 1;

			if (enbLoaded) {
				*(uintptr_t*)&ptrCreateSwapChain = Detours::X64::DetourClassVTable(*(uintptr_t*)dxgiFactory, &hk_IDXGIFactory_CreateSwapChain, 10);
			}
			else {
				DX::ThrowIfFailed(D3D11CreateDevice(
					pAdapter,
					DriverType,
					Software,
					Flags,
					pFeatureLevels,
					FeatureLevels,
					SDKVersion,
					ppDevice,
					pFeatureLevel,
					ppImmediateContext));

				IDXGIDevice* dxgiDevice = nullptr;
				DX::ThrowIfFailed((*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

				IDXGIAdapter* adapter = nullptr;
				DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter));
				dxgiDevice->Release();

				auto proxy = DX12SwapChain::GetSingleton();

				proxy->SetD3D11Device(*ppDevice);

				ID3D11DeviceContext* context;
				(*ppDevice)->GetImmediateContext(&context);
				proxy->SetD3D11DeviceContext(context);
				context->Release();

				// For DLSS-G: init Streamline BEFORE D3D12 device so plugins see the device
				if (upscaling->activeFrameGenType == Upscaling::FrameGenType::kDLSSG) {
					auto dlssg = StreamlineFG::GetSingleton();
					dlssg->InitStreamline();
				}

				proxy->CreateD3D12Device(adapter);
				adapter->Release();

				// XeSS-FG: create contexts after D3D12 device, no device/factory upgrade needed
				if (upscaling->activeFrameGenType == Upscaling::FrameGenType::kXeSSFG) {
					auto xess = XeSSFG::GetSingleton();
					if (!xess->CreateContexts(proxy->d3d12Device.get())) {
						REX::WARN("[FG] XeSS-FG context creation failed, falling back to FSR3");
						upscaling->activeFrameGenType = Upscaling::FrameGenType::kFSR3;
					}
				}

				// DLSS-G: upgrade device+factory via Streamline, then set device
				// slSetD3DDevice must come before proxy API calls trigger plugin hooks
				if (upscaling->activeFrameGenType == Upscaling::FrameGenType::kDLSSG) {
					auto dlssg = StreamlineFG::GetSingleton();

					// Hook CreateSwapChainForHwnd on the ORIGINAL factory BEFORE Streamline wraps it
					// Streamline's factory wrapper will call through to the original → our hook fires
					// This lets us capture the real swap chain and hook its Present for UI compositing
					if (!ptrCreateSwapChainForHwnd) {
						*(uintptr_t*)&ptrCreateSwapChainForHwnd = Detours::X64::DetourClassVTable(
							*(uintptr_t*)dxgiFactory, &hk_CreateSwapChainForHwnd, 15);
						REX::INFO("[DLSSG-UI] Hooked IDXGIFactory2::CreateSwapChainForHwnd (vtable 15) on original factory");
					}

					ID3D12Device* rawDevice = proxy->d3d12Device.get();
					dlssg->slUpgradeInterface((void**)&rawDevice);
					proxy->d3d12Device.copy_from(rawDevice);

					IDXGIFactory* rawFactory = (IDXGIFactory*)dxgiFactory;
					dlssg->slUpgradeInterface((void**)&rawFactory);
					dxgiFactory = (IDXGIFactory4*)rawFactory;

					dlssg->SetD3DDevice(proxy->d3d12Device.get());
				}

				proxy->CreateD3D12CommandQueues();
				proxy->CreateSwapChain((IDXGIFactory5*)dxgiFactory, *pSwapChainDesc);

				if (upscaling->activeFrameGenType == Upscaling::FrameGenType::kDLSSG) {
					auto dlssg = StreamlineFG::GetSingleton();

					if (!dlssg->CheckAndEnableDLSSG()) {
						REX::WARN("[FG] DLSS-G enable failed, falling back to FSR3");
						upscaling->activeFrameGenType = Upscaling::FrameGenType::kFSR3;
					}
				}

				proxy->CreateInterop();

				*ppSwapChain = proxy->GetSwapChainProxy();
				
				return S_OK;
			}

		} else {
			REX::WARN("[Frame Generation] No frame generation backend available, skipping proxy");
		}
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChain(
		pAdapter,
		DriverType,
		Software,
		Flags,
		pFeatureLevels,
		FeatureLevels,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	return ret;
}

void DX11Hooks::Install()
{
	if (ENB_API::RequestENBAPI()) {
		REX::INFO("ENB detected, using alternative swap chain hook");
		enbLoaded = true;
	} else {
		REX::INFO("ENB not detected, using standard swap chain hook");
	}

	auto upscaling = Upscaling::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	// Always load FidelityFX as fallback
	fidelityFX->LoadFFX();

	if (upscaling->settings.frameGenType == 1) {
		REX::INFO("[FG] DLSS-G requested, loading Streamline interposer");
		auto dlssg = StreamlineFG::GetSingleton();
		dlssg->LoadInterposer();
	} else if (upscaling->settings.frameGenType == 2) {
		REX::INFO("[FG] XeSS-FG requested, loading XeSS libraries");
		auto xess = XeSSFG::GetSingleton();
		xess->LoadLibraries();
	}

	uintptr_t moduleBase = (uintptr_t)GetModuleHandle(nullptr);

	(uintptr_t&)ptrD3D11CreateDeviceAndSwapChain = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uintptr_t)hk_D3D11CreateDeviceAndSwapChain);
}

}
