#include "Render/ShaderVariantRuntimeResolver.h"

#include "PCH.h"

namespace cs::engine
{
	namespace
	{
		__declspec(noinline)
		std::optional<ShaderVariantKeyView>
			ResolvePixelShaderVariantFormulaForIdentity(
				std::string_view a_subclass,
				std::uint32_t a_techniqueBits,
				std::optional<bool> a_tiledLighting) noexcept
		{
			volatile std::uint32_t stableTechnique =
				a_techniqueBits;
			return ResolvePixelShaderVariant(
				a_subclass,
				stableTechnique,
				a_tiledLighting);
		}

		std::optional<bool> QueryNgAeTileLightingEnabled() noexcept
		{
			if (!REX::FModule::IsRuntimeNG()
				&& !REX::FModule::IsRuntimeAE()) {
				return std::nullopt;
			}

			// NG and AE share this ID; OG remains unresolved.
			static REL::Relocation<bool()> tileLightingGetter{
				REL::ID(2318371)
			};
			// Tilelight changes per frame.
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
		const auto route = ResolvePixelShaderRuntimeRoute(
			a_subclass, a_techniqueBits);
		if (!route || !route->pluginResolvedPsid)
			return std::nullopt;
		return ShaderVariantKeyView{
			route->subclass,
			route->stage,
			*route->pluginResolvedPsid
		};
	}

	std::optional<PixelShaderRuntimeRoute> ResolvePixelShaderRuntimeRoute(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits) noexcept
	{
		if (a_subclass.empty())
			return std::nullopt;

		PixelShaderRuntimeRoute route{
			.subclass = a_subclass,
			.stage = ShaderStage::kPixel,
			.rawTechnique = a_techniqueBits
		};
		if (!IsPixelShaderVariantResolutionAvailable())
			return route;

		if (a_subclass == "BSDFCompositeShader") {
			route.tiledLighting = QueryNgAeTileLightingEnabled();
		}

		const auto variant =
			ResolvePixelShaderVariantFormulaForIdentity(
			a_subclass,
			a_techniqueBits,
			route.tiledLighting);
		if (variant)
			route.pluginResolvedPsid = variant->id;
		return route;
	}

	PixelShaderRuntimeResolverCodeAddresses
		GetPixelShaderRuntimeResolverCodeAddresses() noexcept
	{
		return {
			reinterpret_cast<std::uintptr_t>(
				&ResolvePixelShaderRuntimeRoute),
			reinterpret_cast<std::uintptr_t>(
				&ResolvePixelShaderVariantFormulaForIdentity)
		};
	}
}
