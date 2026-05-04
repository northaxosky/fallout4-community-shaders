#include "DX11Hooks.h"

#include <d3d11.h>

#include "Env.h"
#include "Log.h"
#include "Menu.h"
#include "Streamline.h"

namespace cs::features::Upscaling
{
	namespace { auto* L = cs::log::Get("cs.feature.upscaling.dx11"); }


struct hkD3D11CreateDeviceAndSwapChain
{
	static HRESULT WINAPI thunk(
		IDXGIAdapter* pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software,
		UINT Flags,
		const D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
		IDXGISwapChain** ppSwapChain,
		ID3D11Device** ppDevice,
		D3D_FEATURE_LEVEL* pFeatureLevel,
		ID3D11DeviceContext** ppImmediateContext)
	{
		L->info("D3D11CreateDeviceAndSwapChain called, forcing feature level 11_1");
		const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
		pFeatureLevels = &featureLevel;
		FeatureLevels = 1;

		DX::ThrowIfFailed(func(pAdapter,
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
			ppImmediateContext));

		L->info("Device created successfully, feature level: 0x{:x}", static_cast<uint>(*pFeatureLevel));
		if (pSwapChainDesc) {
			L->info("SwapChain: {}x{}, format={}, bufferCount={}", pSwapChainDesc->BufferDesc.Width, pSwapChainDesc->BufferDesc.Height, static_cast<uint>(pSwapChainDesc->BufferDesc.Format), pSwapChainDesc->BufferCount);
		}

		auto streamline = Streamline::GetSingleton();

		if (streamline->interposer){
			cs::log::Get("cs.feature.upscaling.streamline")->info("Interposer present, initializing Streamline...");
			streamline->Initialize();
			// X2 cleanup: drops once DXGISwapChainProxy is fully implemented (see findings/pdperf-symbol-analysis.md §5.3).
			if (!cs::env::IsENBLoaded() && !streamline->alreadyInitialized) {
				cs::log::Get("cs.feature.upscaling.streamline")->info("Upgrading swap chain interface (no ENB)");
				streamline->slUpgradeInterface((void**)&(*ppSwapChain));
			} else if (streamline->alreadyInitialized) {
				cs::log::Get("cs.feature.upscaling.streamline")->info("Skipping swap chain upgrade (FrameGen plugin owns Streamline)");
			} else {
				cs::log::Get("cs.feature.upscaling.streamline")->info("Skipping swap chain upgrade (ENB loaded)");
			}
			streamline->slSetD3DDevice(*ppDevice);
			streamline->CheckFeatures(pAdapter);
			streamline->PostDevice();
		} else {
			cs::log::Get("cs.feature.upscaling.streamline")->info("No interposer loaded, Streamline disabled");
		}

		cs::Menu::Get().OnD3D11Ready(*ppDevice, *ppImmediateContext, pSwapChainDesc->OutputWindow);
		cs::Menu::Get().HookPresentOn(*ppSwapChain);

		return S_OK;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

namespace DX11Hooks
{
	void Install()
	{
		auto streamline = Streamline::GetSingleton();
		streamline->LoadInterposer();

		uintptr_t moduleBase = (uintptr_t)GetModuleHandle(nullptr);
		cs::log::Get("cs.feature.upscaling.hook")->info("Module base: {:#x}", moduleBase);

		// Hook BSGraphics::CreateD3DAndSwapChain::D3D11CreateDeviceAndSwapChain to use D3D_FEATURE_LEVEL_11_1
		cs::log::Get("cs.feature.upscaling.hook")->info("Installing IAT hook for D3D11CreateDeviceAndSwapChain");
		(uintptr_t&)hkD3D11CreateDeviceAndSwapChain::func = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uintptr_t)hkD3D11CreateDeviceAndSwapChain::thunk);
		cs::log::Get("cs.feature.upscaling.hook")->info("IAT hook installed, original func: {:#x}", (uintptr_t)hkD3D11CreateDeviceAndSwapChain::func.get());
	}
}

}
