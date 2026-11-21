#pragma once

#include "Render/Extent.h"

namespace cs::render
{
	// One world frame's resolution spaces, latched with the world camera.
	struct RenderResolution
	{
		Extent output;
		Extent render;
		float widthRatio = 1.0f;
		float heightRatio = 1.0f;
		float previousWidthRatio = 1.0f;
		float previousHeightRatio = 1.0f;

		[[nodiscard]] bool Scaled() const noexcept { return render != output; }
	};

	// The live engine ratio right now; unlike the latch it follows scope changes.
	[[nodiscard]] RenderResolution ReadLiveResolution() noexcept;

	// Native outside a world frame (menus, loading, startup).
	[[nodiscard]] RenderResolution GetRenderResolution() noexcept;
}
