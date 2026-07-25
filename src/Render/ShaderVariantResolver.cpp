#include "Render/ShaderVariantResolver.h"

namespace cs::engine
{
	namespace
	{
		std::optional<ShaderVariantKey> ResolveBsdfCompositeVariantKey(
			std::uint32_t) noexcept
		{
			// Tilelight's runtime-global addresses are unresolved on OG/NG.
			return std::nullopt;
		}
	}

	std::optional<PixelShaderVariantView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits) noexcept
	{
		if (a_subclass == "BSDFCompositeShader") {
			if (const auto key =
					ResolveBsdfCompositeVariantKey(a_techniqueBits)) {
				return PixelShaderVariantView{ a_subclass, *key };
			}
		}
		return std::nullopt;
	}
}
