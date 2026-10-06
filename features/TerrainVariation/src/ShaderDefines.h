#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::terrain_variation_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"TERRAIN_VARIATION", { engine::ShaderInjectionTarget::kDeferredPrepass }
	};
}
