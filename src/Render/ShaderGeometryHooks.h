#pragma once

#include "Render/ShaderInjectionTargets.h"

#include <cstdint>

namespace RE
{
	class BSRenderPass;
	class BSShader;
}

namespace cs::engine
{
	// Engine order per pass: SetupGeometry, draw flush, RestoreGeometry.
	struct GeometryObserver
	{
		void (*setup)(RE::BSShader*, RE::BSRenderPass*) noexcept;    // before SetupGeometry
		void (*restore)(RE::BSShader*, RE::BSRenderPass*) noexcept;  // after RestoreGeometry
	};

	// Startup only; the first observer of a target hooks its vtable pair once.
	void ObserveShaderGeometry(ShaderInjectionTarget a_target, GeometryObserver a_observer);

	// Receives the raw BSRenderPass::passEnum, ahead of both command-buffer replay
	// and immediate draws.
	struct TechniqueObserver
	{
		void (*setup)(RE::BSShader*, std::uint32_t a_technique) noexcept;  // before SetupTechnique
	};

	// Startup only; the first observer of a target hooks its SetupTechnique once.
	void ObserveShaderTechnique(ShaderInjectionTarget a_target, TechniqueObserver a_observer);
}
