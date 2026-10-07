#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::skylighting_shader
{
	// Raster and tiled lighting are chosen per frame, so both must carry the ambient factor.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"SKYLIGHTING",
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kBsdfLight,
			engine::ShaderInjectionTarget::kDfTiledLighting, engine::ShaderInjectionTarget::kBsdfComposite,
			engine::ShaderInjectionTarget::kBsWater },
		"SKYLIGHTING_FULLSCREEN_DEBUG"
	};
}
