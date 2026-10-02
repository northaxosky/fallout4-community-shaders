#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::upscaling
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"UPSCALING", { engine::ShaderInjectionTarget::kImageSpace }
	};
}
