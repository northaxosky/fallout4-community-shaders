#pragma once

#include "Utils/ShaderCache/DependencyTrace.h"
#include "Utils/ShaderCache/ShaderRecipe.h"
#include "Utils/ShaderSamplerBindings.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cs::shader_cache
{
	struct SourceCompileOutcome
	{
		bool succeeded = false;
		std::vector<std::uint8_t> bytecode;
		std::optional<util::ShaderSamplerUse> samplers;
		DependencyManifest manifest;
		std::string error;
	};

	SourceCompileOutcome CompileSourceWithManifest(const ShaderRecipe& a_recipe);
}
