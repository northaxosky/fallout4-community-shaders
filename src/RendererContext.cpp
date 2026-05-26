#include "RendererContext.h"

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
}
