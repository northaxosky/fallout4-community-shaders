#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::vanilla_pbr
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"VANILLA_PBR",
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kBsdfLight, engine::ShaderInjectionTarget::kDfTiledLighting, engine::ShaderInjectionTarget::kBsdfComposite },
		"VANILLA_PBR_FULLSCREEN_DEBUG"
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "VanillaPBR", kShaderDefines };
}
