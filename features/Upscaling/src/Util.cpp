#include "Util.h"

#include <d3dcompiler.h>
#include <winrt/base.h>

#include "Log.h"

namespace cs::features::upscaling
{
	namespace { auto* L = cs::log::Get("cs.feature.upscaling.util"); }

namespace Util
{
	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program)
	{
		static auto rendererData = RE::BSGraphics::GetRendererData();
		static auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

		// Build defines (aka convert vector->D3DCONSTANT array)
		std::vector<D3D_SHADER_MACRO> macros;

		for (auto& i : Defines)
			macros.push_back({ i.first, i.second });

		// Add null terminating entry
		macros.push_back({ nullptr, nullptr });

		// Compiler setup
		uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

		winrt::com_ptr<ID3DBlob> shaderBlob;
		winrt::com_ptr<ID3DBlob> shaderErrors;

		std::string str;
		std::wstring path{ FilePath };
		std::transform(path.begin(), path.end(), std::back_inserter(str), [](wchar_t c) {
			return (char)c;
		});
		if (!std::filesystem::exists(FilePath)) {
			L->error("Failed to compile shader; {} does not exist", str);
			return nullptr;
		}
		if (FAILED(D3DCompileFromFile(FilePath, macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE, Program, ProgramType, flags, 0, shaderBlob.put(), shaderErrors.put()))) {
			L->warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
			return nullptr;
		}
		if (shaderErrors)
			L->debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));

		if (!_stricmp(ProgramType, "ps_5_0")) {
			ID3D11PixelShader* regShader;
			DX::ThrowIfFailed(device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
			return regShader;
		}
		else if (!_stricmp(ProgramType, "vs_5_0")) {
			ID3D11VertexShader* regShader;
			DX::ThrowIfFailed(device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
			return regShader;
		}
		else if (!_stricmp(ProgramType, "hs_5_0")) {
			ID3D11HullShader* regShader;
			DX::ThrowIfFailed(device->CreateHullShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
			return regShader;
		}
		else if (!_stricmp(ProgramType, "ds_5_0")) {
			ID3D11DomainShader* regShader;
			DX::ThrowIfFailed(device->CreateDomainShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
			return regShader;
		}
		else if (!_stricmp(ProgramType, "cs_5_0")) {
			ID3D11ComputeShader* regShader;
			DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
			return regShader;
		}
		else if (!_stricmp(ProgramType, "cs_4_0")) {
			ID3D11ComputeShader* regShader;
			DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
			return regShader;
		}

		return nullptr;
	}
}

}
