#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::hdr_display_shader
{
	// Imagespace branches on HDR at run time; UI effects clamp for fp16 RT0.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"HDR_OUTPUT",
		{ engine::ShaderInjectionTarget::kImageSpace, engine::ShaderInjectionTarget::kEffect }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "HDRDisplay", kShaderDefines };
}
