#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::upscaling
{
	// Owned targets drawn inside a SamplerBias override window.
	inline const engine::ShaderDefineDeclaration kSamplerBiasShaderDefines{
		"SAMPLER_MIP_BIAS",
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kDistantTree,
			engine::ShaderInjectionTarget::kEffect }
	};
}
