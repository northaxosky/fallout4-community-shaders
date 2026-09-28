#pragma once

#include "Render/EngineCallSite.h"

namespace cs::render::temporal_anchors
{
	inline constexpr engine::CallSiteAnchor kMainLoopMessageLoopCall{
		.name = "Main::MainLoop -> Main::WindowsMessageLoop",
		.function = REL::ID({ 1125396, 2718225, 4484191 }),
		.offset = { 0x1E, 0x4E, 0x2E },
		.target = REL::ID({ 847266, 2228915, 2228915 })
	};

	inline constexpr engine::CallSiteAnchor kOnIdleSwapCall{
		.name = "Main::OnIdle -> Main::Swap",
		.function = REL::ID({ 633524, 2228917, 2228917 }),
		.offset = { 0x6EC, 0xCDC, 0xCDC },
		.target = REL::ID({ 1075087, 2228913, 2228913 })
	};
}
