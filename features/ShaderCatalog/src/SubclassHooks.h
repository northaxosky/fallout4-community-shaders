#pragma once

#include <cstdint>

namespace cs::features::catalog::subclass_hooks
{
	// Patches ReloadShaders (slot 0x0B) and SetupTechnique (slot 0x02) on each known concrete
	// BSShader subclass. ReloadShaders attributes explicit reloads; SetupTechnique retroactively
	// attributes runtime rows from the subclass's pixelShaders map.
	//
	// Idempotent: guarded by a process-wide once-flag.
	void InstallAll();

	// Diagnostics surface for the feature panel.
	struct InstallStats
	{
		unsigned attempted = 0;
		unsigned succeeded = 0;
		unsigned failed    = 0;
	};
	InstallStats GetInstallStats();
	InstallStats GetReloadInstallStats();
	InstallStats GetSetupTechniqueInstallStats();

	struct RuntimeStats
	{
		std::uint64_t setupTechniqueCalls = 0;
		std::uint64_t mapAttributions     = 0;
	};
	RuntimeStats GetRuntimeStats();
}
