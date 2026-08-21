#pragma once

#include <cstddef>

namespace cs::engine
{
	struct ShaderSubclassHookInstallStats
	{
		unsigned attempted = 0;
		unsigned succeeded = 0;
		unsigned failed = 0;
	};

	void InstallShaderSubclassHooks();
}
