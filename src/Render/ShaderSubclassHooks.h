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

	// The engine path that baked a prepass draw's per-draw constants.
	enum class PrepassBakePath
	{
		kCommandBuffer,  // once, when the pass is created
		kImmediate       // every frame, from BSDFPrePassShader::SetupGeometry
	};

	// Classifies a BSDFPrePassShader draw; the host bakes the answer into the lane DFPrepass.hlsl reads as cb2_pad.x
	// of every non-landscape prepass draw. Runs on the thread that creates the pass or issues the draw.
	using PrepassDrawClassifier = bool (*)(RE::BSRenderPass* a_pass, PrepassBakePath a_path) noexcept;

	struct PrepassLaneStats
	{
		std::uint64_t baked = 0;        // draws whose lane was written
		std::uint64_t unavailable = 0;  // non-landscape draws whose pixel shader has no lane
	};

	// Load or OnPostPostLoad only; the first call patches the engine. Returns false when the patches failed or
	// a classifier is already registered, and then no draw carries a lane.
	[[nodiscard]] bool RegisterPrepassDrawClassifier(PrepassDrawClassifier a_classifier);
	[[nodiscard]] PrepassLaneStats GetPrepassLaneStats() noexcept;
}
