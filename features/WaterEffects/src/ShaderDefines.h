#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::water_effects
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"WATER_EFFECTS", { engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kBsdfComposite, engine::ShaderInjectionTarget::kBsWater },
		"WATER_EFFECTS_FULLSCREEN_DEBUG", engine::ShaderInjectionTarget::kBsWater
	};
}
