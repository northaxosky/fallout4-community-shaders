#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include "Utils/ShaderCompile.h"
#include "Utils/ShaderBufferBindings.h"

#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>

namespace cs::util
{
	namespace
	{
		class ShaderIncludeHandler final : public ID3DInclude
		{
		public:
			explicit ShaderIncludeHandler(std::filesystem::path a_baseDirectory) :
				baseDirectory_(std::move(a_baseDirectory))
			{}

			HRESULT STDMETHODCALLTYPE Open(
				[[maybe_unused]] D3D_INCLUDE_TYPE a_includeType,
				LPCSTR a_fileName,
				[[maybe_unused]] LPCVOID a_parentData,
				LPCVOID* a_data,
				UINT* a_bytes) override
			{
				if (!a_fileName || !a_data || !a_bytes)
					return E_INVALIDARG;

				*a_data = nullptr;
				*a_bytes = 0;

				try {
					return OpenFrom(baseDirectory_, a_fileName, a_data, a_bytes) == OpenResult::kSuccess ? S_OK : E_FAIL;
				} catch (...) {
					return E_FAIL;
				}
			}

			HRESULT STDMETHODCALLTYPE Close(LPCVOID a_data) override
			{
				const auto file = openedFiles_.find(a_data);
				if (file == openedFiles_.end())
					return E_FAIL;

				openedFiles_.erase(file);
				return S_OK;
			}

		private:
			enum class OpenResult
			{
				kSuccess,
				kOpenFailed,
				kReadFailed
			};

			struct OpenedFile
			{
				std::unique_ptr<char[]> buffer;
			};

			OpenResult OpenFrom(
				const std::filesystem::path& a_directory,
				LPCSTR a_fileName,
				LPCVOID* a_data,
				UINT* a_bytes)
			{
				const auto resolvedPath = ResolveShaderInclude(a_directory, a_fileName);

				std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
				if (!file.is_open())
					return OpenResult::kOpenFailed;

				const auto endPosition = file.tellg();
				if (endPosition == std::ifstream::pos_type(-1))
					return OpenResult::kReadFailed;

				const auto fileSize = static_cast<std::streamoff>(endPosition);
				if (fileSize < 0 || fileSize > static_cast<std::streamoff>(std::numeric_limits<UINT>::max()))
					return OpenResult::kReadFailed;

				const auto size = static_cast<std::size_t>(fileSize);
				auto buffer = std::make_unique<char[]>(std::max<std::size_t>(size, 1));

				file.seekg(0, std::ios::beg);
				if (!file)
					return OpenResult::kReadFailed;
				if (size != 0) {
					file.read(buffer.get(), static_cast<std::streamsize>(size));
					if (!file)
						return OpenResult::kReadFailed;
				}

				auto* data = buffer.get();
				const auto [fileIt, inserted] =
					openedFiles_.emplace(data, OpenedFile{ std::move(buffer) });
				if (!inserted)
					return OpenResult::kReadFailed;

				*a_data = fileIt->first;
				*a_bytes = static_cast<UINT>(size);
				return OpenResult::kSuccess;
			}

			std::filesystem::path baseDirectory_;
			std::unordered_map<LPCVOID, OpenedFile> openedFiles_;
		};
	}

	std::vector<std::pair<const char*, const char*>> UtilityShaderDefines(
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType)
	{
		auto defines = a_defines;
		const std::pair<const char*, const char*> stages[] = {
			{ "ps_5_0", "PSHADER" },
			{ "vs_5_0", "VSHADER" },
			{ "hs_5_0", "HULLSHADER" },
			{ "ds_5_0", "DOMAINSHADER" },
			{ "cs_4_0", "COMPUTESHADER" },
			{ "cs_5_0", "COMPUTESHADER" },
			{ "cs_5_1", "COMPUTESHADER" }
		};
		for (const auto& [profile, name] : stages) {
			if (_stricmp(a_programType, profile) == 0) {
				defines.emplace_back(name, "");
				break;
			}
		}
		defines.emplace_back("WINPC", "");
		defines.emplace_back("DX11", "");
		defines.emplace_back("FRAMEBUFFER_REGISTER", "b4");
		return defines;
	}

	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderToBlob(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		std::string* a_outError,
		const std::filesystem::path& a_shaderRoot)
	{
		const std::uint32_t flags =
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		return CompileShaderToBlob(
			a_filePath,
			a_defines,
			a_programType,
			a_program,
			flags,
			a_outError,
			a_shaderRoot);
	}

	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderToBlob(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program,
		std::uint32_t a_flags,
		std::string* a_outError,
		const std::filesystem::path& a_shaderRoot)
	{
		if (a_outError)
			a_outError->clear();

		std::vector<D3D_SHADER_MACRO> macros;
		macros.reserve(a_defines.size() + 4);
		for (const auto& define : UtilityShaderDefines(a_defines, a_programType))
			macros.push_back({ define.first, define.second });
		macros.push_back({ nullptr, nullptr });

		if (!std::filesystem::exists(a_filePath)) {
			if (a_outError)
				*a_outError = "Shader source missing";
			return {};
		}

		Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> shaderErrors;
		ShaderIncludeHandler includeHandler{ a_shaderRoot };
		const HRESULT result = D3DCompileFromFile(
			a_filePath,
			macros.data(),
			&includeHandler,
			a_program,
			a_programType,
			a_flags,
			0,
			shaderBlob.GetAddressOf(),
			shaderErrors.GetAddressOf());

		if (a_outError && shaderErrors) {
			const auto* data = static_cast<const char*>(shaderErrors->GetBufferPointer());
			const auto size = shaderErrors->GetBufferSize();
			if (data && size != 0) {
				a_outError->assign(data, size);
				if (!a_outError->empty() && a_outError->back() == '\0')
					a_outError->pop_back();
			}
		}

		if (FAILED(result)) {
			if (a_outError && a_outError->empty())
				*a_outError = "Unknown error";
			return {};
		}

		if (const auto error = ValidateSubstrateSlots(shaderBlob.Get()); !error.empty()) {
			if (a_outError)
				*a_outError = error;
			return {};
		}
		return shaderBlob;
	}
}
