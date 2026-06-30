#pragma once

#include <d3d11.h>

#include "Render/ComputeScope.h"

namespace RE::BSGraphics
{
	class Context;
}

namespace cs::engine
{
	// Reads the engine's main Context global, matching its TLS+0xB20 fallback on observed render paths.
	// Substitute for null RendererData::shadowState; renderer init allocates it too early, so null-check.
	RE::BSGraphics::Context* GetActiveContext() noexcept;

	// Copy into possibly-bound RTVs without leaving the engine's next draw targeting a NULL OM slot.
	void CopyResourcePreservingOM(ID3D11DeviceContext* a_ctx, ID3D11Resource* a_dst, ID3D11Resource* a_src) noexcept;

	// Save+unbind OM while compute samples engine RTV/DSV resources; otherwise D3D11 nulls the SRV and reads black.
	// Construct OMScope BEFORE ComputeScope so CS clears first on exit, then OM restores safely.
	class OMScope
	{
	public:
		explicit OMScope(ID3D11DeviceContext* a_ctx) noexcept;
		~OMScope() noexcept;

		OMScope(const OMScope&) = delete;
		OMScope(OMScope&&) = delete;
		OMScope& operator=(const OMScope&) = delete;
		OMScope& operator=(OMScope&&) = delete;

	private:
		ID3D11DeviceContext*    _ctx;
		ID3D11RenderTargetView* _savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
		ID3D11DepthStencilView* _savedDSV;
	};

	// Combined guard: member order unbinds OM first, then destruction clears CS before restoring OM.
	// Prefer this over hand-pairing; nesting is safe unless callers expect CS bindings to survive.
	class ComputeOMScope
	{
	public:
		explicit ComputeOMScope(ID3D11DeviceContext* a_ctx) noexcept :
			_om(a_ctx), _cs(a_ctx)
		{}

		ComputeOMScope(const ComputeOMScope&)            = delete;
		ComputeOMScope(ComputeOMScope&&)                 = delete;
		ComputeOMScope& operator=(const ComputeOMScope&) = delete;
		ComputeOMScope& operator=(ComputeOMScope&&)      = delete;

	private:
		OMScope          _om;  // declared first: ctor save+unbind OM, dtor restores OM (runs 2nd)
		cs::ComputeScope _cs;  // declared second: ctor no-op, dtor clears CS slots (runs 1st)
	};
}
