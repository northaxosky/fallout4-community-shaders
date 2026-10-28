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
	}

	std::optional<ShaderInjectionDefines> GatherShaderInjectionDefines(
		ShaderInjectionTarget a_target,
		ShaderStageMask a_stages,
		std::span<const ShaderReplacementRegistration> a_contributions,
		std::string* a_error)
	{
		ShaderInjectionDefines defines;
		for (const auto& contribution : a_contributions) {
			if (contribution.targetId != a_target || (contribution.stages & a_stages) == 0 ||
				!contribution.feature || !contribution.feature->IsLoaded() ||
				!contribution.feature->HasShaderDefine(a_target)) {
				continue;
			}
			if (!MergeDefines(
					defines,
					{ { std::string(contribution.feature->GetShaderDefineName()), "1" } },
					a_error)) {
				return std::nullopt;
			}
			for (const auto& [name, value] : contribution.feature->GetShaderDefineOptions(a_target)) {
				if (!MergeDefines(defines, { { std::string(name), std::string(value) } }, a_error))
					return std::nullopt;
			}
		}
		return defines;
	}

	std::optional<ShaderVariantCompilationDescriptor>
	BuildEffectiveShaderCompileRequest(
		const ShaderInjectionTargetMetadata& a_target,
		ShaderStage a_stage,
		const ShaderVariantCompilationDescriptor& a_family,
		std::span<const ShaderReplacementRegistration> a_contributions,
		GameRuntime a_runtime,
		std::string* a_error)
	{
		auto defines = GatherShaderInjectionDefines(a_target.id, ShaderStageBit(a_stage), a_contributions, a_error);
		if (!defines)
			return std::nullopt;
		const bool substrateActive = !defines->empty();
		ShaderVariantCompilationDescriptor request{
			.sourcePath = a_family.sourcePath,
			.entryPoint = a_family.entryPoint,
			.profile = a_family.profile,
			.defines = std::move(*defines)
		};
		if (!MergeDefines(request.defines, { { "FRAMEBUFFER_REGISTER", "b4" } }, a_error))
			return std::nullopt;
		if (substrateActive && !MergeDefines(
								   request.defines,
								   { { shader_injection_defines::kSubstrate, "1" } },
								   a_error)) {
			return std::nullopt;
		}
		if (a_runtime == GameRuntime::kOG &&
			!MergeDefines(request.defines, { { shader_injection_defines::kRuntimeOG, "1" } }, a_error))
			return std::nullopt;
		if (!MergeDefines(request.defines, a_family.defines, a_error))
			return std::nullopt;
		if (a_error)
			a_error->clear();
		return request;
	}

}
