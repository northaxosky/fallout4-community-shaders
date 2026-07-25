#pragma once

#include "Render/PixelShaderSwapBroker.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::engine
{
	namespace shader_variants
	{
		inline constexpr ShaderVariantKeyView kBsdfCompositeAmbientIbl{
			"BSDFCompositeShader",
			ShaderStage::kPixel,
			ShaderVariantId{ 0xB60 }
		};
		inline constexpr ShaderVariantKeyView
			kBsdfCompositeAmbientIblTilelight{
				"BSDFCompositeShader",
				ShaderStage::kPixel,
				ShaderVariantId{ 0x10B60 }
			};
	}

	std::optional<ShaderVariantKeyView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits,
		std::optional<bool> a_tileLightingEnabled) noexcept;
}
