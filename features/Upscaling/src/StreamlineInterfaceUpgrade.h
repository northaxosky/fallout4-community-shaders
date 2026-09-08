#pragma once

#include <dxgi.h>

#include <sl_core_api.h>

namespace cs::features::streamline
{
	enum class SwapChainUpgradeStatus
	{
		kSuccess,
		kInvalidArgument,
		kIneligible,
		kIncompatibleDevice,
		kUpgradeFailed,
		kProxyNotInstalled,
		kNativeLookupFailed,
		kNativeMismatch
	};

	struct SwapChainUpgradeResult
	{
		SwapChainUpgradeStatus status = SwapChainUpgradeStatus::kInvalidArgument;
		sl::Result sdkResult = sl::Result::eOk;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return status == SwapChainUpgradeStatus::kSuccess;
		}
	};

	[[nodiscard]] bool ShouldInstallSwapChainProxy(
		bool a_initialized,
		bool a_deviceRegistered,
		bool a_dlssAdmitted,
		bool a_d3d12Session) noexcept;

	[[nodiscard]] SwapChainUpgradeResult UpgradeD3D11SwapChain(
		PFun_slUpgradeInterface* a_upgradeInterface,
		PFun_slGetNativeInterface* a_getNativeInterface,
		IDXGISwapChain** a_swapChain) noexcept;
}
