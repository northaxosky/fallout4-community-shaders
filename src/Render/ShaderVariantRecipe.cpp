#include "Render/ShaderVariantRecipe.h"

#include "Utils/ShaderDefines.h"

#include <utility>

namespace cs::engine
{
	shader_cache::ShaderRecipe BuildShaderVariantRecipe(
		const ShaderVariantCompilationRequest& a_request,
		const std::filesystem::path& a_shaderRoot)
	{
		shader_cache::ShaderRecipe recipe;
		recipe.source = a_request.sourcePath;
		recipe.includeRoots.push_back(a_shaderRoot);
		recipe.defines = a_request.defines;
		util::AppendStandardShaderDefines(recipe.defines, a_request.profile);
		recipe.entryPoint = a_request.entryPoint;
		recipe.profile = a_request.profile;
		recipe.stage = a_request.stage;
		return recipe;
	}
}
