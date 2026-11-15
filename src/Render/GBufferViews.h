#pragma once

struct ID3D11DeviceContext;

namespace cs::engine
{
	// For stages that do not bind the G-buffer themselves.
	void BindGBufferViews(ID3D11DeviceContext* a_context) noexcept;
}
