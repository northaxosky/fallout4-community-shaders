#pragma once

#include <d3d11.h>

#include "Engine.h"

namespace cs::features::sss::Util
{
	// Mirrors Upscaling's enumeration so we read the same depth-stencil slot the engine populates pre-deferred.
	enum class DepthStencilTarget
	{
		kMain = 2,
	};
}
