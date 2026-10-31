#pragma once

#include "Render/PrecipitationOcclusion.h"

namespace cs::engine
{
	// Seeds the capture's culling group with frustum-testable leaves, not whole cells.
	[[nodiscard]] HookInstall InstallOccluderGatherHook();
}
