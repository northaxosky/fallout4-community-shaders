#pragma once

#include "Render/SubstrateSlots.h"

#include <cstdint>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <optional>
#include <string_view>
#include <winrt/base.h>

namespace cs::util
{
	struct ShaderSamplerUse
	{
		// Unknown (never reflected) assumes every slot is declared.
		std::uint16_t declared = UINT16_MAX;
		// Declares the host-owned sampler by its shader name, not just its slot.
		bool hostLinearClamp = false;
	};

	[[nodiscard]] inline std::optional<ShaderSamplerUse> ReflectShaderSamplers(ID3D11ShaderReflection* reflection)
	{
		D3D11_SHADER_DESC desc{};
		if (FAILED(reflection->GetDesc(&desc)))
			return std::nullopt;
		ShaderSamplerUse use{ .declared = 0 };
		for (UINT index = 0; index < desc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &binding)))
				return std::nullopt;
			if (binding.Type != D3D_SIT_SAMPLER)
				continue;
			for (UINT slot = binding.BindPoint; slot < binding.BindPoint + binding.BindCount && slot < 16; ++slot)
				use.declared |= static_cast<std::uint16_t>(1u << slot);
			if (binding.BindPoint == render::kLinearClampSamplerSlot && std::string_view(binding.Name) == "LinearSampler")
				use.hostLinearClamp = true;
		}
		return use;
	}

	[[nodiscard]] inline std::optional<ShaderSamplerUse> ReflectShaderSamplers(const void* a_bytecode, std::size_t a_size)
	{
		winrt::com_ptr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(a_bytecode, a_size, IID_PPV_ARGS(reflection.put()))))
			return std::nullopt;
		return ReflectShaderSamplers(reflection.get());
	}
}
