#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::extended_materials_shader
{
	// The prepass resolves sun parallax shadows; DFLight applies them.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"EXTENDED_MATERIALS", { engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kBsdfLight }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "ExtendedMaterials", kShaderDefines };
}
