#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::skylighting_shader
{
	// Raster and tiled lighting are chosen per frame, so both carry the factor.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"SKYLIGHTING",
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kBsdfLight,
			engine::ShaderInjectionTarget::kDfTiledLighting, engine::ShaderInjectionTarget::kBsdfComposite,
			engine::ShaderInjectionTarget::kBsWater, engine::ShaderInjectionTarget::kEffect },
		"SKYLIGHTING_FULLSCREEN_DEBUG"
	};
}
