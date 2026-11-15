#pragma once
#include "Render/ShaderDefineProvider.h"

#include <cstdint>

namespace cs::features::vanilla_pbr
{
	// The composite's matched live LOD table, beside Dynamic Cubemaps' t39-t41.
	inline constexpr std::uint32_t kLiveLODPSSlot = 42;
	// The composite's native material cube array, which blur matching profiles.
	inline constexpr std::uint32_t kAuthoredCubePSSlot = 8;

	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"VANILLA_PBR",
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kDfTiledLighting, engine::ShaderInjectionTarget::kBsdfComposite },
		"VANILLA_PBR_FULLSCREEN_DEBUG"
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "VanillaPBR", kShaderDefines };
}
