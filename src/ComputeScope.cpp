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
		// Widened past 8 because SSGI's gi.cs binds 10 SRVs and other features grow over time;
		// leaking high slots silently corrupts the engine's next draw. D3D11 max-per-stage is 128 SRVs / 14 CBs / 16 samplers / 8 UAVs.
		constexpr UINT kSRVSlots = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;  // 128
		constexpr UINT kUAVSlots = D3D11_PS_CS_UAV_REGISTER_COUNT;                // 8
		constexpr UINT kCBSlots  = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;  // 14
		constexpr UINT kSampSlots = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;        // 16

		ID3D11ShaderResourceView*  nullSRVs[kSRVSlots] = {};
		_ctx->CSSetShaderResources(0, kSRVSlots, nullSRVs);
		ID3D11SamplerState*        nullSamplers[kSampSlots] = {};
		_ctx->CSSetSamplers(0, kSampSlots, nullSamplers);
		ID3D11UnorderedAccessView* nullUAVs[kUAVSlots] = {};
		_ctx->CSSetUnorderedAccessViews(0, kUAVSlots, nullUAVs, nullptr);
		ID3D11Buffer*              nullCBs[kCBSlots] = {};
		_ctx->CSSetConstantBuffers(0, kCBSlots, nullCBs);
		_ctx->CSSetShader(nullptr, nullptr, 0);

		_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _savedRTVs, _savedDSV);
		for (auto* rtv : _savedRTVs)
			if (rtv) rtv->Release();
		if (_savedDSV) _savedDSV->Release();
	}
}
