#pragma once

struct ID3D11DeviceContext;

namespace cs::engine
{
	// Binds MRT0, MRT1, MRT3 and MRT4 at the kGBuffer*Slot registers (FO4/GBufferViews.hlsli)
	// for the draw or dispatch in flight; for light, tiled and composite consumers.
	void BindGBufferViews(ID3D11DeviceContext* a_context) noexcept;
}
