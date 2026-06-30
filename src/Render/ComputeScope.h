#pragma once

#include <d3d11.h>

namespace cs
{
	// Clears only 8 CS slots; wider sweeps stomp engine high-slot bindings and cause dark boxes.
	// Not re-entrant.
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
		ID3D11DeviceContext* _ctx;
	};
}
