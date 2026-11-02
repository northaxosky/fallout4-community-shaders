#pragma once

#include "Render/ShaderStage.h"
#include "Utils/ShaderCache/RevalidationContext.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <cctype>
#include <span>
#include <stdexcept>
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

	using ShaderDefines = std::vector<std::pair<const char*, const char*>>;

	namespace detail
	{
		void SetShaderName(ID3D11DeviceChild* a_shader, std::string_view a_name) noexcept;
		[[noreturn]] void ThrowShaderCompileFailure(std::string_view a_name, const wchar_t* a_filePath);

		template <class T>
		struct ShaderProfile;
		template <>
		struct ShaderProfile<ID3D11ComputeShader>
		{
			static constexpr const char* value = "cs_5_0";
		};
		template <>
		struct ShaderProfile<ID3D11PixelShader>
		{
			static constexpr const char* value = "ps_5_0";
		};
		template <>
		struct ShaderProfile<ID3D11VertexShader>
		{
			static constexpr const char* value = "vs_5_0";
		};
	}

	// Null on failure, which CompileShader already logged.
	template <class T>
	[[nodiscard]] winrt::com_ptr<T> CompileShaderAs(const wchar_t* a_filePath, const ShaderDefines& a_defines = {}, const char* a_program = "main")
	{
		winrt::com_ptr<T> shader;
		shader.attach(static_cast<T*>(CompileShader(a_filePath, a_defines, detail::ShaderProfile<T>::value, a_program)));
		return shader;
	}

	template <class T>
	[[nodiscard]] winrt::com_ptr<T> CompileShaderAs(std::string_view a_name, const wchar_t* a_filePath, const ShaderDefines& a_defines = {})
	{
		auto shader = CompileShaderAs<T>(a_filePath, a_defines);
		if (shader)
			detail::SetShaderName(shader.get(), a_name);
		return shader;
	}

	// For shaders the caller cannot run without: throws std::runtime_error.
	template <class T>
	[[nodiscard]] winrt::com_ptr<T> RequireShaderAs(std::string_view a_name, const wchar_t* a_filePath, const ShaderDefines& a_defines = {})
	{
		auto shader = CompileShaderAs<T>(a_name, a_filePath, a_defines);
		if (!shader)
			detail::ThrowShaderCompileFailure(a_name, a_filePath);
		return shader;
	}

	// Unnamed and named per-stage forwarders.
	[[nodiscard]] inline auto CompileComputeShader(const wchar_t* a_path, const ShaderDefines& a_defines = {}) { return CompileShaderAs<ID3D11ComputeShader>(a_path, a_defines); }
	[[nodiscard]] inline auto CompileComputeShader(std::string_view a_name, const wchar_t* a_path, const ShaderDefines& a_defines = {}) { return CompileShaderAs<ID3D11ComputeShader>(a_name, a_path, a_defines); }
	[[nodiscard]] inline auto CompilePixelShader(const wchar_t* a_path, const ShaderDefines& a_defines = {}) { return CompileShaderAs<ID3D11PixelShader>(a_path, a_defines); }
	[[nodiscard]] inline auto CompilePixelShader(std::string_view a_name, const wchar_t* a_path, const ShaderDefines& a_defines = {}) { return CompileShaderAs<ID3D11PixelShader>(a_name, a_path, a_defines); }
	[[nodiscard]] inline auto CompileVertexShader(const wchar_t* a_path, const ShaderDefines& a_defines = {}) { return CompileShaderAs<ID3D11VertexShader>(a_path, a_defines); }
	[[nodiscard]] inline auto CompileVertexShader(std::string_view a_name, const wchar_t* a_path, const ShaderDefines& a_defines = {}) { return CompileShaderAs<ID3D11VertexShader>(a_name, a_path, a_defines); }
	[[nodiscard]] inline auto RequireComputeShader(std::string_view a_name, const wchar_t* a_path, const ShaderDefines& a_defines = {}) { return RequireShaderAs<ID3D11ComputeShader>(a_name, a_path, a_defines); }

	// Returns null before the engine device is ready.
	[[nodiscard]] ID3D11Device* GetD3DDevice();

	[[nodiscard]] std::string AdapterDescription(ID3D11Device* a_device);
}
