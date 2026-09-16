#include "Render/ShaderInjection.h"

#include "Render/ShaderInjectionDefines.h"

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

		std::optional<ShaderVariantCompilationDescriptor>
			BuildEffectiveShaderCompileRequestImpl(
				const ShaderInjectionTargetMetadata& a_target,
				ShaderStage a_stage,
				const ShaderVariantCompilationDescriptor& a_family,
				std::span<const ShaderReplacementRegistration> a_contributions,
				std::string* a_error)
		{
			ShaderVariantCompilationDescriptor request{
				.sourcePath = a_family.sourcePath,
				.entryPoint = a_family.entryPoint,
				.profile = a_family.profile
			};
			for (const auto& define : a_target.baseDefines)
				request.defines.emplace(define.name, define.value);

			const auto stage = ShaderStageBit(a_stage);
			bool substrateActive = false;
			for (const auto& contribution : a_contributions) {
				if (contribution.targetId != a_target.id
					|| (contribution.stages & stage) == 0) {
					continue;
				}
				substrateActive = true;
				if (!MergeDefines(
						request.defines,
						contribution.defines,
						a_error)) {
					return std::nullopt;
				}
			}
			if (substrateActive
				&& !MergeDefines(
					request.defines,
					{ { shader_injection_defines::kSubstrate, "1" } },
					a_error)) {
				return std::nullopt;
			}
			if (!MergeDefines(request.defines, a_family.defines, a_error))
				return std::nullopt;
			if (a_error)
				a_error->clear();
			return request;
		}
	}

	std::optional<ShaderVariantCompilationDescriptor>
		BuildEffectiveShaderCompileRequest(
			const ShaderInjectionTargetMetadata& a_target,
			ShaderStage a_stage,
			const ShaderVariantCompilationDescriptor& a_family,
			std::span<const ShaderReplacementRegistration> a_contributions,
			std::string* a_error)
	{
		return BuildEffectiveShaderCompileRequestImpl(
			a_target,
			a_stage,
			a_family,
			a_contributions,
			a_error);
	}
}
