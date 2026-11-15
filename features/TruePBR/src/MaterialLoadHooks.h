#pragma once

namespace cs::features::true_pbr
{
	// Hooks the BGSM load: Create names the resource, LoadBinary dispatches by version.
	// False, after logging, if either hook did not attach.
	[[nodiscard]] bool InstallMaterialLoadHooks();
}
