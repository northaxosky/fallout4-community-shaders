#include "Render/ShaderVariantRuntimeResolver.h"

#include "PCH.h"

namespace cs::engine
{
	namespace
	{
		std::optional<bool> QueryNgAeTileLightingEnabled() noexcept
		{
			if (!REX::FModule::IsRuntimeNG()
				&& !REX::FModule::IsRuntimeAE()) {
				return std::nullopt;
			}

			// This ID is shared by NG/AE; OG is intentionally unresolved.
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

	std::optional<ShaderVariantKeyView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits) noexcept
	{
		if (!IsPixelShaderVariantResolutionAvailable())
			return std::nullopt;

		if (a_subclass != "BSDFCompositeShader") {
			return ResolvePixelShaderVariant(
				a_subclass,
				a_techniqueBits,
				std::nullopt);
		}

		return ResolvePixelShaderVariant(
			a_subclass,
			a_techniqueBits,
			QueryNgAeTileLightingEnabled());
	}
}
