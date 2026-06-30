#pragma once

#include <d3d11.h>

#include "Render/ComputeScope.h"

namespace RE::BSGraphics
{
	class Context;
}

namespace cs::engine
{
	// Reads the engine's "main" BSGraphics::Context global. SetDirtyStates and the deferred-pass
	// dispatchers themselves use this same global as their fallback when TLS+0xB20 is null, and
	// every observed engine SetContext call passes nullptr (which just stores this global into
	// TLS+0xB20), so a direct read matches the engine on every known render path.
	//
	// Used as the substitute for RendererData::shadowState, which is permanently nullptr+0x1B70
	// in our environment because renderer init runs before the active context is allocated
	// (FO4RE renderer-shadow-state-dirty.md). Callers must null-check.
	RE::BSGraphics::Context* GetActiveContext() noexcept;

	// CopyResource into a destination that the engine may currently have bound as an RTV.
	// We save the current OM bindings, null them, run the copy, then restore the same pointers.
	// CopyResource into a still-bound RTV triggers an implicit unbind that leaves the engine's
	// next draw writing into a NULL slot. Saving and restoring around the copy keeps the
	// engine's RT chain coherent. OM round-trip with identical pointers is inert.
	void CopyResourcePreservingOM(ID3D11DeviceContext* a_ctx, ID3D11Resource* a_dst, ID3D11Resource* a_src) noexcept;

	// RAII: save OM (all RTVs + DSV), unbind for the lifetime of the scope, restore on dtor.
	// Use whenever a compute dispatch needs to bind a resource as SRV that the engine has currently
	// bound at OM as RTV/DSV. D3D11 resolves that hazard by silently nulling the SRV slot, so the
	// dispatch ends up reading zeros (Bend's start_depth==0 path, or Apply pulling black diffuse).
	// Saving+restoring around the dispatch unbinds the conflicting OM target so the SRV bind sticks,
	// then puts the same pointers back so the engine's next pass continues with its expected OM.
	// Construct OMScope BEFORE any ComputeScope so the CS-stage clear runs first on exit and OM
	// restore doesn't fight a still-bound CS SRV.
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

	// Combined OM+CS guard for compute dispatches that sample engine render targets. Member order
	// makes the contract correct by construction: _om constructs first (save+unbind OM) and, on
	// reverse-order destruction, _cs clears the CS slots first, then _om restores OM. Prefer this
	// over hand-pairing OMScope + ComputeScope. Nesting is safe (the inner guard is a redundant
	// no-op), but a caller relying on its own CS bindings surviving a nested dispatch is not.
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
