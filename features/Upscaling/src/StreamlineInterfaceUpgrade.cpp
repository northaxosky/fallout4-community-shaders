#include "StreamlineInterfaceUpgrade.h"

#include <d3d11.h>

namespace cs::features::streamline
{
	namespace
	{
		bool HasSameIdentity(IUnknown* a_left, IUnknown* a_right) noexcept
		{
			if (!a_left || !a_right) {
				return false;
			}
			IUnknown* leftIdentity = nullptr;
			IUnknown* rightIdentity = nullptr;
			const HRESULT leftResult =
				a_left->QueryInterface(IID_PPV_ARGS(&leftIdentity));
			const HRESULT rightResult =
				a_right->QueryInterface(IID_PPV_ARGS(&rightIdentity));
			const bool same =
				SUCCEEDED(leftResult) && SUCCEEDED(rightResult) &&
				leftIdentity == rightIdentity;
			if (rightIdentity) {
				rightIdentity->Release();
			}
			if (leftIdentity) {
				leftIdentity->Release();
			}
			return same;
		}
	}

	bool ShouldInstallSwapChainProxy(
		bool a_initialized,
		bool a_deviceRegistered,
		bool a_dlssAdmitted,
		bool a_d3d12Session) noexcept
	{
		return a_initialized && a_deviceRegistered && a_dlssAdmitted &&
			!a_d3d12Session;
	}

	SwapChainUpgradeResult UpgradeD3D11SwapChain(
		PFun_slUpgradeInterface* a_upgradeInterface,
		PFun_slGetNativeInterface* a_getNativeInterface,
		IDXGISwapChain** a_swapChain) noexcept
	{
		if (!a_upgradeInterface || !a_getNativeInterface || !a_swapChain ||
			!*a_swapChain) {
			return { .status = SwapChainUpgradeStatus::kInvalidArgument };
		}

		auto* original = *a_swapChain;
		ID3D11Device* device = nullptr;
		const HRESULT deviceResult =
			original->GetDevice(IID_PPV_ARGS(&device));
		if (FAILED(deviceResult) || !device) {
			if (device) {
				device->Release();
			}
			return { .status = SwapChainUpgradeStatus::kIncompatibleDevice };
		}
		device->Release();

		void* candidate = original;
		const sl::Result upgradeResult = a_upgradeInterface(&candidate);
		if (upgradeResult != sl::Result::eOk) {
			if (candidate && candidate != original) {
				static_cast<IUnknown*>(candidate)->Release();
			}
			return {
				.status = SwapChainUpgradeStatus::kUpgradeFailed,
				.sdkResult = upgradeResult
			};
		}
		if (!candidate || candidate == original) {
			return {
				.status = SwapChainUpgradeStatus::kProxyNotInstalled,
				.sdkResult = upgradeResult
			};
		}

		auto* proxy = static_cast<IDXGISwapChain*>(candidate);
		void* nativeInterface = nullptr;
		const sl::Result nativeResult =
			a_getNativeInterface(proxy, &nativeInterface);
		if (nativeResult != sl::Result::eOk || !nativeInterface) {
			if (nativeInterface) {
				static_cast<IUnknown*>(nativeInterface)->Release();
			}
			proxy->Release();
			return {
				.status = SwapChainUpgradeStatus::kNativeLookupFailed,
				.sdkResult = nativeResult
			};
		}

		auto* nativeSwapChain =
			static_cast<IDXGISwapChain*>(nativeInterface);
		const bool sameIdentity =
			HasSameIdentity(original, nativeSwapChain);
		nativeSwapChain->Release();
		if (!sameIdentity) {
			proxy->Release();
			return {
				.status = SwapChainUpgradeStatus::kNativeMismatch,
				.sdkResult = nativeResult
			};
		}

		*a_swapChain = proxy;
		original->Release();
		return {
			.status = SwapChainUpgradeStatus::kSuccess,
			.sdkResult = sl::Result::eOk
		};
	}
}
