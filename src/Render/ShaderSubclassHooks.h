#pragma once

#include "Render/EnginePixelShaderIdentity.h"
#include "Render/PixelShaderSwapBroker.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::engine
{
	struct ShaderSubclassSetupObservation
	{
		void* shader = nullptr;
		std::string_view subclass;
		std::uint32_t rawTechnique = 0;
		EnginePixelShaderIdentityResult enginePixelShader;
		std::optional<ShaderVariantId> pluginResolvedPsid;
		std::optional<bool> tiledLighting;
	};

	struct ShaderSubclassHookInstallStats
	{
		unsigned attempted = 0;
		unsigned succeeded = 0;
		unsigned failed = 0;
	};

	struct ShaderSubclassHookRuntimeStats
	{
		std::uint64_t setupTechniqueCalls = 0;
	};

	struct ShaderSubclassRuntimeLayout
	{
		bool verified = false;
		std::size_t pixelShadersOffset = 0;
	};

	using ShaderSubclassSetupObserver = void (*)(
		const ShaderSubclassSetupObservation& a_observation) noexcept;

	void InstallShaderSubclassHooks();
	bool RegisterShaderSubclassSetupObserver(
		ShaderSubclassSetupObserver a_observer) noexcept;
	ShaderSubclassRuntimeLayout GetShaderSubclassRuntimeLayout() noexcept;
	ShaderSubclassHookInstallStats GetReloadShaderHookInstallStats() noexcept;
	ShaderSubclassHookInstallStats GetSetupTechniqueHookInstallStats() noexcept;
	ShaderSubclassHookRuntimeStats GetShaderSubclassHookRuntimeStats() noexcept;
}
