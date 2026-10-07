#pragma once

#include "Render/PrecipitationOcclusion.h"

namespace cs::engine
{
	// Replaces the occlusion-pass builder of BSLightingShaderProperty while a capture runs.
	[[nodiscard]] HookInstall InstallOccluderPassHook();
}
