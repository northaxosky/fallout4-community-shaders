#pragma once

#include "Render/PrecipitationOcclusion.h"

namespace RE
{
	class BSGeometry;
	class BSLightingShaderProperty;
}

namespace cs::engine
{
	// Replaces BSLightingShaderProperty's occlusion-pass builder during a capture.
	[[nodiscard]] HookInstall InstallOccluderPassHook();

	// The stock BSUtilityShader technique for the occlusion mode, from the same inputs.
	[[nodiscard]] std::uint32_t OcclusionTechnique(const RE::BSLightingShaderProperty& a_property, RE::BSGeometry& a_geometry);
}
