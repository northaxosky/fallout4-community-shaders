#include "Render/ShaderInjection.h"

namespace cs::engine
{
	namespace
	{
		bool MergeDefines(
			ShaderInjectionDefines& a_destination,
			const ShaderInjectionDefines& a_source,
			std::string* a_error)
		{
			for (const auto& [name, value] : a_source) {
				const auto [existing, inserted] =
					a_destination.emplace(name, value);
				if (inserted || existing->second == value)
					continue;
				if (a_error)
					*a_error = "conflicting shader define: " + name;
				return false;
			}
			return true;
		}
	}

	std::optional<ShaderVariantCompilationDescriptor>
		BuildEffectiveShaderCompileRequest(
			const ShaderInjectionTargetMetadata& a_target,
			const ShaderReplacementVariantRegistration& a_variant,
			std::span<const ShaderReplacementRegistration> a_contributions,
			std::string* a_error)
	{
		ShaderVariantCompilationDescriptor request{
			.sourcePath = a_variant.compilation.sourcePath,
			.entryPoint = a_variant.compilation.entryPoint,
			.profile = a_variant.compilation.profile
		};
		for (const auto& define : a_target.baseDefines)
			request.defines.emplace(define.name, define.value);

		const auto stage = ShaderStageBit(a_variant.stage);
		for (const auto& contribution : a_contributions) {
			if (contribution.targetId != a_target.id
				|| (contribution.stages & stage) == 0) {
				continue;
			}
			if (!MergeDefines(
					request.defines,
					contribution.defines,
					a_error)) {
				return std::nullopt;
			}
		}
		if (!MergeDefines(
				request.defines,
				a_variant.compilation.defines,
				a_error)) {
			return std::nullopt;
		}
		if (a_error)
			a_error->clear();
		return request;
	}
}
