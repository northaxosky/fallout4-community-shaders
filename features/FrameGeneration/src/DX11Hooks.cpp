#include "DX11Hooks.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "Upscaling.h"
#include "DX12SwapChain.h"
#include "FidelityFX.h"
#include "Streamline.h"
#include <nvsdk_ngx.h>

#include "Env.h"
#include "Log.h"
#include "Menu.h"
#include "XeSSFG.h"
#include "UICompositor.h"

namespace cs::features::FrameGeneration
{
	namespace { auto* L = cs::log::Get("cs.feature.framegen.dx11"); }


decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;
decltype(&IDXGIFactory::CreateSwapChain) ptrCreateSwapChain;
decltype(&CreateDXGIFactory1) ptrCreateDXGIFactory1;

// Boot-time gate against double-install. Both call sites run on the main thread during init.
static std::atomic<bool> slot10Installed{ false };

HRESULT WINAPI hk_IDXGIFactory_CreateSwapChain(IDXGIFactory2* This, _In_ ID3D11Device* a_device, _In_ DXGI_SWAP_CHAIN_DESC* pDesc, _COM_Outptr_ IDXGISwapChain** ppSwapChain);

static bool InstallSlot10Once(IDXGIFactory* a_factory)
{
	bool expected = false;
	if (!slot10Installed.compare_exchange_strong(expected, true))
		return false;
	*(uintptr_t*)&ptrCreateSwapChain = Detours::X64::DetourClassVTable(
		*(uintptr_t*)a_factory, &hk_IDXGIFactory_CreateSwapChain, 10);
	return true;
}

// Catches factories created outside the engine path (ReShade, overlays, ENB late-init).
static HRESULT WINAPI hk_CreateDXGIFactory1(REFIID riid, void** ppFactory)
{
	HRESULT hr = ptrCreateDXGIFactory1(riid, ppFactory);
	if (!SUCCEEDED(hr) || !ppFactory || !*ppFactory)
		return hr;

	// Only known IDXGIFactory* IIDs share slot-10 = CreateSwapChain. Bail otherwise.
	const bool isFactoryIid =
		riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) ||
		riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
		riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) ||
		riid == __uuidof(IDXGIFactory6) || riid == __uuidof(IDXGIFactory7);
	if (!isFactoryIid)
		return hr;

	auto factory = static_cast<IDXGIFactory*>(*ppFactory);
	if (InstallSlot10Once(factory))
		L->info("CreateDXGIFactory1 returned factory {:#x}; slot-10 installed (orig {:#x})",
			reinterpret_cast<uintptr_t>(factory), *(uintptr_t*)&ptrCreateSwapChain);
	return hr;
}

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
	cs::log::Get("cs.feature.framegen.ui")->info("CreateSwapChainForHwnd hook fired (factory={:#x})", (uintptr_t)This);

	HRESULT hr = ptrCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
	if (FAILED(hr) || !ppSwapChain || !*ppSwapChain)
		return hr;

	cs::log::Get("cs.feature.framegen.ui")->info("Real swap chain created: {:#x}, format={}, {}x{}",
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
		cs::log::Get("cs.feature.framegen.ui")->info("Real swap chain Present hooked (vtable index 8)");

		static bool warnedDLSSGMenu = false;
		if (!warnedDLSSGMenu) {
			cs::log::Get("cs.menu")->warn("DLSS-G active - menu drawn on proxy backbuffer; will warp until UICompositor integration lands");
			warnedDLSSGMenu = true;
		}
	} else {
		cs::log::Get("cs.feature.framegen.ui")->warn("Failed to QI swap chain ({:#x}) or command queue ({:#x})",
			(uintptr_t)realSC4, (uintptr_t)cmdQueue);
	}

	if (realSC4) realSC4->Release();
	if (cmdQueue) cmdQueue->Release();

	return hr;
}

HRESULT WINAPI hk_IDXGIFactory_CreateSwapChain(IDXGIFactory2* This, _In_ ID3D11Device* a_device, _In_ DXGI_SWAP_CHAIN_DESC* pDesc, _COM_Outptr_ IDXGISwapChain** ppSwapChain)
{
	// ENB path: ENB's wrapped factory calls CreateSwapChain - we intercept to insert our D3D12 proxy
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
			L->warn("XeSS-FG context creation failed (ENB path), falling back to FSR3");
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
			cs::log::Get("cs.feature.framegen.ui")->info("Hooked IDXGIFactory2::CreateSwapChainForHwnd (vtable 15) on original factory (ENB path)");
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
			L->warn("DLSS-G enable failed, falling back to FSR3");
			upscaling->activeFrameGenType = Upscaling::FrameGenType::kFSR3;
		}
	}

	proxy->CreateInterop();

	*ppSwapChain = proxy->GetSwapChainProxy();

	{
		ID3D11DeviceContext* menuContext = nullptr;
		a_device->GetImmediateContext(&menuContext);
		cs::Menu::Get().OnD3D11Ready(a_device, menuContext, pDesc->OutputWindow);
		if (menuContext)
			menuContext->Release();
		cs::Menu::Get().HookPresentOn(*ppSwapChain);
	}

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
				L->info("GPU: {} (VRAM: {}MB, VendorId: {:#x}, DeviceId: {:#x})",
					gpuName, adapterDesc.DedicatedVideoMemory / (1024 * 1024),
					adapterDesc.VendorId, adapterDesc.DeviceId);
			}
		}

		L->info("Frame Generation enabled, using D3D12 proxy");

		auto fidelityFX = FidelityFX::GetSingleton();

		bool hasBackend = fidelityFX->module;

		// For DLSS-G, tentatively enable - actual init after D3D12 device creation
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

			// Slot-10 install on the engine's adapter-parent factory. Atomic-once across both install sites.
			InstallSlot10Once(dxgiFactory);
			if (!cs::env::IsENBLoaded()) {
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
						L->warn("XeSS-FG context creation failed, falling back to FSR3");
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
						cs::log::Get("cs.feature.framegen.ui")->info("Hooked IDXGIFactory2::CreateSwapChainForHwnd (vtable 15) on original factory");
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
						L->warn("DLSS-G enable failed, falling back to FSR3");
						upscaling->activeFrameGenType = Upscaling::FrameGenType::kFSR3;
					}
				}

				proxy->CreateInterop();

				*ppSwapChain = proxy->GetSwapChainProxy();

				cs::Menu::Get().OnD3D11Ready(*ppDevice, *ppImmediateContext, pSwapChainDesc->OutputWindow);
				cs::Menu::Get().HookPresentOn(*ppSwapChain);

				return S_OK;
			}

		} else {
			L->warn("No frame generation backend available, skipping proxy");
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
	L->info("ENB state: {} swap chain hook", cs::env::IsENBLoaded() ? "loaded, using alternative" : "not loaded, using standard");

	auto upscaling = Upscaling::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	// Always load FidelityFX as fallback
	fidelityFX->LoadFFX();

	if (upscaling->settings.frameGenType == 1) {
		L->info("DLSS-G requested, loading Streamline interposer");
		auto dlssg = StreamlineFG::GetSingleton();
		dlssg->LoadInterposer();
	} else if (upscaling->settings.frameGenType == 2) {
		L->info("XeSS-FG requested, loading XeSS libraries");
		auto xess = XeSSFG::GetSingleton();
		xess->LoadLibraries();
	}

	uintptr_t moduleBase = (uintptr_t)GetModuleHandle(nullptr);

	(uintptr_t&)ptrD3D11CreateDeviceAndSwapChain = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uintptr_t)hk_D3D11CreateDeviceAndSwapChain);

	// Defensive CreateDXGIFactory1 hook only when an FG backend can actually use the proxy. Skip when no backend loaded so non-FG users don't get their dxgi calls intercepted.
	if (fidelityFX->module ||
		(upscaling->settings.frameGenType == 1 && StreamlineFG::GetSingleton()->interposer) ||
		(upscaling->settings.frameGenType == 2 && XeSSFG::GetSingleton()->fgModule)) {
		(uintptr_t&)ptrCreateDXGIFactory1 = Detours::IATHook(moduleBase, "dxgi.dll", "CreateDXGIFactory1", (uintptr_t)hk_CreateDXGIFactory1);
	}
}

}
