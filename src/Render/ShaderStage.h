#pragma once

#include <cstdint>
#include <string_view>

namespace cs::engine
{
	enum class ShaderStage : std::uint8_t
	{
		kVertex = 0,
		kPixel = 1,
		kCompute = 2,
		kCount = 3
	};

	constexpr std::string_view ShaderStageName(ShaderStage a_stage) noexcept
	{
		switch (a_stage) {
		case ShaderStage::kVertex:
			return "vertex";
		case ShaderStage::kPixel:
			return "pixel";
		case ShaderStage::kCompute:
			return "compute";
		default:
			return "unknown";
		}
	}

	using ShaderStageMask = std::uint32_t;

	constexpr ShaderStageMask ShaderStageBit(ShaderStage a_stage) noexcept
	{
		return ShaderStageMask{ 1 } << static_cast<unsigned>(a_stage);
	}
}
