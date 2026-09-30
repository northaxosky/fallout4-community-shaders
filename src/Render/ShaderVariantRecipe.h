#pragma once

#include "Render/ShaderVariantCompilation.h"
#include "Utils/ShaderCache/ShaderRecipe.h"
#include "Utils/ShaderInclude.h"

namespace cs::engine
{
	shader_cache::ShaderRecipe BuildShaderVariantRecipe(
		const ShaderVariantCompilationRequest& a_request,
		const std::filesystem::path& a_shaderRoot = util::kDefaultShaderRoot);
}
