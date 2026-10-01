#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::dc
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"DYNAMIC_CUBEMAPS",
		{ engine::ShaderInjectionTarget::kBsWater, engine::ShaderInjectionTarget::kBsdfComposite,
			engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kDfTiledLighting, engine::ShaderInjectionTarget::kImageSpace }
	};
}
