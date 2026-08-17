#pragma once
// Non-generated internal helper: shared registration builder for ShaderInjection.cpp and generated static-family sources.

#include "Render/ShaderInjection.h"

#include <string>
#include <string_view>
#include <vector>

namespace cs::engine
{
	std::string_view ProfileForStage(
		ShaderStage a_stage) noexcept;

	ShaderReplacementVariantRegistration MakeDefaultVariantRegistration(
		ShaderInjectionTarget a_target,
		std::string a_name,
		std::vector<ShaderVariantKey> a_variantKeys,
		std::string a_expectedStockSha1,
		ShaderInjectionDefines a_defines = {},
		ShaderStage a_stage = ShaderStage::kPixel);
}
