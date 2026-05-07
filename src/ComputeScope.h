#pragma once

#include <d3d11.h>

namespace cs
{
	// Saves the OM render-target binding on construction and unbinds RTVs/DSV so compute work
	// can run without bind-conflicts on resources currently bound as RTV/DSV. On destruction:
	// clears CS-stage bindings (SRVs, sampler, UAV, CB, shader) so the next pass doesn't see
	// ours, restores the saved OM state, releases the AddRef'd RTVs/DSV.
	class ComputeScope
	{
	public:
		explicit ComputeScope(ID3D11DeviceContext* a_ctx) noexcept;
		~ComputeScope() noexcept;

		ComputeScope(const ComputeScope&)            = delete;
		ComputeScope& operator=(const ComputeScope&) = delete;
		ComputeScope(ComputeScope&&)                 = delete;
		ComputeScope& operator=(ComputeScope&&)      = delete;

	private:
		ID3D11DeviceContext*    _ctx;
		ID3D11RenderTargetView* _savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* _savedDSV = nullptr;
	};
}
