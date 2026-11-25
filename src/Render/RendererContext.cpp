#include "Render/RendererContext.h"

#include <array>
#include <cstring>

namespace cs::engine
{
	RE::BSGraphics::Context* GetActiveContext() noexcept
	{
		return RE::BSGraphics::GetGlobalContext();
	}

	void CopyResourcePreservingOM(ID3D11DeviceContext* a_ctx, ID3D11Resource* a_dst, ID3D11Resource* a_src) noexcept
	{
		if (!a_ctx || !a_dst || !a_src)
			return;

		OMScope om(a_ctx);
		a_ctx->CopyResource(a_dst, a_src);
	}

	HRESULT WriteDiscard(ID3D11DeviceContext* a_ctx, ID3D11Buffer* a_buffer, const void* a_data, std::size_t a_size) noexcept
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const auto result = a_ctx->Map(a_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(result))
			return result;
		std::memcpy(mapped.pData, a_data, a_size);
		a_ctx->Unmap(a_buffer, 0);
		return S_OK;
	}

	OMScope::OMScope(ID3D11DeviceContext* a_ctx) noexcept :
		_ctx(a_ctx),
		_savedRTVs{},
		_savedDSV(nullptr)
	{
		if (!_ctx)
			return;
		_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _savedRTVs, &_savedDSV);
		_ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}

	OMScope::~OMScope() noexcept
	{
		if (_ctx) {
			_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _savedRTVs, _savedDSV);
		}
		for (auto* rtv : _savedRTVs) {
			if (rtv)
				rtv->Release();
		}
		if (_savedDSV)
			_savedDSV->Release();
	}
}
