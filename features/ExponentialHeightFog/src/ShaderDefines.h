#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::fog_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"EXPONENTIAL_HEIGHT_FOG",
		{ engine::ShaderInjectionTarget::kBsdfComposite, engine::ShaderInjectionTarget::kBsWater,
			engine::ShaderInjectionTarget::kEffect, engine::ShaderInjectionTarget::kDistantTree, engine::ShaderInjectionTarget::kBsdfLight },
		"EXPONENTIAL_HEIGHT_FOG_FULLSCREEN_DEBUG"
	};
}
