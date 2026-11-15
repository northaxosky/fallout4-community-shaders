#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::true_pbr
{
	// No debug define: views are a uniform branch, so no recompile.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"FO4_TRUE_PBR",
		{ engine::ShaderInjectionTarget::kDeferredPrepass }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "TruePBR", kShaderDefines };
}
