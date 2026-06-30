#pragma once

#include <d3d11.h>

namespace cs
{
	// RAII guard around CS dispatches. Entry is a no-op; exit clears CS-stage bindings the
	// dispatches touched (SRV/UAV/CB/sampler/shader) so the next CS dispatch starts clean.
	// Width is capped at 8 slots per stage - a full-width null sweep (128 SRVs etc.) stomps
	// engine CS-stage bindings at high slots during deferred passes and produces dark boxes
	// at building positions. Not re-entrant.
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
