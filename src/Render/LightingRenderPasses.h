#pragma once

#include "Render/RenderHooks.h"

#include "RE/B/BSShaderProperty.h"

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

	// Runs after the pass list is built or reused; a_passes may be null.
	using PostLightingRenderPassesCallback = void (*)(const LightingRenderPassesCall&, const RE::BSShaderProperty::RenderPassArray* a_passes);

	// Register in Load/OnPostPostLoad; callbacks run on accumulation threads.
	bool RegisterPreLightingRenderPasses(PreLightingRenderPassesCallback a_callback, HookPriority a_priority = HookPriority::Default);
	bool RegisterPreLightingRenderPasses(Feature& a_owner, PreLightingRenderPassesCallback a_callback, HookPriority a_priority = HookPriority::Default);
	bool RegisterPostLightingRenderPasses(Feature& a_owner, PostLightingRenderPassesCallback a_callback, HookPriority a_priority = HookPriority::Default);
}
