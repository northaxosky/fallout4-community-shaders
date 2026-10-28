#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::subsurface_scattering_shader
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"SUBSURFACE_SCATTERING", { engine::ShaderInjectionTarget::kBsdfComposite }
	};
}
