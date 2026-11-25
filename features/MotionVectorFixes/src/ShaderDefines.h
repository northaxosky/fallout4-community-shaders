#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::motion_vector_fixes_shader
{
	// The prepass vertex shader writes the previous grass wind gust.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"MOTION_VECTOR_FIXES",
		{ engine::ShaderInjectionTarget::kDeferredPrepass },
		{},
		engine::ShaderInjectionTarget::kCount,
		engine::ShaderStageBit(engine::ShaderStage::kVertex)
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "MotionVectorFixes", kShaderDefines };
}
