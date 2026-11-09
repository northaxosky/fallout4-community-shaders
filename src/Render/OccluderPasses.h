#pragma once

#include "Render/PrecipitationOcclusion.h"

namespace RE
{
	class BSGeometry;
	class BSLightingShaderProperty;
}

namespace cs::engine
{
	// The mesh LOD mode a pass is built with before any fade node writes it.
	inline constexpr std::int8_t kDefaultLodMode = 3;

	// Replaces BSLightingShaderProperty's occlusion-pass builder during a capture.
	[[nodiscard]] HookInstall InstallOccluderPassHook();

	// Drops the stock raster depth bias from engine-delegated capture draws.
	[[nodiscard]] HookInstall InstallOccluderDepthBiasHook();

	// The stock BSUtilityShader technique for the occlusion mode.
	[[nodiscard]] std::uint32_t OcclusionTechnique(const RE::BSLightingShaderProperty& a_property, RE::BSGeometry& a_geometry);
}
