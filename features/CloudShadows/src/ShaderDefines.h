#pragma once
#include "Render/ShaderDefineProvider.h"

#include <cstdint>

namespace cs::features::cloud_shadows_shader
{
	// Composite cloud occlusion cube; upstream's CloudShadowsTexture.
	inline constexpr std::uint32_t kCloudShadowsSlot = 25;
	// Depth the cloud pixel shader rejects against; shared with canonical depth.
	inline constexpr std::uint32_t kCloudDepthSlot = 17;

	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"CLOUD_SHADOWS",
		{ engine::ShaderInjectionTarget::kBsSky, engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kBsWater }
	};
}
