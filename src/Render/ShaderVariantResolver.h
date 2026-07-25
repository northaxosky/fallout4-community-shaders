#pragma once

#include "Render/PixelShaderSwapBroker.h"

#include <array>
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

		inline constexpr std::array
			kBsdfLightDeferredPoint{
				ShaderVariantKeyView{
					"BSDFLightShader",
					ShaderStage::kPixel,
					ShaderVariantId{ 0x00001204 }
				},
				ShaderVariantKeyView{
					"BSDFLightShader",
					ShaderStage::kPixel,
					ShaderVariantId{ 0x10001204 }
				}
			};
		inline constexpr std::array
			kBsdfLightDeferredDirectional{
				ShaderVariantKeyView{
					"BSDFLightShader",
					ShaderStage::kPixel,
					ShaderVariantId{ 0x01200202 }
				},
				ShaderVariantKeyView{
					"BSDFLightShader",
					ShaderStage::kPixel,
					ShaderVariantId{ 0x01200282 }
				},
				ShaderVariantKeyView{
					"BSDFLightShader",
					ShaderStage::kPixel,
					ShaderVariantId{ 0x11200202 }
				}
			};
		inline constexpr std::array
			kBsdfLightDeferredDirectionalIbl{
				ShaderVariantKeyView{
					"BSDFLightShader",
					ShaderStage::kPixel,
					ShaderVariantId{ 0x01220202 }
				},
				ShaderVariantKeyView{
					"BSDFLightShader",
					ShaderStage::kPixel,
					ShaderVariantId{ 0x01220282 }
				},
				ShaderVariantKeyView{
					"BSDFLightShader",
					ShaderStage::kPixel,
					ShaderVariantId{ 0x11220202 }
				}
			};
	}

	std::optional<ShaderVariantKeyView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits,
		std::optional<bool> a_tileLightingEnabled) noexcept;
}
