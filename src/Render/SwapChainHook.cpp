#include "Render/SwapChainHook.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <mutex>
#include <utility>
#include <vector>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/D3D11Bootstrap.h"

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.swapchainhook");
		std::atomic<CreateDeviceAndSwapChain> nextCreateDeviceAndSwapChain{ nullptr };
		std::atomic<CreateDevice> nextCreateDevice{ nullptr };
		std::atomic<SwapChainHookState> installState{ SwapChainHookState::kUnattempted };
		std::mutex installMutex;
		std::vector<PreCreateDeviceCallback> preCreateCallbacks;
		std::vector<PostCreateDeviceCallback> postCreateCallbacks;
		ReplacementCreateDeviceCallback replacementCreateCallback;

		// FO4: no static d3d11 import; bind by name like upstream's import would.
		template <class Fn>
		Fn ResolveD3D11Export(const char* a_name) noexcept
		{
			auto* d3d11 = GetModuleHandleW(L"d3d11.dll");
			return d3d11 ? reinterpret_cast<Fn>(GetProcAddress(d3d11, a_name)) : nullptr;
		}

		// A throwing callback must never cross the game's import boundary.
		template <class Fn>
		void RunGuarded(const char* a_phase, Fn&& a_fn) noexcept
		{
			try {
				a_fn();
			} catch (const std::exception& e) {
				L->error("SwapChainHook {} callback threw: {}", a_phase, e.what());
			} catch (...) {
				L->error("SwapChainHook {} callback threw", a_phase);
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
			CS_LOG_ONCE(L, spdlog::level::info, "SwapChainHook thunk ran");
			const auto next = nextCreateDeviceAndSwapChain.load(std::memory_order_acquire);
			if (!next) {
				L->error("SwapChainHook thunk has no next hook; returning E_FAIL");
				return E_FAIL;
			}

			DXGI_SWAP_CHAIN_DESC swapChainDesc{};
			const bool hasDesc = a_swapChainDesc != nullptr;
			std::vector<D3D_FEATURE_LEVEL> featureLevels;
			if (a_featureLevels && a_featureLevelCount) {
				featureLevels.assign(a_featureLevels, a_featureLevels + a_featureLevelCount);
			}
			if (hasDesc) {
				swapChainDesc = *a_swapChainDesc;
				for (auto& callback : preCreateCallbacks) {
					RunGuarded("pre-create", [&] { callback(&swapChainDesc, featureLevels); });
				}
			}

			const auto* requestedFeatureLevels =
				featureLevels.empty() ? a_featureLevels : featureLevels.data();
			const auto requestedFeatureLevelCount =
				featureLevels.empty() ? a_featureLevelCount : static_cast<UINT>(featureLevels.size());
			CreateDeviceAndSwapChainContext context{
				.createDevice = nextCreateDevice.load(std::memory_order_acquire),
				.adapter = a_adapter,
				.driverType = a_driverType,
				.software = a_software,
				.flags = a_flags,
				.featureLevels = requestedFeatureLevels,
				.featureLevelCount = requestedFeatureLevelCount,
				.sdkVersion = a_sdkVersion,
				.swapChainDesc = hasDesc ? &swapChainDesc : nullptr,
				.swapChain = a_swapChain,
				.device = a_device,
				.featureLevel = a_featureLevel,
				.immediateContext = a_immediateContext
			};

			std::optional<HRESULT> replacementResult;
			if (replacementCreateCallback) {
				try {
					replacementResult = replacementCreateCallback(context);
				} catch (const std::exception& e) {
					L->error("SwapChainHook replacement-create callback threw: {}", e.what());
					replacementResult = E_FAIL;
				} catch (...) {
					L->error("SwapChainHook replacement-create callback threw");
					replacementResult = E_FAIL;
				}
			}

			const HRESULT result = replacementResult ? *replacementResult : next(a_adapter, a_driverType, a_software, a_flags, requestedFeatureLevels, requestedFeatureLevelCount, a_sdkVersion, hasDesc ? &swapChainDesc : nullptr, a_swapChain, a_device, a_featureLevel, a_immediateContext);

			if (SUCCEEDED(result)) {
				// Callbacks run before the bootstrap so an interface upgrade is visible to it.
				for (auto& callback : postCreateCallbacks) {
					RunGuarded("post-create", [&] { callback(a_adapter, a_device, a_swapChain); });
				}
			}

			cs::d3d11::RunBootstrapPostCreate(
				result,
				a_adapter,
				hasDesc ? &swapChainDesc : nullptr,
				a_swapChain,
				a_device,
				a_immediateContext);

			return result;
		}
	}

	bool RegisterPreCreateDeviceAndSwapChain(PreCreateDeviceCallback a_callback)
	{
		std::scoped_lock lock(installMutex);
		if (!a_callback ||
			installState.load(std::memory_order_acquire) !=
				SwapChainHookState::kUnattempted) {
			return false;
		}
		preCreateCallbacks.push_back(std::move(a_callback));
		return true;
	}

	bool RegisterPostCreateDeviceAndSwapChain(PostCreateDeviceCallback a_callback)
	{
		std::scoped_lock lock(installMutex);
		if (!a_callback ||
			installState.load(std::memory_order_acquire) !=
				SwapChainHookState::kUnattempted) {
			return false;
		}
		postCreateCallbacks.push_back(std::move(a_callback));
		return true;
	}

	bool RegisterReplacementCreateDeviceAndSwapChain(ReplacementCreateDeviceCallback a_callback)
	{
		std::scoped_lock lock(installMutex);
		if (!a_callback || replacementCreateCallback ||
			installState.load(std::memory_order_acquire) !=
				SwapChainHookState::kUnattempted) {
			return false;
		}
		replacementCreateCallback = std::move(a_callback);
		return true;
	}

	bool InstallSwapChainHook()
	{
		std::scoped_lock lock(installMutex);
		if (installState.load(std::memory_order_acquire) !=
			SwapChainHookState::kUnattempted) {
			return installState.load(std::memory_order_acquire) ==
			       SwapChainHookState::kInstalled;
		}
		installState.store(SwapChainHookState::kInstalling, std::memory_order_release);

		const auto previous = REX::FModule::GetExecutingModule().SetImportFunctionAddress(
			"D3D11CreateDeviceAndSwapChain",
			"d3d11.dll",
			reinterpret_cast<uintptr_t>(&CreateDeviceAndSwapChainThunk));
		if (!previous ||
			previous == reinterpret_cast<uintptr_t>(&CreateDeviceAndSwapChainThunk)) {
			const auto existing =
				nextCreateDeviceAndSwapChain.load(std::memory_order_acquire);
			const auto native =
				ResolveD3D11Export<CreateDeviceAndSwapChain>("D3D11CreateDeviceAndSwapChain");
			if (!existing && native &&
				native != &CreateDeviceAndSwapChainThunk) {
				nextCreateDeviceAndSwapChain.store(native, std::memory_order_release);
			}
			if (!nextCreateDevice.load(std::memory_order_acquire)) {
				nextCreateDevice.store(ResolveD3D11Export<CreateDevice>("D3D11CreateDevice"),
					std::memory_order_release);
			}
			L->error(
				"SwapChainHook IAT installation returned an invalid predecessor ({:#x}); "
				"the thunk will pass through to the system entry point if it was installed",
				previous);
			installState.store(SwapChainHookState::kFailed, std::memory_order_release);
			return false;
		}
		nextCreateDeviceAndSwapChain.store(
			reinterpret_cast<CreateDeviceAndSwapChain>(previous),
			std::memory_order_release);
		nextCreateDevice.store(ResolveD3D11Export<CreateDevice>("D3D11CreateDevice"),
			std::memory_order_release);
		installState.store(SwapChainHookState::kInstalled, std::memory_order_release);
		L->info("SwapChainHook IAT hook installed (next={:#x})", previous);
		return true;
	}

	SwapChainHookState GetSwapChainHookState() noexcept
	{
		return installState.load(std::memory_order_acquire);
	}
}
