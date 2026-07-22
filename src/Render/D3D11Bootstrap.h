#pragma once

#include <d3d11.h>

namespace cs::d3d11
{
	void RunBootstrapPostCreate(
		HRESULT a_result,
		IDXGIAdapter* a_adapter,
		const DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
		IDXGISwapChain** a_swapChain,
		ID3D11Device** a_device,
		ID3D11DeviceContext** a_immediateContext);
}
