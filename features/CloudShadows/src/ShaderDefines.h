#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::cloud_shadows_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"CLOUD_SHADOWS",
		{ engine::ShaderInjectionTarget::kBsSky, engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kBsWater }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "CloudShadows", kShaderDefines };
}
