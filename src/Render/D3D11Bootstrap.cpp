#include "Render/D3D11Bootstrap.h"

#include <atomic>
#include <exception>
#include <string_view>

#include "Feature.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderInjection.h"

namespace cs::d3d11
{
	namespace
	{
		auto* L = cs::log::Get("cs.d3d11.bootstrap");
		std::atomic<bool> ready{ false };

		template <class Callback>
		void InvokeOwner(std::string_view a_name, Callback&& a_callback) noexcept
		{
			try {
				a_callback();
			} catch (const std::exception& e) {
				try {
					L->error("{} failed: {}", a_name, e.what());
				} catch (...) {
				}
			} catch (...) {
				try {
					L->error("{} failed: unknown exception", a_name);
				} catch (...) {
				}
			}
		}
	}

	void RunBootstrapPostCreate(
		HRESULT a_result,
		IDXGIAdapter* a_adapter,
		const DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
		IDXGISwapChain** a_swapChain,
		ID3D11Device** a_device,
		ID3D11DeviceContext** a_immediateContext)
	{
		const bool complete =
			SUCCEEDED(a_result)
			&& a_swapChainDesc
			&& a_swapChainDesc->OutputWindow
			&& a_swapChain
			&& *a_swapChain
			&& a_device
			&& *a_device
			&& a_immediateContext
			&& *a_immediateContext;
		bool expected = false;
		if (complete && ready.compare_exchange_strong(expected, true)) {
			// Load-bearing order: FeatureManager::OnD3D11ReadyAll (ShaderCatalog registers observer) must precede FreezeAndCompileShaderInjections (registers resolver) so the broker's slot-15 thunk sees observers before the first resolver dispatch. SetPixelShaderSwapBrokerDevice may run at any point relative to the two registrations - both Register* paths latch g_installRequested and the hook installs whenever the device is also known.
			InvokeOwner("PixelShaderSwapBroker D3D11 readiness", [&] {
				engine::SetPixelShaderSwapBrokerDevice(*a_device);
			});
			InvokeOwner("FeatureManager D3D11 readiness", [&] {
				FeatureManager::Get().OnD3D11ReadyAll(a_adapter, *a_device);
			});
			InvokeOwner("ShaderInjection freeze and compile", [&] {
				engine::FreezeAndCompileShaderInjections(*a_device);
			});
			InvokeOwner("Menu D3D11 initialization", [&] {
				Menu::Get().OnD3D11Ready(*a_device, *a_immediateContext, a_swapChainDesc->OutputWindow);
			});
			InvokeOwner("Menu Present hook", [&] {
				Menu::Get().HookPresentOn(*a_swapChain);
			});
		}
	}
}
