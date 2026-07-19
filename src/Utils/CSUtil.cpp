#include "Utils/CSUtil.h"

#include <algorithm>
#include <cstdio>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <winrt/base.h>

#include "Log.h"

namespace cs::util
{
	namespace
	{
		auto* L = cs::log::Get("cs.util");

		class ShaderIncludeHandler final : public ID3DInclude
		{
		public:
			explicit ShaderIncludeHandler(std::filesystem::path a_baseDirectory) :
				baseDirectory_(std::move(a_baseDirectory))
			{}

			HRESULT STDMETHODCALLTYPE Open(
				[[maybe_unused]] D3D_INCLUDE_TYPE a_includeType,
				LPCSTR a_fileName,
				LPCVOID a_parentData,
				LPCVOID* a_data,
				UINT* a_bytes) override
			{
				if (!a_fileName || !a_data || !a_bytes)
					return E_INVALIDARG;

				*a_data  = nullptr;
				*a_bytes = 0;

				auto includingDirectory = baseDirectory_;
				if (a_parentData) {
					const auto parent = openedFiles_.find(a_parentData);
					if (parent != openedFiles_.end())
						includingDirectory = parent->second.directory;
				}

				const auto result = OpenFrom(includingDirectory, a_fileName, a_data, a_bytes);
				if (result == OpenResult::kSuccess)
					return S_OK;
				if (result != OpenResult::kOpenFailed)
					return E_FAIL;

				return OpenFrom(baseDirectory_, a_fileName, a_data, a_bytes) == OpenResult::kSuccess ? S_OK : E_FAIL;
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
				std::filesystem::path   directory;
			};

			OpenResult OpenFrom(
				const std::filesystem::path& a_directory,
				LPCSTR a_fileName,
				LPCVOID* a_data,
				UINT* a_bytes)
			{
				std::error_code error;
				const auto resolvedPath = std::filesystem::weakly_canonical(a_directory / a_fileName, error);
				if (error)
					return OpenResult::kOpenFailed;

				std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
				if (!file.is_open())
					return OpenResult::kOpenFailed;

				const auto endPosition = file.tellg();
				if (endPosition == std::ifstream::pos_type(-1))
					return OpenResult::kReadFailed;

				const auto fileSize = static_cast<std::streamoff>(endPosition);
				if (fileSize < 0 || fileSize > static_cast<std::streamoff>(std::numeric_limits<UINT>::max()))
					return OpenResult::kReadFailed;

				const auto size   = static_cast<std::size_t>(fileSize);
				auto       buffer = std::make_unique<char[]>(std::max<std::size_t>(size, 1));

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
					openedFiles_.emplace(data, OpenedFile{ std::move(buffer), resolvedPath.parent_path() });
				if (!inserted)
					return OpenResult::kReadFailed;

				*a_data  = fileIt->first;
				*a_bytes = static_cast<UINT>(size);
				return OpenResult::kSuccess;
			}

			std::filesystem::path                        baseDirectory_;
			std::unordered_map<LPCVOID, OpenedFile> openedFiles_;
		};
	}

	ID3D11Device* GetD3DDevice()
	{
		auto* data = RE::BSGraphics::GetRendererData();
		if (!data || !data->device)
			return nullptr;
		return reinterpret_cast<ID3D11Device*>(data->device);
	}

	bool ReadMarker(const char* a_path, char& a_out)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, a_path, "rb") != 0 || !f)
			return false;
		// Skip UTF-8 BOM if present (PowerShell `Out-File -Encoding utf8` and Notepad both write it).
		unsigned char buf[4] = {};
		size_t        read   = fread(buf, 1, 4, f);
		size_t        cursor = 0;
		if (read >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
			cursor = 3;
		bool ok = false;
		if (read > cursor) {
			a_out = static_cast<char>(buf[cursor]);
			ok    = true;
		}
		fclose(f);
		// Delete successful smoke markers so stale files cannot override later runs.
		if (ok)
			std::remove(a_path);
		return ok;
	}

	ID3D11DeviceChild* CompileShader(
		const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program)
	{
		auto* device = GetD3DDevice();
		if (!device) {
			L->error("Cannot compile shader: D3D device not ready");
			return nullptr;
		}

		std::vector<D3D_SHADER_MACRO> macros;
		macros.reserve(a_defines.size() + 1);
		for (auto& d : a_defines)
			macros.push_back({ d.first, d.second });
		macros.push_back({ nullptr, nullptr });

		const uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

		winrt::com_ptr<ID3DBlob> shaderBlob;
		winrt::com_ptr<ID3DBlob> shaderErrors;

		std::string narrow;
		std::wstring wide{ a_filePath };
		std::transform(wide.begin(), wide.end(), std::back_inserter(narrow), [](wchar_t c) { return (char)c; });

		if (!std::filesystem::exists(a_filePath)) {
			L->error("Shader source missing: {}", narrow);
			return nullptr;
		}

		ShaderIncludeHandler includeHandler{ std::filesystem::path{ a_filePath }.parent_path() };
		if (FAILED(D3DCompileFromFile(a_filePath, macros.data(), &includeHandler,
				 a_program, a_programType, flags, 0, shaderBlob.put(), shaderErrors.put()))) {
			L->warn("Shader compilation failed ({}):\n{}", narrow,
				shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
			return nullptr;
		}
		if (shaderErrors)
			L->debug("Shader logs ({}):\n{}", narrow, static_cast<char*>(shaderErrors->GetBufferPointer()));

		if (!_stricmp(a_programType, "ps_5_0")) {
			ID3D11PixelShader* s = nullptr;
			DX::ThrowIfFailed(device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &s));
			return s;
		}
		if (!_stricmp(a_programType, "vs_5_0")) {
			ID3D11VertexShader* s = nullptr;
			DX::ThrowIfFailed(device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &s));
			return s;
		}
		if (!_stricmp(a_programType, "hs_5_0")) {
			ID3D11HullShader* s = nullptr;
			DX::ThrowIfFailed(device->CreateHullShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &s));
			return s;
		}
		if (!_stricmp(a_programType, "ds_5_0")) {
			ID3D11DomainShader* s = nullptr;
			DX::ThrowIfFailed(device->CreateDomainShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &s));
			return s;
		}
		if (!_stricmp(a_programType, "cs_5_0") || !_stricmp(a_programType, "cs_4_0")) {
			ID3D11ComputeShader* s = nullptr;
			DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &s));
			return s;
		}

		L->warn("CompileShader: unsupported program type '{}'", a_programType);
		return nullptr;
	}
}
