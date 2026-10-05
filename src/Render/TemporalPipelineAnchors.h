#pragma once

#include "Render/EngineCallSite.h"

namespace cs::render::temporal_anchors
{
	inline constexpr engine::CallSiteAnchor kMainLoopMessageLoopCall{
		.name = "Main::MainLoop -> Main::WindowsMessageLoop",
		.function = RE::ID::Main::Run,
		.offset = { 0x1E, 0x4E, 0x2E },
		.target = RE::ID::Main::Run_WindowsMessageLoop
	};

	inline constexpr engine::CallSiteAnchor kOnIdleSwapCall{
		.name = "Main::OnIdle -> Main::Swap",
		.function = RE::ID::Main::OnIdle,
		.offset = { 0x6EC, 0xCDC, 0xCDC },
		.target = RE::ID::Main::Swap
	};
}
