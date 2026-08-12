#include "Utils/CSUtil.h"
#include "Utils/ShaderCompile.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "Log.h"

namespace cs::util
{
	namespace
	{
		auto* L = cs::log::Get("cs.util");
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
		// Accept UTF-8 BOMs from common Windows editors.
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
		// Delete consumed smoke markers to prevent stale overrides.
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

		std::string narrow;
		std::wstring wide{ a_filePath };
		std::transform(wide.begin(), wide.end(), std::back_inserter(narrow), [](wchar_t c) { return (char)c; });

		std::string shaderLogs;
		auto shaderBlob = CompileShaderToBlob(a_filePath, a_defines, a_programType, a_program, &shaderLogs);
		if (!shaderBlob) {
			if (shaderLogs == "Shader source missing")
				L->error("Shader source missing: {}", narrow);
			else
				L->warn("Shader compilation failed ({}):\n{}", narrow,
					shaderLogs.empty() ? "Unknown error" : shaderLogs);
			return nullptr;
		}
		if (!shaderLogs.empty())
			L->debug("Shader logs ({}):\n{}", narrow, shaderLogs);

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
