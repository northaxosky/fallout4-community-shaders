#pragma once

namespace cs::features::true_pbr
{
	// Keeps Tags() in step with the engine's lighting materials: assigned after setup from the
	// template's materialPath, copied with CopyMembers, erased by the destructor.
	// False, after logging, if a hook did not attach.
	[[nodiscard]] bool InstallMaterialTagHooks();
}
