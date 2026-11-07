#pragma once
#include "Render/ShaderDefineProvider.h"
#include "Render/ShaderInjectionDefines.h"

namespace cs::features::upscaling
{
	// Owned pixel shaders that read SharedData (mip bias, temporal dither).
	inline const engine::ShaderDefineDeclaration kMipBiasShaderDefines{
		engine::shader_injection_defines::kSubstrate,
		{ engine::ShaderInjectionTarget::kDeferredPrepass, engine::ShaderInjectionTarget::kDistantTree,
			engine::ShaderInjectionTarget::kBsWater, engine::ShaderInjectionTarget::kBsSky }
	};

	// Imagespace fullscreen VS draws read the exact render scale from b4.
	inline const engine::ShaderDefineDeclaration kImageSpaceUvShaderDefines{
		engine::shader_injection_defines::kSubstrate,
		{ engine::ShaderInjectionTarget::kImageSpace },
		{},
		engine::ShaderInjectionTarget::kCount,
		engine::ShaderStageBit(engine::ShaderStage::kVertex)
	};

	inline const engine::RegisterShaderContribution kRegisterMipBias{ "UpscalingMipBias", kMipBiasShaderDefines };
	inline const engine::RegisterShaderContribution kRegisterImageSpaceUv{ "UpscalingImageSpaceUv", kImageSpaceUvShaderDefines };
}
