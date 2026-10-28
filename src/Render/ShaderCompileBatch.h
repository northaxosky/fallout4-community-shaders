#pragma once

#include <chrono>
#include <cstdint>

namespace cs::engine
{
	// A batch runs from the first queued variant to the last one finishing.
	struct ShaderCompileBatch
	{
		std::uint32_t completedTasks = 0;
		std::uint32_t failedTasks = 0;
		std::uint32_t totalTasks = 0;
		std::uint32_t cacheHits = 0;
		std::chrono::milliseconds duration{};
		bool active = false;

		[[nodiscard]] std::uint32_t FinishedTasks() const noexcept
		{
			return completedTasks + failedTasks;
		}

		[[nodiscard]] std::uint32_t CompiledTasks() const noexcept
		{
			return cacheHits < completedTasks ? completedTasks - cacheHits : 0;
		}
	};
}
