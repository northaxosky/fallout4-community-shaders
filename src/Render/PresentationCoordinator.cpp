#include "Render/PresentationCoordinator.h"

#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <mutex>

#include "Log.h"
#include "LogThrottle.h"

namespace cs::render
{
	namespace
	{
		using CreateDeviceAndSwapChain = decltype(&D3D11CreateDeviceAndSwapChain);

		auto* L = cs::log::Get("cs.render.presentationcoordinator");
		std::atomic<CreateDeviceAndSwapChain> nextCreateDeviceAndSwapChain{ nullptr };
		std::mutex installMutex;
		bool installAttempted = false;

		HRESULT WINAPI CreateDeviceAndSwapChainForwardThunk(
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
			CS_LOG_ONCE(L, spdlog::level::info, "PresentationCoordinator forward thunk ran");
			const auto next = nextCreateDeviceAndSwapChain.load(std::memory_order_acquire);
			if (!next) {
				L->error("PresentationCoordinator forward thunk has no next hook; returning E_FAIL");
				return E_FAIL;
			}
			return next(
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
		}
	}

	void InstallPresentationCoordinatorHook()
	{
		std::scoped_lock lock(installMutex);
		if (installAttempted) {
			return;
		}
		installAttempted = true;

		const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		if (!module) {
			L->error("PresentationCoordinator: GetModuleHandle failed; hook not installed");
			return;
		}
		const auto previous = Detours::IATHook(
			module,
			"d3d11.dll",
			"D3D11CreateDeviceAndSwapChain",
			reinterpret_cast<uintptr_t>(&CreateDeviceAndSwapChainForwardThunk));
		nextCreateDeviceAndSwapChain.store(
			reinterpret_cast<CreateDeviceAndSwapChain>(previous),
			std::memory_order_release);
		L->info("PresentationCoordinator IAT hook installed (next={:#x})", previous);
	}
}
