#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/ShaderVariantCompilation.h"
#include "Utils/ShaderCompile.h"
#include "generated/VertexShaderCompilePermutations.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cs::log
{
	spdlog::logger* Get(const char*)
	{
		return spdlog::default_logger_raw();
	}
}

namespace cs::engine
{
	std::shared_ptr<ShaderVariantCompilationPolicy>
		CreateEagerShaderVariantCompilationPolicy()
	{
		return {};
	}

	bool RegisterPixelShaderSwapResolver(ShaderSwapResolver)
	{
		return true;
	}

	bool RegisterPixelShaderSwapResolver(
		PixelShaderSwapResolverRegistration)
	{
		return true;
	}

	bool PixelShaderSwapBrokerHooksInstalled() noexcept
	{
		return true;
	}

	void EnsurePreSunLightDrawInstalled()
	{}
}

namespace
{
	using ShaderDefines = std::vector<std::pair<const char*, const char*>>;

	struct ShaderCase
	{
		const char* path;
		ShaderDefines defines;
		const char* profile{ "cs_5_0" };
		const char* entryPoint{ "main" };
	};

	int failures = 0;

	void Compile(
		const std::filesystem::path& a_path,
		const ShaderDefines& a_defines,
		const char* a_profile = "cs_5_0",
		const char* a_entryPoint = "main")
	{
		const std::wstring widePath = a_path.wstring();
		std::string error;
		const auto blob = cs::util::CompileShaderToBlob(
			widePath.c_str(),
			a_defines,
			a_profile,
			a_entryPoint,
			&error);
		if (!blob) {
			std::printf("FAIL: %s\n%s\n", a_path.string().c_str(), error.c_str());
			++failures;
		}
	}

	void CompileScreenSpaceGI(const std::filesystem::path& a_root)
	{
		Compile(a_root / "XeGTAO" / "decode.cs.hlsl", {});
		Compile(a_root / "XeGTAO" / "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "1" } });
		Compile(a_root / "XeGTAO" / "gi.cs.hlsl", {});
		Compile(a_root / "XeGTAO" / "gi.cs.hlsl", { { "SSGI_BOUNCE", "1" } });
		Compile(a_root / "XeGTAO" / "denoise.cs.hlsl", {});
		Compile(a_root / "XeGTAO" / "denoise.cs.hlsl", { { "SSGI_BOUNCE", "1" } });
		Compile(a_root / "ResolveCS.hlsl", {});
		Compile(a_root / "BounceTelemetryCS.hlsl", {});
		Compile(a_root / "BounceIntegrationPS.hlsl", {}, "vs_5_0", "VSMain");
		Compile(a_root / "BounceIntegrationPS.hlsl", {}, "ps_5_0", "PSMain");
	}

	void CompileRegistration(
		const std::filesystem::path& a_root,
		const cs::engine::ShaderReplacementVariantRegistration& a_registration,
		const ShaderDefines& a_contributorDefines)
	{
		const auto* target =
			cs::engine::GetShaderInjectionTarget(
				a_registration.targetId);
		if (!target) {
			std::printf("FAIL: registration target metadata is missing\n");
			++failures;
			return;
		}

		std::vector<cs::engine::ShaderReplacementRegistration>
			contributions;
		if (!a_contributorDefines.empty()) {
			cs::engine::ShaderReplacementRegistration contribution;
			contribution.targetId = a_registration.targetId;
			contribution.contributor = "ShaderCompile";
			for (const auto& [name, value] : a_contributorDefines)
				contribution.defines.emplace(name, value);
			contributions.push_back(std::move(contribution));
		}

		std::string compileTestError;
		const auto compileTestRequest =
			cs::engine::BuildEffectiveShaderCompileRequest(
				*target,
				a_registration,
				contributions,
				&compileTestError);
		if (!compileTestRequest) {
			std::printf(
				"FAIL: effective compile request for %s: %s\n",
				a_registration.name.c_str(),
				compileTestError.c_str());
			++failures;
			return;
		}

		ShaderDefines defines;
		defines.reserve(compileTestRequest->defines.size());
		for (const auto& [name, value] : compileTestRequest->defines)
			defines.emplace_back(name.c_str(), value.c_str());
		Compile(
			a_root / compileTestRequest->sourcePath,
			defines,
			compileTestRequest->profile.c_str(),
			compileTestRequest->entryPoint.c_str());
	}

	void CompileLighting(const std::filesystem::path& a_root)
	{
		const auto registrations =
			cs::engine::GetDefaultShaderReplacementVariants();
		if (registrations.empty()) {
			std::printf("FAIL: no shader replacement registrations were discovered\n");
			++failures;
		}
		for (const auto& registration : registrations)
			CompileRegistration(a_root, registration, {});

		const std::array<ShaderCase, 3> featureCompositionCases{ {
			{
				"BSDFLightShader.hlsl",
				{
					{ "BSDFLIGHT_PS_DEFERRED", "1" },
					{ "LIGHT_TYPE", "1" },
					{
						cs::engine::shader_injection_defines::
							kScreenSpaceShadows,
						"1"
					}
				},
				"ps_5_0"
			},
			{
				"BSDFLightShader.hlsl",
				{
					{ "AMBIENT_IBL_IN_LIGHT", "1" },
					{ "BSDFLIGHT_PS_DEFERRED", "1" },
					{ "LIGHT_TYPE", "1" }
				},
				"ps_5_0"
			},
			{
				"BSDFLightShader.hlsl",
				{
					{ "AMBIENT_IBL_IN_LIGHT", "1" },
					{ "BSDFLIGHT_PS_DEFERRED", "1" },
					{ "LIGHT_TYPE", "1" },
					{
						cs::engine::shader_injection_defines::
							kScreenSpaceShadows,
						"1"
					}
				},
				"ps_5_0"
			}
		} };
		const std::array<ShaderCase, 3> explicitSourceCases{ {
			{ "DeferredComposite.hlsl", {}, "ps_5_0" },
			{ "BSDFPrePass.hlsl", {}, "ps_5_0" },
			{ "VolumetricLighting.hlsl", {}, "ps_5_0" }
		} };
		const auto compileCases = [&a_root](const auto& a_cases) {
			for (const auto& shader : a_cases) {
				Compile(
					a_root / shader.path,
					shader.defines,
					shader.profile,
					shader.entryPoint);
			}
		};
		compileCases(featureCompositionCases);
		compileCases(explicitSourceCases);

		using namespace cs::engine::shader_injection_defines;
		const std::array<ShaderDefines, 3> directionalCompositions{ {
			{ { kScreenSpaceShadows, "1" } },
			{ { kWetnessEffects, "1" } },
			{
				{ kScreenSpaceShadows, "1" },
				{ kWetnessEffects, "1" }
			}
		} };
		const std::array<ShaderDefines, 3> ambientCompositions{ {
			{ { kScreenSpaceGi, "1" } },
			{ { kWetnessEffects, "1" } },
			{
				{ kScreenSpaceGi, "1" },
				{ kWetnessEffects, "1" }
			}
		} };
		std::size_t contributorCompositionCount = 0;
		for (const auto& registration : registrations) {
			const auto* compositions =
				registration.targetId
						== cs::engine::ShaderInjectionTarget::kBsdfLight
				? &directionalCompositions
				: nullptr;
			if (compositions) {
				for (const auto& defines : *compositions) {
					CompileRegistration(a_root, registration, defines);
					++contributorCompositionCount;
				}
			}
			if (registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite) {
				for (const auto& defines : ambientCompositions) {
					CompileRegistration(a_root, registration, defines);
					++contributorCompositionCount;
				}
			}
		}
		std::printf(
			"ShaderCompile checked %zu registration-derived and %zu explicit permutations\n",
			registrations.size(),
			featureCompositionCases.size() + explicitSourceCases.size()
				+ contributorCompositionCount);
	}

	const char* SourceForVertexFamily(const char* a_family)
	{
		const std::string family(a_family);
		if (family == "BSSky")
			return "BSSkyShader.hlsl";
		if (family == "BSWater")
			return "BSWaterShader.hlsl";
		if (family == "BSLighting")
			return "BSLightingShader.hlsl";
		return nullptr;
	}

	void CompileVertexPermutations(const std::filesystem::path& a_root)
	{
		const auto permutations =
			cs::test::shader_compile::GetVertexShaderCompilePermutations();
		if (permutations.empty()) {
			std::printf("FAIL: no vertex shader permutations were discovered\n");
			++failures;
		}
		for (const auto& permutation : permutations) {
			const auto* source = SourceForVertexFamily(permutation.family);
			if (!source) {
				std::printf(
					"FAIL: unknown vertex permutation family '%s'\n",
					permutation.family);
				++failures;
				continue;
			}
			Compile(a_root / source, permutation.defines, "vs_5_0", "main");
		}
		std::printf(
			"ShaderCompile checked %zu vertex permutations\n",
			permutations.size());
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::fprintf(
			stderr,
			"Usage: ShaderCompileTests <ScreenSpaceGI shader directory> <lighting shader directory>\n");
		return 2;
	}

	CompileScreenSpaceGI(argv[1]);
	CompileLighting(argv[2]);
	CompileVertexPermutations(argv[2]);

	if (failures == 0)
		std::printf("ShaderCompile passed\n");
	else
		std::printf("%d shader(s) failed to compile\n", failures);

	return failures ? 1 : 0;
}
