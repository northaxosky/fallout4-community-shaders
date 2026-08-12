#include "Render/ShaderVariantResolver.h"

namespace cs::engine
{
	namespace
	{
		constexpr std::uint32_t kBsdfCompositePixelShaderMask =
			0xFFFFFBE9;
		constexpr std::uint32_t kTileLighting = 0x10000;
		constexpr std::uint32_t kBsdfLightOverdrawMask =
			0x140;
		constexpr std::uint32_t kBsdfLightOverdrawPixelShaderMask =
			0xF801257F;
		constexpr std::uint32_t kBsdfLightKeyFeatureMask =
			0x4003A;
		constexpr std::uint32_t kBsdfLightFallbackPixelShaderMask =
			0xFC07FF7F;

		std::optional<ShaderVariantId> ResolveBsdfCompositeVariantId(
			std::uint32_t a_techniqueBits,
			std::optional<bool> a_tileLightingEnabled) noexcept
		{
			if (!a_tileLightingEnabled)
				return std::nullopt;

			auto pixelShaderId =
				a_techniqueBits & kBsdfCompositePixelShaderMask;
			if (*a_tileLightingEnabled)
				pixelShaderId |= kTileLighting;
			else
				pixelShaderId &= ~kTileLighting;
			return ShaderVariantId{ pixelShaderId };
		}

		ShaderVariantId ResolveBsdfLightVariantId(
			std::uint32_t a_techniqueBits) noexcept
		{
			if ((a_techniqueBits & kBsdfLightOverdrawMask) != 0) {
				return ShaderVariantId{
					a_techniqueBits
						& kBsdfLightOverdrawPixelShaderMask
				};
			}
			if ((a_techniqueBits & kBsdfLightKeyFeatureMask) != 0)
				return ShaderVariantId{ a_techniqueBits };
			// CPU setup may consume bits absent from PSID.
			return ShaderVariantId{
				a_techniqueBits
					& kBsdfLightFallbackPixelShaderMask
			};
		}
	}

	std::optional<ShaderVariantKeyView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits,
		std::optional<bool> a_tileLightingEnabled) noexcept
	{
		if (a_subclass == "BSDFCompositeShader") {
			if (const auto id =
					ResolveBsdfCompositeVariantId(
						a_techniqueBits,
						a_tileLightingEnabled)) {
				return ShaderVariantKeyView{
					a_subclass,
					ShaderStage::kPixel,
					*id
				};
			}
		}
		if (a_subclass == "BSDFLightShader") {
			return ShaderVariantKeyView{
				a_subclass,
				ShaderStage::kPixel,
				ResolveBsdfLightVariantId(a_techniqueBits)
			};
		}
		return std::nullopt;
	}
}
