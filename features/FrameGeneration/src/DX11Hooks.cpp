#include "DX11Hooks.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "FrameGeneration.h"
#include "DX12SwapChain.h"
#include "FidelityFX.h"
#include "Streamline.h"
#include "Upscaling.h"
#include <nvsdk_ngx.h>

#include "Env.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Render/SwapChainHook.h"
#include "Render/StreamlineCore.h"
#include "XeSSFG.h"

namespace cs::features::framegeneration
{
	namespace { auto* L = cs::log::Get("cs.feature.framegeneration.dx11"); }

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

	if (!pDesc->Windowed) {
		frameGen->SetLastKnownWindowed(false);
		frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kExclusiveFullscreen);
		CS_LOG_ONCE(L, spdlog::level::warn, "Frame generation requested but skipped: reason=exclusive_fullscreen");
		return (This->*ptrCreateSwapChain)(a_device, pDesc, ppSwapChain);
	}

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

	// DLSS-G: upgrade device+factory via Streamline (selection already routed DLSS-G away from ENB).
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

	return S_OK;
}

static bool IsFrameGenerationActive() noexcept
{
	return FrameGeneration::GetSingleton()->IsLoaded();
}

static cs::render::FrameGenerationCreateRoute EvaluateFrameGenerationCreate(
	cs::render::SwapChainCreateContext& a_context)
{
	auto* pAdapter = a_context.adapter;
	auto* pSwapChainDesc = a_context.swapChainDesc;
	auto frameGen = FrameGeneration::GetSingleton();
	CS_LOG_ONCE(L, spdlog::level::info, "FrameGeneration create evaluation ran");
	L->info("FrameGeneration swap-chain create: Windowed={}, OutputWindow={:#x}",
		pSwapChainDesc->Windowed != FALSE,
		reinterpret_cast<uintptr_t>(pSwapChainDesc->OutputWindow));
	frameGen->SetLastKnownWindowed(pSwapChainDesc->Windowed != FALSE);

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
		const bool frameGenerationRequested = frameGen->settings.frameGenerationMode;
		bool userEnabled = frameGenerationRequested;
		if (!frameGenerationRequested)
			frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kUserDisabled);

		// RenderDoc needs the real D3D11 chain; FSR3/XeSS-FG proxy captures are empty.
		if (userEnabled && cs::env::IsRenderDocActive()) {
			frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kRenderDoc);
			CS_LOG_ONCE(L, spdlog::level::warn, "Frame generation requested but skipped: reason=renderdoc");
			userEnabled = false;
		}

		bool hasBackend = userEnabled && fidelityFX->module;
		if (!frameGenerationRequested) {
			L->info("FrameGeneration disabled in INI; skipping D3D12 swap-chain proxy");
		} else if (userEnabled) {
			L->info("Frame Generation requested; evaluating D3D12 proxy backend");
		}

		// For DLSS-G, tentatively enable - actual init after D3D12 device creation
		if (userEnabled && frameGen->settings.frameGenType == 1) {
			if (cs::env::IsENBLoaded()) {
				// ENB owns the swap chain (no DLSS-G upgrade); use FSR3-FG if its runtime loaded, else disable FG.
				if (fidelityFX->module) {
					L->warn("DLSS-G unavailable under ENB; using FSR3 frame generation instead");
					frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
					hasBackend = true;
				} else {
					frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kENBSwapChainOwner);
					CS_LOG_ONCE(L, spdlog::level::warn, "Frame generation requested but skipped: reason=enb_swapchain_owner");
					hasBackend = false;
				}
			} else {
				auto upscaling = Upscaling::GetSingleton();
				const bool conflictsWithDlssUpscaling = upscaling->IsLoaded() &&
					upscaling->settings.upscaleMethodPreference == static_cast<uint>(Upscaling::UpscaleMethod::kDLSS);
				if (conflictsWithDlssUpscaling) {
					if (fidelityFX->module) {
						L->warn("DLSS-G cannot run alongside DLSS upscaling (Streamline one-device-per-instance limit; see issue #7) - using FSR3 frame generation instead");
						frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
						hasBackend = true;
					} else {
						frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kDlssgUpscalerConflict);
						CS_LOG_ONCE(L, spdlog::level::warn, "DLSS-G cannot run alongside DLSS upscaling (Streamline one-device-per-instance limit; see issue #7), and the FSR3 frame generation module is unavailable - disabling frame generation");
						hasBackend = false;
					}
				} else {
					frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kDLSSG;
					hasBackend = true;
				}
			}
		} else if (userEnabled && frameGen->settings.frameGenType == 2) {
			auto xess = XeSSFG::GetSingleton();
			if (xess->fgModule && xess->xellModule) {
				frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kXeSSFG;
				hasBackend = true;
			}
		}

		if (hasBackend) {
			frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kActive);
			frameGen->d3d12Interop = true;
			frameGen->refreshRate = FrameGeneration::GetRefreshRate(pSwapChainDesc->OutputWindow);

			IDXGIFactory4* dxgiFactory;
			pAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

			a_context.ForceFeatureLevel11_1();

			// Slot-10 install on the engine's adapter-parent factory. Atomic-once across both install sites.
			InstallSlot10Once(dxgiFactory);
			if (!cs::env::IsENBLoaded()) {
				return { true, dxgiFactory };
			}
		} else if (userEnabled &&
			frameGen->GetFrameGenSkipReason() != FrameGeneration::FrameGenSkipReason::kENBSwapChainOwner &&
			frameGen->GetFrameGenSkipReason() != FrameGeneration::FrameGenSkipReason::kDlssgUpscalerConflict) {
			frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kNoModule);
			CS_LOG_ONCE(L, spdlog::level::warn, "Frame generation requested but skipped: reason=no_module");
		}
	} else {
		if (frameGen->settings.frameGenType == static_cast<int>(FrameGeneration::FrameGenType::kDLSSG)) {
			// Keep shared Streamline initialization DLSS-only when FG cannot run.
			auto* core = cs::Streamline::GetSingleton();
			core->RemoveRequestedFeature(sl::kFeatureDLSS_G);
			core->RemoveRequestedFeature(sl::kFeatureReflex);
			core->RemoveRequestedFeature(sl::kFeaturePCL);
		}

		if (frameGen->settings.frameGenerationMode) {
			frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kExclusiveFullscreen);
			CS_LOG_ONCE(L, spdlog::level::warn, "Frame generation requested but skipped: reason=exclusive_fullscreen");
		} else {
			frameGen->SetFrameGenSkipReason(FrameGeneration::FrameGenSkipReason::kUserDisabled);
		}
	}

	return {};
}

static HRESULT RunFrameGenerationInlineCreate(
	cs::render::SwapChainCreateContext& a_context,
	IDXGIFactory4* a_dxgiFactory)
{
	auto frameGen = FrameGeneration::GetSingleton();

	DX::ThrowIfFailed(D3D11CreateDevice(
		a_context.adapter,
		a_context.driverType,
		a_context.software,
		a_context.flags,
		a_context.featureLevels,
		a_context.featureLevelCount,
		a_context.sdkVersion,
		a_context.device,
		a_context.featureLevel,
		a_context.immediateContext));

	IDXGIDevice* dxgiDevice = nullptr;
	DX::ThrowIfFailed((*a_context.device)->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice));

	IDXGIAdapter* adapter = nullptr;
	DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter));
	dxgiDevice->Release();

	auto proxy = DX12SwapChain::GetSingleton();

	proxy->SetD3D11Device(*a_context.device);

	ID3D11DeviceContext* context;
	(*a_context.device)->GetImmediateContext(&context);
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

		IDXGIFactory* rawFactory = (IDXGIFactory*)a_dxgiFactory;
		core->slUpgradeInterface((void**)&rawFactory);
		a_dxgiFactory = (IDXGIFactory4*)rawFactory;

		StreamlineFG::GetSingleton()->SetD3DDevice(proxy->d3d12Device.get());
	}

	proxy->CreateD3D12CommandQueues();
	proxy->CreateSwapChain((IDXGIFactory5*)a_dxgiFactory, *a_context.swapChainDesc);

	if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
		auto dlssg = StreamlineFG::GetSingleton();

		if (!dlssg->CheckAndEnableDLSSG()) {
			L->warn("DLSS-G enable failed, falling back to FSR3");
			frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
		}
	}

	proxy->CreateInterop();

	*a_context.swapChain = proxy->GetSwapChainProxy();

	cs::Streamline::GetSingleton()->Initialize();
	cs::Streamline::GetSingleton()->OnD3D11Ready(a_context.adapter, *a_context.device);

	return S_OK;
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

	cs::render::RegisterFrameGenerationCreatePhases(
		&IsFrameGenerationActive,
		&EvaluateFrameGenerationCreate,
		&RunFrameGenerationInlineCreate);

	// Hook factory creation only when an FG backend can use the proxy; under ENB DLSS-G needs the FSR3 module (fallback), so require it.
	if (fidelityFX->module ||
		(frameGen->settings.frameGenType == 1 && cs::Streamline::GetSingleton()->interposer && !cs::env::IsENBLoaded()) ||
		(frameGen->settings.frameGenType == 2 && XeSSFG::GetSingleton()->fgModule)) {
		(uintptr_t&)ptrCreateDXGIFactory1 = Detours::IATHook(moduleBase, "dxgi.dll", "CreateDXGIFactory1", (uintptr_t)hk_CreateDXGIFactory1);
	}
}

}
