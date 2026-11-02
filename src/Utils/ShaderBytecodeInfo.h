#pragma once

#include "Utils/ShaderBufferBindings.h"
#include "Utils/ShaderSamplerBindings.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace cs::util
{
	// Everything a compile or cache hit derives from bytecode, via one D3DReflect.
	struct ShaderBytecodeInfo
	{
		std::string slotError;
		std::optional<ShaderSamplerUse> samplers;
	};

	[[nodiscard]] inline ShaderBytecodeInfo InspectShaderBytecode(std::span<const std::uint8_t> a_bytecode)
	{
		winrt::com_ptr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(a_bytecode.data(), a_bytecode.size(), IID_PPV_ARGS(reflection.put()))))
			return { .slotError = "Substrate slot validation could not reflect shader" };
		return { .slotError = ValidateSubstrateSlots(reflection.get()), .samplers = ReflectShaderSamplers(reflection.get()) };
	}
}
