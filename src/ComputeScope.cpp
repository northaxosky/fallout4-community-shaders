#include "ComputeScope.h"

namespace cs
{
	ComputeScope::ComputeScope(ID3D11DeviceContext* a_ctx) noexcept :
		_ctx(a_ctx)
	{
		(void)_ctx;
	}

	ComputeScope::~ComputeScope() noexcept
	{
		// CS-stage hygiene: clear the slots our dispatches likely touched so the next CS
		// dispatch starts clean. Width is capped at 8 - widening to the D3D11 maximums
		// (128 SRVs / 14 CBs / 16 samplers) caused dark boxes at building positions whenever
		// SSGI or SSS was enabled, because the engine has CS-stage resources bound at high
		// slots during deferred passes and a full-width null sweep stomps them.
		constexpr UINT kClearWidth = 8;

		ID3D11ShaderResourceView*  nullSRVs[kClearWidth]     = {};
		ID3D11SamplerState*        nullSamplers[kClearWidth] = {};
		ID3D11UnorderedAccessView* nullUAVs[kClearWidth]     = {};
		ID3D11Buffer*              nullCBs[kClearWidth]      = {};

		_ctx->CSSetShaderResources(0, kClearWidth, nullSRVs);
		_ctx->CSSetSamplers(0, kClearWidth, nullSamplers);
		_ctx->CSSetUnorderedAccessViews(0, kClearWidth, nullUAVs, nullptr);
		_ctx->CSSetConstantBuffers(0, kClearWidth, nullCBs);
		_ctx->CSSetShader(nullptr, nullptr, 0);
	}
}
