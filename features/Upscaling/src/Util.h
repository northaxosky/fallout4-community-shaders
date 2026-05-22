#pragma once

#include <d3d11.h>

#include "Engine.h"

namespace cs::features::upscaling
{

namespace Util
{

	using RenderTarget = cs::engine::RenderTarget;
	using DepthStencilTarget = cs::engine::DepthStencilTarget;

	// Thin shims kept for back-compat. New code should call cs::engine::dynres::* directly.
	inline void SetDynamicResolution(RE::BSGraphics::RenderTargetManager* rtm, float width, float height, bool activated)
	{
		cs::engine::dynres::Set(rtm, width, height, activated);
	}

	[[nodiscard]] inline float GetGameDynamicWidthRatio(RE::BSGraphics::RenderTargetManager* rtm)
	{
		return cs::engine::dynres::GetWidthRatio(rtm);
	}

	[[nodiscard]] inline float GetGameDynamicHeightRatio(RE::BSGraphics::RenderTargetManager* rtm)
	{
		return cs::engine::dynres::GetHeightRatio(rtm);
	}

}

}
