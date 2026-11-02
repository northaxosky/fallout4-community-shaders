#pragma once
#include "Render/ShaderDefineProvider.h"
#include "Render/ShaderInjectionDefines.h"

namespace cs::features::upscaling
{
	// Owned pixel shaders that sample materials with SharedData::MipBias.
	inline const engine::ShaderDefineDeclaration kMipBiasShaderDefines{
		engine::shader_injection_defines::kSubstrate,
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kDistantTree,
			engine::ShaderInjectionTarget::kBsWater }
	};

	// Imagespace fullscreen VS draws read the exact render scale from b4.
	inline const engine::ShaderDefineDeclaration kImageSpaceUvShaderDefines{
		engine::shader_injection_defines::kSubstrate,
		{ engine::ShaderInjectionTarget::kImageSpace },
		{},
		engine::ShaderInjectionTarget::kCount,
		engine::ShaderStageBit(engine::ShaderStage::kVertex)
	};
}
