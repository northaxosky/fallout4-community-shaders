#pragma once

#include "Render/PixelShaderSwapBroker.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::engine
{
	namespace shader_variants
	{
		inline constexpr ShaderVariantKey kBsdfCompositeAmbientIbl{ 0xB60 };
		inline constexpr ShaderVariantKey kBsdfCompositeAmbientIblTilelight{ 0x10B60 };
	}

	std::optional<PixelShaderVariantView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits) noexcept;
}
