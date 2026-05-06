#pragma once

#include <d3d11.h>

namespace cs::features::sss::Util
{
	// Mirrors Upscaling's enumeration so we read the same depth-stencil slot the engine populates pre-deferred.
	enum class DepthStencilTarget
	{
		kMain = 2,
	};

	[[nodiscard]] inline RE::BSGraphics::State* State_GetSingleton()
	{
		static REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID({ 600795, 2704621, 2704621 }) };
		return singleton.get();
	}

	ID3D11DeviceChild* CompileShader(const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program = "main");
}
