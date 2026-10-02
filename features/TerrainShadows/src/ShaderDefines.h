#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::terrain_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"TERRAIN_SHADOWS",
		{ engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kDistantTree,
			engine::ShaderInjectionTarget::kBsWater, engine::ShaderInjectionTarget::kEffect, engine::ShaderInjectionTarget::kBsdfComposite },
		"TERRAIN_SHADOWS_FULLSCREEN_DEBUG"
	};
}
