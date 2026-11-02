#pragma once

#include "Render/ShaderDefineProvider.h"

namespace cs::features::unified_water
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"UNIFIED_WATER", { engine::ShaderInjectionTarget::kBsWater }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "UnifiedWater", kShaderDefines };
}
