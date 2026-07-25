#pragma once

#include "Render/ShaderVariantResolver.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::engine
{
	bool IsPixelShaderVariantResolutionAvailable() noexcept;

	std::optional<ShaderVariantKeyView> ResolvePixelShaderVariant(
		std::string_view a_subclass,
		std::uint32_t a_techniqueBits) noexcept;
}
