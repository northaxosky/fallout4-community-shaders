#pragma once

#include <d3d11.h>

#include "Render/ComputeScope.h"

namespace RE::BSGraphics
{
	class Context;
}

namespace cs::engine
{
	// May be null during renderer initialization.
	RE::BSGraphics::Context* GetActiveContext() noexcept;

	// Preserve bound RTVs across copies.
	void CopyResourcePreservingOM(ID3D11DeviceContext* a_ctx, ID3D11Resource* a_dst, ID3D11Resource* a_src) noexcept;

	// Unbind OM before compute sampling; restore it last.
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

	// Prefer unless nested CS bindings must survive.
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
		OMScope          _om;  // Declared first so OM restores last.
		cs::ComputeScope _cs;  // Declared second so CS clears first.
	};
}
