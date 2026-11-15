#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::true_pbr
{
	// No debug define: the debug views are a uniform branch, so toggling them recompiles nothing.
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"FO4_TRUE_PBR",
		{ engine::ShaderInjectionTarget::kDeferredPrepass }
	};

	inline const engine::RegisterShaderContribution kRegisterShaderContribution{ "TruePBR", kShaderDefines };
}
