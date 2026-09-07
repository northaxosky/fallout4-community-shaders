#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <functional>
#include <optional>
#include <cstdint>
#include <vector>

namespace cs::render
{
	using CreateDeviceAndSwapChain = decltype(&D3D11CreateDeviceAndSwapChain);

	struct CreateDeviceAndSwapChainContext
	{
		CreateDeviceAndSwapChain realCreate;
		IDXGIAdapter* adapter;
		D3D_DRIVER_TYPE driverType;
		HMODULE software;
		UINT flags;
		const D3D_FEATURE_LEVEL* featureLevels;
		UINT featureLevelCount;
		UINT sdkVersion;
		const DXGI_SWAP_CHAIN_DESC* swapChainDesc;
		IDXGISwapChain** swapChain;
		ID3D11Device** device;
		D3D_FEATURE_LEVEL* featureLevel;
		ID3D11DeviceContext** immediateContext;
	};

	// Runs before the real creation call; may adjust the descriptor and requested feature levels.
	using PreCreateDeviceCallback =
		std::function<void(DXGI_SWAP_CHAIN_DESC*, std::vector<D3D_FEATURE_LEVEL>&)>;

	// Runs before the D3D bootstrap so a callback may upgrade the device and swap chain in place.
	using PostCreateDeviceCallback =
		std::function<void(IDXGIAdapter*, ID3D11Device**, IDXGISwapChain**)>;
	using ReplacementCreateDeviceCallback =
		std::function<std::optional<HRESULT>(CreateDeviceAndSwapChainContext&)>;

	enum class SwapChainHookState : std::uint8_t
	{
		kUnattempted,
		kInstalling,
		kInstalled,
		kFailed
	};

	// Register only on the startup thread, before the swap chain is created.
	bool RegisterPreCreateDeviceAndSwapChain(PreCreateDeviceCallback a_callback);
	bool RegisterPostCreateDeviceAndSwapChain(PostCreateDeviceCallback a_callback);
	bool RegisterReplacementCreateDeviceAndSwapChain(ReplacementCreateDeviceCallback a_callback);

	bool InstallSwapChainHook();
	[[nodiscard]] SwapChainHookState GetSwapChainHookState() noexcept;
}
