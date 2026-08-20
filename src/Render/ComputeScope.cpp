#include "Render/ComputeScope.h"

#include "Render/SharedData.h"

namespace cs
{
	ComputeScope::ComputeScope(ID3D11DeviceContext* a_ctx) noexcept :
		_ctx(a_ctx)
	{
		(void)_ctx;
	}

	ComputeScope::~ComputeScope() noexcept
	{
		if (!_ctx) {
			return;
		}

		// Clearing more than eight slots breaks engine bindings.
		constexpr UINT kClearWidth = 8;

		ID3D11ShaderResourceView*  nullSRVs[kClearWidth]     = {};
		ID3D11SamplerState*        nullSamplers[kClearWidth] = {};
		ID3D11UnorderedAccessView* nullUAVs[kClearWidth]     = {};
		ID3D11Buffer*              nullCBs[kClearWidth]      = {};

		_ctx->CSSetShaderResources(0, kClearWidth, nullSRVs);
		_ctx->CSSetSamplers(0, kClearWidth, nullSamplers);
		_ctx->CSSetUnorderedAccessViews(0, kClearWidth, nullUAVs, nullptr);
		if (render::IsSharedDataReady()) {
			_ctx->CSSetConstantBuffers(
				0,
				render::kSharedDataSlot,
				nullCBs);
			_ctx->CSSetConstantBuffers(
				render::kFeatureDataSlot + 1,
				kClearWidth - render::kFeatureDataSlot - 1,
				nullCBs);
		} else {
			_ctx->CSSetConstantBuffers(0, kClearWidth, nullCBs);
		}
		_ctx->CSSetShader(nullptr, nullptr, 0);
	}
}
