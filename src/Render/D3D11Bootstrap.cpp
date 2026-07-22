#include "Render/D3D11Bootstrap.h"

#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <exception>
#include <mutex>
#include <string_view>

#include "Feature.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderInjection.h"

namespace cs::d3d11
{
	namespace
	{
		using CreateDeviceAndSwapChain = decltype(&D3D11CreateDeviceAndSwapChain);

		auto* L = cs::log::Get("cs.d3d11.bootstrap");
		std::atomic<CreateDeviceAndSwapChain> nextCreateDeviceAndSwapChain{ nullptr };
		std::atomic<bool> ready{ false };
		std::mutex installMutex;
		bool installAttempted = false;

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

		HRESULT WINAPI CreateDeviceAndSwapChainThunk(
			IDXGIAdapter* a_adapter,
			D3D_DRIVER_TYPE a_driverType,
			HMODULE a_software,
			UINT a_flags,
			const D3D_FEATURE_LEVEL* a_featureLevels,
			UINT a_featureLevelCount,
			UINT a_sdkVersion,
			const DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
			IDXGISwapChain** a_swapChain,
			ID3D11Device** a_device,
			D3D_FEATURE_LEVEL* a_featureLevel,
			ID3D11DeviceContext** a_immediateContext)
		{
			CS_LOG_ONCE(L, spdlog::level::info, "Core bootstrap swap-chain thunk ran");
			auto next = nextCreateDeviceAndSwapChain.load(std::memory_order_acquire);
			if (!next) {
				std::scoped_lock lock(installMutex);
				next = nextCreateDeviceAndSwapChain.load(std::memory_order_acquire);
			}
			if (!next) {
				L->error("D3D11 bootstrap invoked without a downstream function");
				return E_FAIL;
			}

			const HRESULT result = next(
				a_adapter,
				a_driverType,
				a_software,
				a_flags,
				a_featureLevels,
				a_featureLevelCount,
				a_sdkVersion,
				a_swapChainDesc,
				a_swapChain,
				a_device,
				a_featureLevel,
				a_immediateContext);

			RunBootstrapPostCreate(
				result,
				a_adapter,
				a_swapChainDesc,
				a_swapChain,
				a_device,
				a_immediateContext);

			return result;
		}
	}

	void InstallBootstrapHook()
	{
		std::scoped_lock lock(installMutex);
		if (installAttempted) {
			return;
		}
		installAttempted = true;

		const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		if (!module) {
			L->error("Failed to install D3D11 bootstrap hook: game module not found");
			return;
		}

		const auto previous = Detours::IATHook(
			module,
			"d3d11.dll",
			"D3D11CreateDeviceAndSwapChain",
			reinterpret_cast<uintptr_t>(&CreateDeviceAndSwapChainThunk));
		if (!previous) {
			L->error("Failed to install D3D11 bootstrap hook: no downstream function returned");
			return;
		}
		nextCreateDeviceAndSwapChain.store(
			reinterpret_cast<CreateDeviceAndSwapChain>(previous),
			std::memory_order_release);
		L->info("Core bootstrap installed d3d11.dll!D3D11CreateDeviceAndSwapChain IAT hook (previous={:#x})", previous);
	}
}
