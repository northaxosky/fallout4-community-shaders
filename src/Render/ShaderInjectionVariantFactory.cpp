#include "Render/ShaderInjectionVariantFactory.h"

#include <utility>

namespace cs::engine
{
	std::string_view ProfileForStage(
		ShaderStage a_stage) noexcept
	{
		static_assert(
			static_cast<std::uint8_t>(ShaderStage::kCount) == 3);
		switch (a_stage) {
		case ShaderStage::kVertex:
			return "vs_5_0";
		case ShaderStage::kPixel:
			return "ps_5_0";
		case ShaderStage::kCompute:
			return "cs_5_0";
		}
		std::unreachable();
	}

	ShaderReplacementVariantRegistration MakeDefaultVariantRegistration(
		ShaderInjectionTarget a_target,
		std::string a_name,
		std::vector<ShaderVariantKey> a_variantKeys,
		std::string a_expectedStockSha1,
		ShaderInjectionDefines a_defines,
		ShaderStage a_stage)
	{
		const auto* metadata = GetShaderInjectionTarget(a_target);
		ShaderReplacementVariantRegistration registration;
		registration.targetId = a_target;
		registration.name = std::move(a_name);
		registration.variantKeys = std::move(a_variantKeys);
		registration.expectedStockSha1 =
			std::move(a_expectedStockSha1);
		registration.stage = a_stage;
		if (metadata) {
			registration.compilation.sourcePath =
				std::wstring(metadata->sourcePath);
			registration.compilation.entryPoint =
				std::string(metadata->entryPoint);
		}
		registration.compilation.profile =
			std::string(ProfileForStage(a_stage));
		registration.compilation.defines =
			std::move(a_defines);
		return registration;
	}
}
