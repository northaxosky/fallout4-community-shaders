#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::ssgi
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"SSGI", { engine::ShaderInjectionTarget::kBsdfComposite, engine::ShaderInjectionTarget::kDeferredPrepass }
	};
}
