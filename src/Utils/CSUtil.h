#pragma once

#include "Render/ShaderStage.h"
#include "Utils/ShaderCache/RevalidationContext.h"

#include <d3d11.h>

#include <cctype>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cs::util
{
	// ASCII case-insensitive equality, as the engine compares editor IDs and names.
	[[nodiscard]] inline bool EqualsIgnoreCase(std::string_view a_lhs, std::string_view a_rhs) noexcept
	{
		if (a_lhs.size() != a_rhs.size())
			return false;
		for (std::size_t i = 0; i < a_lhs.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(a_lhs[i])) != std::tolower(static_cast<unsigned char>(a_rhs[i])))
				return false;
		}
		return true;
	}

	HRESULT CreateShaderChild(
		ID3D11Device& a_device,
		engine::ShaderStage a_stage,
		std::span<const std::uint8_t> a_bytecode,
		ID3D11DeviceChild*& a_shader) noexcept;

	class ShaderCompilationBatch
	{
	public:
		ShaderCompilationBatch() noexcept;
		~ShaderCompilationBatch() noexcept;

		ShaderCompilationBatch(const ShaderCompilationBatch&) = delete;
		ShaderCompilationBatch& operator=(const ShaderCompilationBatch&) = delete;

	private:
		shader_cache::RevalidationContext _revalidation;
		shader_cache::RevalidationContext* _previous = nullptr;
	};

	// Returns null on failure; failed recipes are not retried.
	ID3D11DeviceChild* CompileShader(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program = "main");

	// Returns null before the engine device is ready.
	[[nodiscard]] ID3D11Device* GetD3DDevice();

	[[nodiscard]] std::string AdapterDescription(ID3D11Device* a_device);
}
