#pragma once

#include <cstdint>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <optional>
#include <winrt/base.h>

namespace cs::util
{
	[[nodiscard]] inline std::optional<std::uint16_t> ReflectShaderSamplers(const void* a_bytecode, std::size_t a_size)
	{
		winrt::com_ptr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(a_bytecode, a_size, IID_PPV_ARGS(reflection.put()))))
			return std::nullopt;
		D3D11_SHADER_DESC desc{};
		if (FAILED(reflection->GetDesc(&desc)))
			return std::nullopt;
		std::uint16_t mask = 0;
		for (UINT index = 0; index < desc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &binding)))
				return std::nullopt;
			if (binding.Type != D3D_SIT_SAMPLER)
				continue;
			for (UINT slot = binding.BindPoint; slot < binding.BindPoint + binding.BindCount && slot < 16; ++slot)
				mask |= static_cast<std::uint16_t>(1u << slot);
		}
		return mask;
	}
}
