#pragma once

#include "Render/FrameProfiler.h"

#include <cstdint>

namespace Util
{
	class GraphSampleGate
	{
	private:
		std::uint64_t lastFrame = UINT64_MAX;
		std::uint32_t warmupFrames = 0;

	public:
		// Covers the open hitch plus the profiler's 3-frame readback latency.
		static constexpr std::uint32_t kWarmupFrames = 10;

		/**
		 * @brief Whether the graph should record a sample this frame.
		 * @param frame Current frame number.
		 * @return False for repeat calls within a frame and while warming up after a skipped frame.
		 */
		bool ShouldSample(std::uint64_t frame)
		{
			if (frame == lastFrame)
				return false;
			if (frame != lastFrame + 1)
				warmupFrames = kWarmupFrames;
			lastFrame = frame;
			if (warmupFrames == 0)
				return true;
			warmupFrames--;
			return false;
		}

		/** @brief ShouldSample() for the current render frame. */
		// FO4: the profiler's frame sequence stands in for the engine frame counter.
		bool ShouldSample() { return ShouldSample(cs::render::profiling::FrameSequence()); }
	};
}
