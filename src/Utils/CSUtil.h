#pragma once

#include "Utils/ShaderCache/RevalidationContext.h"

#include <d3d11.h>

#include <string>
#include <utility>
#include <vector>

namespace cs::util
{
	class ShaderCompilationBatch
	{
	public:
		ShaderCompilationBatch() noexcept;
		~ShaderCompilationBatch() noexcept;

		ShaderCompilationBatch(const ShaderCompilationBatch&) = delete;
		ShaderCompilationBatch& operator=(const ShaderCompilationBatch&) = delete;

	private:
		shader_cache::RevalidationContext  _revalidation;
		shader_cache::RevalidationContext* _previous = nullptr;
	};

	// Compiles HLSL and returns null on failure.
	ID3D11DeviceChild* CompileShader(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program = "main");

	// Returns null before the engine device is ready.
	[[nodiscard]] ID3D11Device* GetD3DDevice();
}
