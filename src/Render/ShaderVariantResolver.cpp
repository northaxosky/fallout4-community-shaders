#include "Render/ShaderVariantResolver.h"

namespace cs::engine
{
	namespace
	{
		constexpr std::uint32_t kBsdfCompositePixelShaderMask =
			0xFFFFFBE9;
		constexpr std::uint32_t kTileLighting = 0x10000;

		std::optional<ShaderVariantKey> ResolveBsdfCompositeVariantKey(
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
			return ShaderVariantKey{ pixelShaderId };
		}
	}

	std::optional<PixelShaderVariantView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits,
		std::optional<bool> a_tileLightingEnabled) noexcept
	{
		if (a_subclass == "BSDFCompositeShader") {
			if (const auto key =
					ResolveBsdfCompositeVariantKey(
						a_techniqueBits,
						a_tileLightingEnabled)) {
				return PixelShaderVariantView{ a_subclass, *key };
			}
		}
		return std::nullopt;
	}
}
