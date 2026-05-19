#include "CSUtil.h"

#include <algorithm>
#include <cstdio>
#include <d3dcompiler.h>
#include <filesystem>
#include <winrt/base.h>

#include "Log.h"

namespace cs::util
{
	namespace { auto* L = cs::log::Get("cs.util"); }

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
		if (fopen_s(&f, a_path, "r") != 0 || !f)
			return false;
		a_out = static_cast<char>(fgetc(f));
		fclose(f);
		return true;
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

		if (FAILED(D3DCompileFromFile(a_filePath, macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE,
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
