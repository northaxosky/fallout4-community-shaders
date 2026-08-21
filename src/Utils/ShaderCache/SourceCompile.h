#pragma once

#include "Utils/ShaderCache/DependencyTrace.h"
#include "Utils/ShaderCache/ShaderRecipe.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs::shader_cache
{
	struct SourceCompileOutcome
	{
		bool                      succeeded = false;
		std::vector<std::uint8_t> bytecode;
		DependencyManifest        manifest;
		std::string               error;
	};

	SourceCompileOutcome CompileSourceWithManifest(const ShaderRecipe& a_recipe);
}
