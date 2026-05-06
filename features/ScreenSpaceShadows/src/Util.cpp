#include "Util.h"

#include <d3dcompiler.h>
#include <filesystem>
#include <winrt/base.h>

#include "Log.h"

namespace cs::features::sss::Util
{
	namespace { auto* L = cs::log::Get("cs.feature.sss.util"); }

	ID3D11DeviceChild* CompileShader(const wchar_t* a_filePath,
		const std::vector<std::pair<const char*, const char*>>& a_defines,
		const char* a_programType,
		const char* a_program)
	{
		auto rendererData = RE::BSGraphics::GetRendererData();
		auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

		std::vector<D3D_SHADER_MACRO> macros;
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
			L->warn("Shader compilation failed:\n{}",
				shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
			return nullptr;
		}
		if (shaderErrors)
			L->debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));

		if (!_stricmp(a_programType, "cs_5_0")) {
			ID3D11ComputeShader* cs = nullptr;
			DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &cs));
			return cs;
		}
		return nullptr;
	}
}
