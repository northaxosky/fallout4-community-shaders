#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::skylighting_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"SKYLIGHTING", { engine::ShaderInjectionTarget::kBsdfComposite, engine::ShaderInjectionTarget::kBsWater },
		"SKYLIGHTING_FULLSCREEN_DEBUG"
	};
}
