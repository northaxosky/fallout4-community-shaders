#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::lod_blending_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"LOD_BLENDING", { engine::ShaderInjectionTarget::kDeferredPrepass }
	};
}
