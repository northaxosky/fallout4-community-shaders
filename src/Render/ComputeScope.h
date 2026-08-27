#pragma once

#include <d3d11.h>

namespace cs
{
	// Clears eight slots; wider sweeps break engine bindings. Not re-entrant.
	class ComputeScope
	{
	public:
		explicit ComputeScope(
			ID3D11DeviceContext* a_ctx,
			UINT a_srvCount = 8,
			UINT a_samplerCount = 8,
			UINT a_uavCount = 8,
			UINT a_constantBufferCount = 8) noexcept;
		~ComputeScope() noexcept;

		ComputeScope(const ComputeScope&)            = delete;
		ComputeScope& operator=(const ComputeScope&) = delete;
		ComputeScope(ComputeScope&&)                 = delete;
		ComputeScope& operator=(ComputeScope&&)      = delete;

	private:
		ID3D11DeviceContext* _ctx;
		UINT _srvCount;
		UINT _samplerCount;
		UINT _uavCount;
		UINT _constantBufferCount;
	};
}
