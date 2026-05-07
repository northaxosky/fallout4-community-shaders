#pragma once

#include <d3d11.h>

namespace cs
{
	// RAII: saves+unbinds OM on entry; clears CS-stage bindings and restores OM on exit. Not re-entrant.
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
