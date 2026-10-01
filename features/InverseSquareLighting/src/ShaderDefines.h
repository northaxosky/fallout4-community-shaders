#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::isl
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"INVERSE_SQUARE_LIGHTING", { engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kDfTiledLighting }
	};
}
