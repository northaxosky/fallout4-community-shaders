#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::skylighting_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"SKYLIGHTING", { engine::ShaderInjectionTarget::kBsdfComposite },
		"SKYLIGHTING_FULLSCREEN_DEBUG"
	};
}
