#pragma once

#include "Render/SubstrateSlots.h"

#include <array>
#include <cstring>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <string>
#include <string_view>
#include <wrl/client.h>

namespace cs::util
{
	[[nodiscard]] inline std::string ValidateSubstrateSlots(ID3DBlob* a_blob)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(a_blob->GetBufferPointer(), a_blob->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))))
			return "Substrate slot validation could not reflect shader";
		D3D11_SHADER_DESC desc{};
		if (FAILED(reflection->GetDesc(&desc)))
			return "Substrate slot validation could not read shader";
		const char* names[]{ "PerFrame", "SharedData", "FeatureData", "FO4SharedData" };
		std::array<UINT, render::kSubstrateBufferCount> bindings{};
		for (UINT index = 0; index < desc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &binding)))
				return "Substrate slot validation could not read binding";
			if (binding.Type != D3D_SIT_CBUFFER || binding.BindPoint < render::kFrameDataSlot || binding.BindPoint > render::kFO4SharedDataSlot)
				continue;
			const auto slot = binding.BindPoint - render::kFrameDataSlot;
			const auto name = std::string_view(binding.Name);
			if (++bindings[slot] > 1 || (name != names[slot] && !name.ends_with(std::string("::") + names[slot])))
				return "Constant-buffer slot collision at b" + std::to_string(binding.BindPoint);
		}
		return {};
	}
}
