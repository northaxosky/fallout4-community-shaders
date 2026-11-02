#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::terrain_blending_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"TERRAIN_BLENDING", { engine::ShaderInjectionTarget::kDeferredPrepass }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "TerrainBlending", kShaderDefines };
}
