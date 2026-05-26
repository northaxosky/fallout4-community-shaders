#pragma once

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
}
