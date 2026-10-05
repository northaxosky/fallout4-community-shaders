#pragma once

#include <cstddef>
#include <cstdint>

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

	// Per-draw state for the deferred prepass. The render thread calls classify exactly once per
	// BSDFPrePassShader draw, in draw order, then apply with that class before the draw is issued.
	// Class 0 is the neutral state: it is applied for draws without a prepass pass.
	struct PrepassDrawObserver
	{
		std::uint32_t (*classify)(RE::BSRenderPass* a_pass) noexcept;
		void (*apply)(std::uint32_t a_class) noexcept;
	};

	// Load or OnPostPostLoad only; the first call patches the engine, and false means no observer was installed.
	// Covers both the immediate SetupGeometry path and command-buffer replay.
	[[nodiscard]] bool RegisterPrepassDrawObserver(PrepassDrawObserver a_observer);
}
