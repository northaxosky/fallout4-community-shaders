#pragma once

#include "Render/EngineCallSite.h"

namespace cs::engine::screenshot
{
	// Consumes the queued flag, which the hotkey and DevBench both set.
	inline constexpr CallSiteAnchor kSwapQueuedCheck{
		.name = "Main::Swap -> MenuControls::IsScreenshotQueued",
		.function = RE::ID::Main::Swap,
		.offset = { 0xBB, 0xBB, 0xBB },
		.target = RE::ID::MenuControls::IsScreenshotQueued
	};

	// Copy(0, kScreenshot): the frame buffer into the screenshot target.
	inline constexpr CallSiteAnchor kSwapCopy{
		.name = "Main::Swap -> ImageSpaceManager::Copy",
		.function = RE::ID::Main::Swap,
		.offset = { 0x2D3, 0x2B6, 0x2B6 },
		.target = RE::ID::ImageSpaceManager::Copy
	};
	inline constexpr CallSiteAnchor kCameraPathCopy{
		.name = "Main::RunCameraPath -> ImageSpaceManager::Copy",
		.function = RE::ID::Main::RunCameraPath,
		.offset = { 0x3EC, 0x3FD, 0x3FD },
		.target = RE::ID::ImageSpaceManager::Copy
	};

	// Saves the screenshot target; skipped when a feature owns the file.
	inline constexpr CallSiteAnchor kSwapTakeScreenShot{
		.name = "Main::Swap -> ScreenShot::TakeScreenShot",
		.function = RE::ID::Main::Swap,
		.offset = { 0x2E3, 0x2C6, 0x2C6 },
		.target = RE::ID::ScreenShot::TakeScreenShot
	};
}
