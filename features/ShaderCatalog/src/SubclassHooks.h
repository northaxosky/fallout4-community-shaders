#pragma once

#include <cstdint>

namespace cs::features::catalog::subclass_hooks
{
	// Patch ReloadShaders (0x0B) and SetupTechnique (0x02) to attribute explicit and runtime shader rows.
	// Idempotent: guarded by a process-wide once-flag.
	void InstallAll();

	struct InstallStats
	{
		unsigned attempted = 0;
		unsigned succeeded = 0;
		unsigned failed    = 0;
	};
	InstallStats GetReloadInstallStats();
	InstallStats GetSetupTechniqueInstallStats();

	struct RuntimeStats
	{
		std::uint64_t setupTechniqueCalls = 0;
		std::uint64_t mapAttributions     = 0;
	};
	RuntimeStats GetRuntimeStats();
}
