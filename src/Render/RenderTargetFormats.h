#pragma once

#include <cstdint>
#include <dxgiformat.h>

namespace RE::BSGraphics
{
	enum class RenderTargetID : std::int32_t;
}

namespace cs::engine
{
	// Call during Load, before the renderer creates targets; false on conflict.
	[[nodiscard]] bool RequestRenderTargetFormat(RE::BSGraphics::RenderTargetID a_target, DXGI_FORMAT a_format);
}
