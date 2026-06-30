#include "Render/RendererContext.h"

namespace cs::engine
{
	RE::BSGraphics::Context* GetActiveContext() noexcept
	{
		using Context = RE::BSGraphics::Context;

		// AE 0x038CAA98 / NG 0x03624C98 / OG 0x061DDC68 - the "main" BSGraphics::Context global.
		// Source: Fallout4RE knowledge/cross-runtime/bsgraphics-active-context.md (Option 2).
		// On the render thread the engine itself uses this same global as the fallback in
		// SetDirtyStates/DeferredComposite when TLS+0xB20 is null, and all observed engine
		// SetContext callers pass nullptr (which propagates this global into TLS), so a direct
		// read here matches engine behaviour on every observed call path.
		static REL::Relocation<Context**> ptr{ REL::ID({ 33539, 2704428, 2704428 }) };
		return ptr ? *ptr : nullptr;
	}

	void CopyResourcePreservingOM(ID3D11DeviceContext* a_ctx, ID3D11Resource* a_dst, ID3D11Resource* a_src) noexcept
	{
		if (!a_ctx || !a_dst || !a_src) return;

		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* savedDSV = nullptr;
		a_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);

		ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		a_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTVs, nullptr);

		a_ctx->CopyResource(a_dst, a_src);

		a_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);

		for (auto* rtv : savedRTVs) {
			if (rtv) rtv->Release();
		}
		if (savedDSV) savedDSV->Release();
	}

	OMScope::OMScope(ID3D11DeviceContext* a_ctx) noexcept :
		_ctx(a_ctx),
		_savedRTVs{},
		_savedDSV(nullptr)
	{
		if (!_ctx) return;
		_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _savedRTVs, &_savedDSV);
		_ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}

	OMScope::~OMScope() noexcept
	{
		if (_ctx) {
			_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _savedRTVs, _savedDSV);
		}
		for (auto* rtv : _savedRTVs) {
			if (rtv) rtv->Release();
		}
		if (_savedDSV) _savedDSV->Release();
	}
}
