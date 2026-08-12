#pragma once

#include <d3d11.h>

namespace cs
{
	// Clears eight slots; wider sweeps break engine bindings. Not re-entrant.
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
