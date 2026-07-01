#include "DX11Hooks.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "FrameGeneration.h"
#include "DX12SwapChain.h"
#include "FidelityFX.h"
#include "Streamline.h"
#include <nvsdk_ngx.h>

#include "Env.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Feature.h"
#include "Render/StreamlineCore.h"
#include "XeSSFG.h"

namespace cs::features::framegeneration
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

HRESULT WINAPI hk_IDXGIFactory_CreateSwapChain(IDXGIFactory2* This, _In_ ID3D11Device* a_device, _In_ DXGI_SWAP_CHAIN_DESC* pDesc, _COM_Outptr_ IDXGISwapChain** ppSwapChain)
{
	// ENB path: ENB's wrapped factory calls CreateSwapChain - we intercept to insert our D3D12 proxy
	auto frameGen = FrameGeneration::GetSingleton();

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
	if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
		cs::Streamline::GetSingleton()->Initialize();
	}

	proxy->CreateD3D12Device(adapter);
	adapter->Release();

	// XeSS-FG: create contexts after D3D12 device
	if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kXeSSFG) {
		auto xess = XeSSFG::GetSingleton();
		if (!xess->CreateContexts(proxy->d3d12Device.get())) {
			L->warn("XeSS-FG context creation failed (ENB path), falling back to FSR3");
			frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
		}
	}

	// DLSS-G: upgrade device+factory via Streamline. Backend selection already routed DLSS-G away
	// from ENB (ENB owns the swap chain), so this only runs when Streamline may own it.
	IDXGIFactory5* factory = (IDXGIFactory5*)This;
	if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
		auto* core = cs::Streamline::GetSingleton();

		ID3D12Device* rawDevice = proxy->d3d12Device.get();
		core->slUpgradeInterface((void**)&rawDevice);
		proxy->d3d12Device.copy_from(rawDevice);

		IDXGIFactory* rawFactory = (IDXGIFactory*)factory;
		core->slUpgradeInterface((void**)&rawFactory);
		factory = (IDXGIFactory5*)rawFactory;

		StreamlineFG::GetSingleton()->SetD3DDevice(proxy->d3d12Device.get());
	}

	proxy->CreateD3D12CommandQueues();
	proxy->CreateSwapChain(factory, *pDesc);

	if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
		auto dlssg = StreamlineFG::GetSingleton();
		if (!dlssg->CheckAndEnableDLSSG()) {
			L->warn("DLSS-G enable failed, falling back to FSR3");
			frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
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
	auto frameGen = FrameGeneration::GetSingleton();

	if (pSwapChainDesc->Windowed) {
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

		auto fidelityFX = FidelityFX::GetSingleton();

		// User-disabled FG keeps FO4 on native D3D11 for clean captures.
		bool userEnabled = frameGen->settings.frameGenerationMode;

		// RenderDoc needs the real D3D11 chain; FSR3/XeSS-FG proxy captures are empty.
		if (userEnabled && cs::env::IsRenderDocActive()) {
			L->warn("RenderDoc detected; disabling FrameGeneration for this session to preserve native D3D11 capture path");
			userEnabled = false;
		}

		bool hasBackend = userEnabled && fidelityFX->module;
		if (!userEnabled) {
			L->info("FrameGeneration disabled in INI; skipping D3D12 swap-chain proxy");
		} else {
			L->info("Frame Generation enabled, using D3D12 proxy");
		}

		// For DLSS-G, tentatively enable - actual init after D3D12 device creation
		if (userEnabled && frameGen->settings.frameGenType == 1) {
			if (cs::env::IsENBLoaded()) {
				// ENB owns the swap chain, so Streamline can't upgrade it for DLSS-G. Use FSR3-FG when
				// its runtime is loaded, otherwise leave FG off rather than crash in the FSR3 path later.
				if (fidelityFX->module) {
					L->warn("DLSS-G unavailable under ENB; using FSR3 frame generation instead");
					frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
					hasBackend = true;
				} else {
					L->warn("DLSS-G selected under ENB but FSR3 runtime not loaded; disabling frame generation");
					hasBackend = false;
				}
			} else {
				frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kDLSSG;
				hasBackend = true;
			}
		} else if (userEnabled && frameGen->settings.frameGenType == 2) {
			auto xess = XeSSFG::GetSingleton();
			if (xess->fgModule && xess->xellModule) {
				frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kXeSSFG;
				hasBackend = true;
			}
		}

		if (hasBackend) {
			frameGen->d3d12Interop = true;
			frameGen->refreshRate = FrameGeneration::GetRefreshRate(pSwapChainDesc->OutputWindow);

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
				if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
					cs::Streamline::GetSingleton()->Initialize();
				}

				proxy->CreateD3D12Device(adapter);
				adapter->Release();

				// XeSS-FG: create contexts after D3D12 device, no device/factory upgrade needed
				if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kXeSSFG) {
					auto xess = XeSSFG::GetSingleton();
					if (!xess->CreateContexts(proxy->d3d12Device.get())) {
						L->warn("XeSS-FG context creation failed, falling back to FSR3");
						frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
					}
				}

				// DLSS-G: upgrade device/factory, then slSetD3DDevice before proxy hooks fire.
				if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
					auto* core = cs::Streamline::GetSingleton();

					ID3D12Device* rawDevice = proxy->d3d12Device.get();
					core->slUpgradeInterface((void**)&rawDevice);
					proxy->d3d12Device.copy_from(rawDevice);

					IDXGIFactory* rawFactory = (IDXGIFactory*)dxgiFactory;
					core->slUpgradeInterface((void**)&rawFactory);
					dxgiFactory = (IDXGIFactory4*)rawFactory;

					StreamlineFG::GetSingleton()->SetD3DDevice(proxy->d3d12Device.get());
				}

				proxy->CreateD3D12CommandQueues();
				proxy->CreateSwapChain((IDXGIFactory5*)dxgiFactory, *pSwapChainDesc);

				if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
					auto dlssg = StreamlineFG::GetSingleton();

					if (!dlssg->CheckAndEnableDLSSG()) {
						L->warn("DLSS-G enable failed, falling back to FSR3");
						frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
					}
				}

				proxy->CreateInterop();

				*ppSwapChain = proxy->GetSwapChainProxy();

				cs::Menu::Get().OnD3D11Ready(*ppDevice, *ppImmediateContext, pSwapChainDesc->OutputWindow);
				cs::Menu::Get().HookPresentOn(*ppSwapChain);

				// Proxy path returns early instead of chaining to Upscaling's IAT thunk; drive shared Streamline init here.
				cs::Streamline::GetSingleton()->Initialize();
				cs::Streamline::GetSingleton()->OnD3D11Ready(pAdapter, *ppDevice);
				cs::FeatureManager::Get().OnD3D11ReadyAll(pAdapter, *ppDevice);

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

	auto frameGen = FrameGeneration::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	fidelityFX->LoadFFX();

	if (frameGen->settings.frameGenType == 1) {
		L->info("DLSS-G requested, ensuring Streamline interposer is loaded");
		cs::Streamline::GetSingleton()->LoadInterposer();
	} else if (frameGen->settings.frameGenType == 2) {
		L->info("XeSS-FG requested, loading XeSS libraries");
		auto xess = XeSSFG::GetSingleton();
		xess->LoadLibraries();
	}

	uintptr_t moduleBase = (uintptr_t)GetModuleHandle(nullptr);

	(uintptr_t&)ptrD3D11CreateDeviceAndSwapChain = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uintptr_t)hk_D3D11CreateDeviceAndSwapChain);

	// Hook CreateDXGIFactory1 only when an FG backend can actually use the proxy. Under ENB the
	// DLSS-G interposer path can't wrap ENB's swap chain (it falls back to FSR3, gated by module),
	// so don't let a missing-FSR3 DLSS-G+ENB install intercept factories and crash the FSR3 path.
	if (fidelityFX->module ||
		(frameGen->settings.frameGenType == 1 && cs::Streamline::GetSingleton()->interposer && !cs::env::IsENBLoaded()) ||
		(frameGen->settings.frameGenType == 2 && XeSSFG::GetSingleton()->fgModule)) {
		(uintptr_t&)ptrCreateDXGIFactory1 = Detours::IATHook(moduleBase, "dxgi.dll", "CreateDXGIFactory1", (uintptr_t)hk_CreateDXGIFactory1);
	}
}

}
