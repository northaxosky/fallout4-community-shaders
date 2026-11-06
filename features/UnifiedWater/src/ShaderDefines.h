#pragma once

#include "Render/ShaderDefineProvider.h"

namespace cs::features::unified_water
{
	// Upstream defines UNIFIED_WATER for all shader types; SSLR reads it.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"UNIFIED_WATER", { engine::ShaderInjectionTarget::kBsWater, engine::ShaderInjectionTarget::kImageSpace }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "UnifiedWater", kShaderDefines };
}
