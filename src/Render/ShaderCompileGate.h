#pragma once

#include "Render/ShaderCompileBatch.h"

#include <chrono>

namespace cs::engine
{
	// Also the overlay reveal delay: a fast warm cache never holds or shows.
	inline constexpr std::chrono::milliseconds kShaderCompileHoldDelay{ 1000 };

	[[nodiscard]] constexpr bool BatchHoldsLoads(const ShaderCompileBatch& a_batch) noexcept
	{
		return a_batch.active && a_batch.duration >= kShaderCompileHoldDelay;
	}

	// Logs one summary line per finished compile batch; call once at startup.
	void InstallShaderCompileSummaryLog();

	// Defers MainMenu loads while shaders compile; call once during plugin load.
	void InstallShaderCompileLoadGate();

	// False when background_compilation lets play start while shaders compile.
	[[nodiscard]] bool ShaderCompileBlocksPlay();

	// True while MainMenu loads are deferred; fails open when quitting.
	[[nodiscard]] bool ShaderCompileHoldsLoads();

	// Frame thread; cancels pending compiles once the game is quitting.
	void StopShaderCompileOnQuit() noexcept;
}
