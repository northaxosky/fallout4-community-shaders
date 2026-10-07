#pragma once

#include "Render/PrecipitationOcclusion.h"

namespace cs::engine
{
	// Replaces BSLightingShaderProperty's occlusion-pass builder during a capture.
	[[nodiscard]] HookInstall InstallOccluderPassHook();
}
