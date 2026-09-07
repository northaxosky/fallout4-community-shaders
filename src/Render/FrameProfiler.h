#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace cs::render::profiling
{
	// Initializes the GPU profiler once from the engine-owned D3D11 device.
	void InitializeD3D11(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context) noexcept;

	// Marks the engine's completed deferred-composite frame. Generated D3D12
	// presentation frames are intentionally outside this boundary.
	void MarkEngineFrame() noexcept;
}
