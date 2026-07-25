#include "Render/ShaderVariantRuntimeResolver.h"

#include "PCH.h"

namespace cs::engine
{
	namespace
	{
		std::optional<bool> QueryTileLightingEnabled() noexcept
		{
			if (!IsPixelShaderVariantResolutionAvailable())
				return std::nullopt;

			static REL::Relocation<bool()> tileLightingGetter{
				REL::ID(2318371)
			};
			// Tilelight is frame-dependent, so read it for each creation.
			return tileLightingGetter();
		}
	}

	bool IsPixelShaderVariantResolutionAvailable() noexcept
	{
		return REX::FModule::IsRuntimeNG()
			|| REX::FModule::IsRuntimeAE();
	}

	std::optional<PixelShaderVariantView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits) noexcept
	{
		if (a_subclass != "BSDFCompositeShader")
			return std::nullopt;

		return ResolvePixelShaderVariant(
			a_subclass,
			a_techniqueBits,
			QueryTileLightingEnabled());
	}
}
