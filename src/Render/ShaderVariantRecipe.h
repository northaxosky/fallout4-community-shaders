#pragma once

#include "Render/ShaderVariantCompilation.h"
#include "Utils/ShaderCache/ShaderRecipe.h"

namespace cs::engine
{
	shader_cache::ShaderRecipe BuildShaderVariantRecipe(
		const ShaderVariantCompilationRequest& a_request);
}
