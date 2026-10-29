#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::subsurface_scattering_shader
{
	// The prepass writes the per-draw skin profile; the composite consumes it.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"SUBSURFACE_SCATTERING", { engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kBsdfComposite }
	};
}
