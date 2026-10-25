#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::rain_refraction_shader
{
	// The vertex stage adds the drop UV interpolator, so the stages replace together.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"RAIN_REFRACTION", { engine::ShaderInjectionTarget::kEffect }, {}, engine::ShaderInjectionTarget::kEffect
	};
}
