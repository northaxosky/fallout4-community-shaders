#include "Render/ComputeScope.h"

#include <algorithm>

namespace cs
{
	ComputeScope::ComputeScope(
		ID3D11DeviceContext* a_ctx,
		UINT a_srvCount,
		UINT a_samplerCount,
		UINT a_uavCount,
		UINT a_constantBufferCount) noexcept :
		_ctx(a_ctx),
		_srvCount(std::min(a_srvCount, 8u)),
		_samplerCount(std::min(a_samplerCount, 8u)),
		_uavCount(std::min(a_uavCount, 8u)),
		_constantBufferCount(std::min(a_constantBufferCount, 8u))
	{
		(void)_ctx;
	}

	ComputeScope::~ComputeScope() noexcept
	{
		if (!_ctx) {
			return;
		}

		constexpr UINT kClearWidth = 8;

		ID3D11ShaderResourceView*  nullSRVs[kClearWidth]     = {};
		ID3D11SamplerState*        nullSamplers[kClearWidth] = {};
		ID3D11UnorderedAccessView* nullUAVs[kClearWidth]     = {};
		ID3D11Buffer*              nullCBs[kClearWidth]      = {};

		if (_srvCount) {
			_ctx->CSSetShaderResources(0, _srvCount, nullSRVs);
		}
		if (_samplerCount) {
			_ctx->CSSetSamplers(0, _samplerCount, nullSamplers);
		}
		if (_uavCount) {
			_ctx->CSSetUnorderedAccessViews(0, _uavCount, nullUAVs, nullptr);
		}
		if (_constantBufferCount) {
			_ctx->CSSetConstantBuffers(0, _constantBufferCount, nullCBs);
		}
		_ctx->CSSetShader(nullptr, nullptr, 0);
	}
}
