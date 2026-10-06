#pragma once
#include "Render/ShaderDefineProvider.h"

namespace cs::features::upscaling
{
	inline const engine::ShaderDefineDeclaration kShaderDefines{
		"UPSCALING", { engine::ShaderInjectionTarget::kImageSpace }
	};

	// Targets drawn inside a SamplerBias override window whose shaders read SharedData::MipBias.
	inline const engine::ShaderDefineDeclaration kSamplerBiasShaderDefines{
		"SAMPLER_MIP_BIAS", { engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kDistantTree }
	};
}
