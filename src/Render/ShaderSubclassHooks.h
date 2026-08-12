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

	struct ShaderSubclassRuntimeLayout
	{
		bool verified = false;
		std::size_t pixelShadersOffset = 0;
	};

	void InstallShaderSubclassHooks();
	ShaderSubclassRuntimeLayout GetShaderSubclassRuntimeLayout() noexcept;
}
