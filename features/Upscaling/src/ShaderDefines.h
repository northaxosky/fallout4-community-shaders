#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::upscaling
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"UPSCALING", { engine::ShaderInjectionTarget::kImageSpace }
	};

	// Every owned target drawn inside a SamplerBias override window; evidence in the Upscaling record.
	inline const engine::ShaderDefineDeclaration kSamplerBiasShaderDefines{
		"SAMPLER_MIP_BIAS",
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kDistantTree,
			engine::ShaderInjectionTarget::kEffect }
	};
}
