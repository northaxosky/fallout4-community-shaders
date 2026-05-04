#include "DX11Hooks.h"

#include <d3d11.h>

#include "Streamline.h"

extern bool enbLoaded;

namespace cs::features::Upscaling
{

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
		REX::INFO("[DX11] D3D11CreateDeviceAndSwapChain called, forcing feature level 11_1");
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

		REX::INFO("[DX11] Device created successfully, feature level: 0x{:x}", static_cast<uint>(*pFeatureLevel));
		if (pSwapChainDesc) {
			REX::INFO("[DX11] SwapChain: {}x{}, format={}, bufferCount={}", pSwapChainDesc->BufferDesc.Width, pSwapChainDesc->BufferDesc.Height, static_cast<uint>(pSwapChainDesc->BufferDesc.Format), pSwapChainDesc->BufferCount);
		}

		auto streamline = Streamline::GetSingleton();

		if (streamline->interposer){
			REX::INFO("[SL] Interposer present, initializing Streamline...");
			streamline->Initialize();
			if (!enbLoaded && !streamline->alreadyInitialized) {
				REX::INFO("[SL] Upgrading swap chain interface (no ENB)");
				streamline->slUpgradeInterface((void**)&(*ppSwapChain));
			} else if (streamline->alreadyInitialized) {
				REX::INFO("[SL] Skipping swap chain upgrade (FrameGen plugin owns Streamline)");
			} else {
				REX::INFO("[SL] Skipping swap chain upgrade (ENB loaded)");
			}
			streamline->slSetD3DDevice(*ppDevice);
			streamline->CheckFeatures(pAdapter);
			streamline->PostDevice();
		} else {
			REX::INFO("[SL] No interposer loaded, Streamline disabled");
		}

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
		REX::INFO("[HOOK] Module base: {:#x}", moduleBase);

		// Hook BSGraphics::CreateD3DAndSwapChain::D3D11CreateDeviceAndSwapChain to use D3D_FEATURE_LEVEL_11_1
		REX::INFO("[HOOK] Installing IAT hook for D3D11CreateDeviceAndSwapChain");
		(uintptr_t&)hkD3D11CreateDeviceAndSwapChain::func = Detours::IATHook(moduleBase, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uintptr_t)hkD3D11CreateDeviceAndSwapChain::thunk);
		REX::INFO("[HOOK] IAT hook installed, original func: {:#x}", (uintptr_t)hkD3D11CreateDeviceAndSwapChain::func.get());
	}
}

}
