#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::light_limit_fix
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"LIGHT_LIMIT_FIX", { engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kDfTiledLighting, engine::ShaderInjectionTarget::kEffect, engine::ShaderInjectionTarget::kBsWater },
		"LLFDEBUG", engine::ShaderInjectionTarget::kCount, 0, { engine::ShaderInjectionTarget::kEffect }
	};
}
