#pragma once

#include "Render/RenderHooks.h"

#include <cstdint>

namespace RE
{
	class BSGeometry;
	class BSLightingShaderProperty;
	class BSShaderAccumulator;
}

namespace cs::engine
{
	struct LightingRenderPassesCall
	{
		RE::BSLightingShaderProperty* property;
		RE::BSGeometry* geometry;
		std::uint32_t renderMode;
		RE::BSShaderAccumulator* accumulator;
	};

	// Runs before the engine builds or reuses the property's pass list.
	using PreLightingRenderPassesCallback = void (*)(const LightingRenderPassesCall&);

	// Register in Load/OnPostPostLoad; callbacks run on accumulation threads.
	bool RegisterPreLightingRenderPasses(PreLightingRenderPassesCallback a_callback, HookPriority a_priority = HookPriority::Default);
	bool RegisterPreLightingRenderPasses(Feature& a_owner, PreLightingRenderPassesCallback a_callback, HookPriority a_priority = HookPriority::Default);
}
