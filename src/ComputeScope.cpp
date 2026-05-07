#include "ComputeScope.h"

namespace cs
{
	ComputeScope::ComputeScope(ID3D11DeviceContext* a_ctx) noexcept :
		_ctx(a_ctx)
	{
		_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _savedRTVs, &_savedDSV);
		_ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}

	ComputeScope::~ComputeScope() noexcept
	{
		ID3D11ShaderResourceView*  nullSRVs[8] = {};
		_ctx->CSSetShaderResources(0, 8, nullSRVs);
		ID3D11SamplerState*        nullSampler[1] = { nullptr };
		_ctx->CSSetSamplers(0, 1, nullSampler);
		ID3D11UnorderedAccessView* nullUAVs[8] = {};
		_ctx->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);
		ID3D11Buffer*              nullCBs[8] = {};
		_ctx->CSSetConstantBuffers(0, 8, nullCBs);
		_ctx->CSSetShader(nullptr, nullptr, 0);

		_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _savedRTVs, _savedDSV);
		for (auto* rtv : _savedRTVs)
			if (rtv) rtv->Release();
		if (_savedDSV) _savedDSV->Release();
	}
}
