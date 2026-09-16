#pragma once

#include <cstdint>

namespace cs::engine
{
	enum class ShaderStage : std::uint8_t
	{
		kVertex,
		kPixel,
		kCompute,
		kCount
	};

	using ShaderStageMask = std::uint32_t;

	constexpr ShaderStageMask ShaderStageBit(ShaderStage a_stage) noexcept
	{
		return ShaderStageMask{ 1 } << static_cast<unsigned>(a_stage);
	}
}
