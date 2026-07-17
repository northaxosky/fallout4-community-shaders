#pragma once

#include <cstddef>
#include <cstdint>

namespace cs::features::catalog::subclass_hooks
{
	// Idempotently patch ReloadShaders (0x0B) and SetupTechnique (0x02) once to attribute explicit and runtime shader rows.
	void InstallAll(std::size_t a_pixelShadersOffset);

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
