#pragma once

#include <cstddef>

namespace RE
{
	class BSRenderPass;
}

namespace cs::engine
{
	struct ShaderSubclassHookInstallStats
	{
		unsigned attempted = 0;
		unsigned succeeded = 0;
		unsigned failed = 0;
	};

	void InstallShaderSubclassHooks();

	// Runs on the render thread before each native BSDFPrePassShader::SetupGeometry.
	using PrepassGeometryObserver = void (*)(RE::BSRenderPass* a_pass) noexcept;

	// Load or OnPostPostLoad only; the first call patches the vtable, and false means no observer was installed.
	[[nodiscard]] bool RegisterPrepassGeometryObserver(PrepassGeometryObserver a_observer);
}
