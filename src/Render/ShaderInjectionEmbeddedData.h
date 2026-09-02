#pragma once

#include <string_view>

namespace cs::engine::embedded
{
	std::string_view BsdfShaderReplacementVariants() noexcept;
	std::string_view StaticFamilyShaderReplacementVariants() noexcept;
}
