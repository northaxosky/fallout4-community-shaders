#pragma once
#include "Render/ShaderDefineProvider.h"

#include <cstdint>

namespace cs::features::volumetric_shadows_shader
{
	inline constexpr std::uint32_t kSharedShadowMapSlot = 18;
	inline constexpr std::uint32_t kShadowLightsSlot = 98;

	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"VOLUMETRIC_SHADOWS",
		{ engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kBsWater, engine::ShaderInjectionTarget::kEffect }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "VolumetricShadows", kShaderDefines };
}
