#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::volumetric_shadows_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"VOLUMETRIC_SHADOWS",
		{ engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kBsWater, engine::ShaderInjectionTarget::kEffect }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "VolumetricShadows", kShaderDefines };
}
