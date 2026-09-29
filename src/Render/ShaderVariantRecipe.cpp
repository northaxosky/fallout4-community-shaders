#include "Render/ShaderVariantRecipe.h"

#include <utility>

namespace cs::engine
{
	namespace
	{
		shader_cache::ShaderCacheStage ToCacheStage(
			ShaderStage a_stage) noexcept
		{
			static_assert(static_cast<std::uint8_t>(ShaderStage::kCount) == 3);
			switch (a_stage) {
			case ShaderStage::kVertex:
				return shader_cache::ShaderCacheStage::kVertex;
			case ShaderStage::kPixel:
				return shader_cache::ShaderCacheStage::kPixel;
			case ShaderStage::kCompute:
				return shader_cache::ShaderCacheStage::kCompute;
			}
			std::unreachable();
		}
	}

	shader_cache::ShaderRecipe BuildShaderVariantRecipe(
		const ShaderVariantCompilationRequest& a_request)
	{
		shader_cache::ShaderRecipe recipe;
		recipe.source = a_request.sourcePath;
		recipe.includeRoots.push_back(a_request.sourcePath.parent_path());
		recipe.defines = a_request.defines;
		recipe.entryPoint = a_request.entryPoint;
		recipe.profile = a_request.profile;
		recipe.stage = ToCacheStage(a_request.stage);
		return recipe;
	}
}
