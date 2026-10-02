#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::wetness
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"WETNESS_EFFECTS",
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kBsdfLight,
			engine::ShaderInjectionTarget::kDfTiledLighting, engine::ShaderInjectionTarget::kBsdfComposite },
		"WETNESS_EFFECTS_FULLSCREEN_DEBUG", engine::ShaderInjectionTarget::kDeferredPrepass
	};
}
