#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::hdr_display_shader
{
	// The tonemap, gamma and temporal AA passes branch on HDR at run time.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"HDR_OUTPUT",
		{ engine::ShaderInjectionTarget::kImageSpace }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "HDRDisplay", kShaderDefines };
}
