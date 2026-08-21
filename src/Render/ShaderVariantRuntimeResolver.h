#pragma once

#include "Render/ShaderVariantResolver.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::engine
{
	struct PixelShaderRuntimeResolverCodeAddresses
	{
		std::uintptr_t runtimeResolver = 0;
		std::uintptr_t formulaResolver = 0;
	};

	bool IsPixelShaderVariantResolutionAvailable() noexcept;

	// Nullopt on OG, where the tiled-lighting getter has no resolved address.
	std::optional<bool> QueryTiledLightingEnabled() noexcept;

	std::optional<PixelShaderRuntimeRoute> ResolvePixelShaderRuntimeRoute(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits) noexcept;
	std::optional<ShaderVariantKeyView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits) noexcept;
	PixelShaderRuntimeResolverCodeAddresses
		GetPixelShaderRuntimeResolverCodeAddresses() noexcept;
}
