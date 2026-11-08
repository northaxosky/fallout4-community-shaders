#pragma once

#include "Render/EngineCallSite.h"

namespace cs::engine::screenshot
{
	// Logical target the engine's screenshot copy fills and saves.
	inline constexpr auto kTarget = static_cast<RE::BSGraphics::RenderTargetID>(63);

	inline constexpr REL::VariantID kIsScreenshotQueued{ 257450, 2249381 };
	inline constexpr REL::VariantID kCopy{ 1386071, 2316602 };
	inline constexpr REL::VariantID kTakeScreenShot{ 919230, 2229158 };
	inline constexpr REL::VariantID kRunCameraPath{ 635170, 2228936 };

	// Consumes the queued flag, which the hotkey and DevBench both set.
	inline constexpr CallSiteAnchor kSwapQueuedCheck{
		.name = "Main::Swap -> MenuControls::IsScreenshotQueued",
		.function = RE::ID::Main::Swap,
		.offset = { 0xBB, 0xBB, 0xBB },
		.target = kIsScreenshotQueued
	};

	// Copy(0, 63): the frame buffer into the screenshot target, after the interface draws.
	inline constexpr CallSiteAnchor kSwapCopy{
		.name = "Main::Swap -> ImageSpaceManager::Copy",
		.function = RE::ID::Main::Swap,
		.offset = { 0x2D3, 0x2B6, 0x2B6 },
		.target = kCopy
	};
	inline constexpr CallSiteAnchor kCameraPathCopy{
		.name = "Main::RunCameraPath -> ImageSpaceManager::Copy",
		.function = kRunCameraPath,
		.offset = { 0x3EC, 0x3FD, 0x3FD },
		.target = kCopy
	};

	// Saves the screenshot target to a file; skipped when a feature owns the file.
	inline constexpr CallSiteAnchor kSwapTakeScreenShot{
		.name = "Main::Swap -> ScreenShot::TakeScreenShot",
		.function = RE::ID::Main::Swap,
		.offset = { 0x2E3, 0x2C6, 0x2C6 },
		.target = kTakeScreenShot
	};
}
