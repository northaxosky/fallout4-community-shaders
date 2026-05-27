#pragma once

#include <d3d11.h>

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
}
