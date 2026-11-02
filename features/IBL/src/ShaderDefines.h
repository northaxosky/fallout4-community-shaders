#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::ibl_shader
{
	// Raster and tiled lighting take the ambient; the rest take the fog tint.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"IBL",
		{ engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kDfTiledLighting,
			engine::ShaderInjectionTarget::kBsdfComposite, engine::ShaderInjectionTarget::kBsWater,
			engine::ShaderInjectionTarget::kEffect, engine::ShaderInjectionTarget::kDistantTree }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "ImageBasedLighting", kShaderDefines };
}
