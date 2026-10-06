#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::upscaling
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"UPSCALING", { engine::ShaderInjectionTarget::kImageSpace }
	};

	// Owned targets drawn inside a SamplerBias override window.
	inline const engine::ShaderDefineDeclaration kSamplerBiasShaderDefines{
		"SAMPLER_MIP_BIAS",
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kDistantTree,
			engine::ShaderInjectionTarget::kEffect }
	};
}
