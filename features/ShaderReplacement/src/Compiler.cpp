#include "Compiler.h"

#include "Log.h"
#include "Sha1.h"

#include <algorithm>
#include <cstdio>
#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <vector>
#include <winrt/base.h>

namespace cs::features::replacement
{
	namespace { auto* L = cs::log::Get("cs.feature.shaderreplacement"); }

	bool CompileEntry(ID3D11Device* device, ReplacementEntry& e)
	{
		e.compile_attempted = true;
		e.compile_ok = false;
		e.compile_error.clear();
		e.compiled_sha1_hex.clear();
		e.compiled_ps = nullptr;

		if (!device) {
			e.compile_error = "no D3D11 device";
			L->error("Compile '{}' failed: {}", e.name, e.compile_error);
			return false;
		}

		std::error_code ec;
		if (!std::filesystem::exists(e.hlsl_abs_path, ec)) {
			std::string narrow;
			narrow.reserve(e.hlsl_abs_path.size());
			for (wchar_t c : e.hlsl_abs_path)
				narrow.push_back(static_cast<char>(c & 0x7F));
			e.compile_error = "source missing: " + narrow;
			L->error("Compile '{}' failed: {}", e.name, e.compile_error);
			return false;
		}

		std::vector<D3D_SHADER_MACRO> macros;
		macros.reserve(e.defines.size() + 1);
		for (const auto& d : e.defines)
			macros.push_back({ d.first.c_str(), d.second.c_str() });
		macros.push_back({ nullptr, nullptr });

		const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

		winrt::com_ptr<ID3DBlob> blob;
		winrt::com_ptr<ID3DBlob> errors;

		HRESULT hr = D3DCompileFromFile(
			e.hlsl_abs_path.c_str(),
			macros.data(),
			D3D_COMPILE_STANDARD_FILE_INCLUDE,
			e.entry.c_str(),
			e.profile.c_str(),
			flags, 0,
			blob.put(), errors.put());

		if (FAILED(hr) || !blob) {
			const char* msg = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
			e.compile_error = msg ? msg : "unknown error";
			L->error("Compile '{}' failed (hr=0x{:08x}):\n{}", e.name,
				static_cast<unsigned>(hr), e.compile_error);
			return false;
		}

		winrt::com_ptr<ID3D11PixelShader> ps;
		hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, ps.put());
		if (FAILED(hr) || !ps) {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "CreatePixelShader hr=0x%08x", static_cast<unsigned>(hr));
			e.compile_error = buf;
			L->error("Compile '{}' failed: {}", e.name, e.compile_error);
			return false;
		}

		const auto sha = Sha1Compute(blob->GetBufferPointer(), blob->GetBufferSize());
		e.compiled_sha1_hex = Sha1ToHex(sha);
		e.compiled_ps = std::move(ps);
		e.compile_ok = true;

		L->info("Compile '{}' ok: {} bytes, dxbc-sha1={}", e.name,
			static_cast<std::size_t>(blob->GetBufferSize()), e.compiled_sha1_hex);
		return true;
	}
}
