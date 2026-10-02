#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::sss
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"SCREEN_SPACE_SHADOWS", { engine::ShaderInjectionTarget::kBsdfLight }
	};
}
