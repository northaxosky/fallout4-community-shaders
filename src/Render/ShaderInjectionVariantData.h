#pragma once

#include "Render/ShaderInjection.h"

#include <vector>

namespace cs::engine
{
	void AppendBsdfFamilyShaderReplacementVariants(
		std::vector<ShaderReplacementVariantRegistration>& a_variants);
	void AppendStaticFamilyShaderReplacementVariants(
		std::vector<ShaderReplacementVariantRegistration>& a_variants);
}
