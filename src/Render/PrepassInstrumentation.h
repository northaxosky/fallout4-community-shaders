#pragma once

#include "Render/PixelShaderSwapBroker.h"

#include <cstdint>

namespace RE
{
	class BSRenderPass;
	class BSShader;
}

namespace cs::engine::prepass_instrumentation
{
	struct Snapshot
	{
		std::uint64_t setupTechniques = 0;
		std::uint64_t completedTechniques = 0;
		std::uint64_t zeroDrawTechniques = 0;
		std::uint64_t multiDrawTechniques = 0;
		std::uint64_t draws = 0;
		std::uint64_t objectLodDraws = 0;
		std::uint64_t bitClearDraws = 0;
		std::uint64_t setupReentries = 0;
		std::uint64_t missingTechniqueDraws = 0;
		std::uint64_t missingVertexIdentities = 0;
		std::uint64_t missingPixelIdentities = 0;
		std::uint64_t ambiguousShaderIdentities = 0;
		std::uint64_t maxDrawsPerTechnique = 0;
		std::uint64_t lastObjectLodFrame = 0;
		bool enabled = false;
		bool hooksInstalled = false;
		bool shaderTrackingInstalled = false;
	};

	bool InstallShaderTracking();
	void SetHooksInstalled(bool a_installed) noexcept;
	void OnSetupTechnique(
		RE::BSShader* a_shader,
		std::uint32_t a_rawTechnique,
		bool a_succeeded) noexcept;
	void OnRestoreTechnique(RE::BSShader* a_shader) noexcept;
	void OnSetupGeometry(
		RE::BSShader* a_shader,
		RE::BSRenderPass* a_pass) noexcept;
	Snapshot GetSnapshot() noexcept;
}
