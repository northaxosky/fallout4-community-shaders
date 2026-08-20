#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/ShaderVariantCompilation.h"
#include "Utils/ShaderCompile.h"
#include "generated/VertexShaderCompilePermutations.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <thread>
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
	using ShaderDefines =
		std::vector<std::pair<std::string, std::string>>;

	struct ShaderCase
	{
		const char* path;
		ShaderDefines defines;
		const char* profile{ "cs_5_0" };
		const char* entryPoint{ "main" };
	};

	struct ShaderCompileJob
	{
		std::filesystem::path path;
		ShaderDefines         defines;
		std::string           profile;
		std::string           entryPoint;
		std::string           description;
		std::string           preparationError;
	};

	struct ShaderCompileResult
	{
		std::string error;
	};

	std::string CompileInputKey(
		const std::filesystem::path& a_path,
		const ShaderDefines& a_defines,
		const char* a_profile = "cs_5_0",
		const char* a_entryPoint = "main")
	{
		std::string key = a_path.string();
		key.append("|").append(a_profile).append("|").append(a_entryPoint);
		for (const auto& [name, value] : a_defines)
			key.append("|").append(name).append("=").append(value);
		return key;
	}

	void AddCompile(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_path,
		ShaderDefines a_defines,
		const char* a_profile = "cs_5_0",
		const char* a_entryPoint = "main",
		std::string a_context = {})
	{
		std::string description = a_context.empty() ?
			a_path.string() :
			std::move(a_context) + ": " + a_path.string();
		description.append(" [")
			.append(a_profile)
			.append("/")
			.append(a_entryPoint);
		for (const auto& [name, value] : a_defines)
			description.append(", ").append(name).append("=").append(value);
		description.append("]");

		a_jobs.push_back({
			.path = a_path,
			.defines = std::move(a_defines),
			.profile = a_profile,
			.entryPoint = a_entryPoint,
			.description = std::move(description)
		});
	}

	void AddPreparationFailure(
		std::vector<ShaderCompileJob>& a_jobs,
		std::string a_description,
		std::string a_error)
	{
		a_jobs.push_back({
			.description = std::move(a_description),
			.preparationError = std::move(a_error)
		});
	}

	ShaderCompileResult Compile(const ShaderCompileJob& a_job)
	{
		if (!a_job.preparationError.empty())
			return { a_job.preparationError };

		std::vector<std::pair<const char*, const char*>> defines;
		defines.reserve(a_job.defines.size());
		for (const auto& [name, value] : a_job.defines)
			defines.emplace_back(name.c_str(), value.c_str());

		const std::wstring widePath = a_job.path.wstring();
		std::string error;
		const auto blob = cs::util::CompileShaderToBlob(
			widePath.c_str(),
			defines,
			a_job.profile.c_str(),
			a_job.entryPoint.c_str(),
			&error);
		return { blob ? std::string{} : std::move(error) };
	}

	int CompileAll(const std::vector<ShaderCompileJob>& a_jobs)
	{
		if (a_jobs.empty())
			return 0;

		std::vector<ShaderCompileResult> results(a_jobs.size());
		std::atomic_size_t nextJob{ 0 };
		const auto hardwareThreads =
			std::max(1u, std::thread::hardware_concurrency());
		const auto workerCount = std::min({
			a_jobs.size(),
			static_cast<std::size_t>(hardwareThreads),
			std::size_t{ 16 }
		});
		{
			std::vector<std::jthread> workers;
			workers.reserve(workerCount);
			for (std::size_t worker = 0; worker < workerCount; ++worker) {
				workers.emplace_back([&a_jobs, &results, &nextJob] {
					for (;;) {
						const auto index =
							nextJob.fetch_add(
								1,
								std::memory_order_relaxed);
						if (index >= a_jobs.size())
							return;
						results[index] = Compile(a_jobs[index]);
					}
				});
			}
		}

		int failures = 0;
		for (std::size_t index = 0; index < a_jobs.size(); ++index) {
			if (results[index].error.empty())
				continue;
			std::printf(
				"FAIL: %s\n%s\n",
				a_jobs[index].description.c_str(),
				results[index].error.c_str());
			++failures;
		}
		return failures;
	}

	std::size_t AddScreenSpaceGI(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto firstJob = a_jobs.size();
		AddCompile(a_jobs, a_root / "XeGTAO" / "decode.cs.hlsl", {});
		AddCompile(a_jobs, a_root / "XeGTAO" / "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "1" } });
		AddCompile(a_jobs, a_root / "XeGTAO" / "gi.cs.hlsl", {});
		AddCompile(a_jobs, a_root / "XeGTAO" / "gi.cs.hlsl", { { "SSGI_BOUNCE", "1" } });
		AddCompile(a_jobs, a_root / "XeGTAO" / "denoise.cs.hlsl", {});
		AddCompile(a_jobs, a_root / "XeGTAO" / "denoise.cs.hlsl", { { "SSGI_BOUNCE", "1" } });
		AddCompile(a_jobs, a_root / "ResolveCS.hlsl", {});
		AddCompile(a_jobs, a_root / "BounceTelemetryCS.hlsl", {});
		AddCompile(a_jobs, a_root / "BounceIntegrationPS.hlsl", {}, "vs_5_0", "VSMain");
		AddCompile(a_jobs, a_root / "BounceIntegrationPS.hlsl", {}, "ps_5_0", "PSMain");
		return a_jobs.size() - firstJob;
	}

	void AddRegistration(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root,
		const cs::engine::ShaderReplacementVariantRegistration& a_registration,
		const ShaderDefines& a_contributorDefines,
		std::set<std::string>* a_uniqueInputs = nullptr)
	{
		const auto* target =
			cs::engine::GetShaderInjectionTarget(
				a_registration.targetId);
		if (!target) {
			AddPreparationFailure(
				a_jobs,
				"registration " + a_registration.name,
				"Registration target metadata is missing");
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
			AddPreparationFailure(
				a_jobs,
				"registration " + a_registration.name,
				"Effective compile request failed: "
					+ compileTestError);
			return;
		}

		ShaderDefines defines;
		defines.reserve(compileTestRequest->defines.size());
		for (const auto& [name, value] : compileTestRequest->defines)
			defines.emplace_back(name, value);
		const auto path = a_root / compileTestRequest->sourcePath;
		if (a_uniqueInputs) {
			a_uniqueInputs->insert(CompileInputKey(
				path,
				defines,
				compileTestRequest->profile.c_str(),
				compileTestRequest->entryPoint.c_str()));
		}
		AddCompile(
			a_jobs,
			path,
			std::move(defines),
			compileTestRequest->profile.c_str(),
			compileTestRequest->entryPoint.c_str(),
			"registration " + a_registration.name);
	}

	struct LightingCounts
	{
		std::size_t registrationDerived = 0;
		std::size_t uniqueRegistrationInputs = 0;
		std::size_t explicitPermutations = 0;
	};

	LightingCounts AddLighting(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto registrations =
			cs::engine::GetDefaultShaderReplacementVariants();
		if (registrations.empty()) {
			AddPreparationFailure(
				a_jobs,
				"shader replacement registrations",
				"No shader replacement registrations were discovered");
		}
		std::set<std::string> uniqueRegistrationInputs;
		for (const auto& registration : registrations)
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{},
				&uniqueRegistrationInputs);

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
		const auto compileCases =
			[&a_jobs, &a_root](const auto& a_cases) {
			for (const auto& shader : a_cases) {
				AddCompile(
					a_jobs,
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
					AddRegistration(
						a_jobs,
						a_root,
						registration,
						defines);
					++contributorCompositionCount;
				}
			}
			if (registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite) {
				for (const auto& defines : ambientCompositions) {
					AddRegistration(
						a_jobs,
						a_root,
						registration,
						defines);
					++contributorCompositionCount;
				}
			}
		}
		return {
			.registrationDerived = registrations.size(),
			.uniqueRegistrationInputs =
				uniqueRegistrationInputs.size(),
			.explicitPermutations =
				featureCompositionCases.size()
				+ explicitSourceCases.size()
				+ contributorCompositionCount
		};
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

	std::size_t AddVertexPermutations(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto permutations =
			cs::test::shader_compile::GetVertexShaderCompilePermutations();
		if (permutations.empty()) {
			AddPreparationFailure(
				a_jobs,
				"vertex shader permutations",
				"No vertex shader permutations were discovered");
		}
		for (const auto& permutation : permutations) {
			const auto* source = SourceForVertexFamily(permutation.family);
			if (!source) {
				AddPreparationFailure(
					a_jobs,
					"vertex " + std::string(permutation.label),
					"Unknown vertex permutation family '"
						+ std::string(permutation.family) + "'");
				continue;
			}
			ShaderDefines defines;
			defines.reserve(permutation.defines.size());
			for (const auto& [name, value] : permutation.defines)
				defines.emplace_back(name, value);
			AddCompile(
				a_jobs,
				a_root / source,
				std::move(defines),
				"vs_5_0",
				"main",
				"vertex " + std::string(permutation.family)
					+ "/" + permutation.label);
		}
		return permutations.size();
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

	std::vector<ShaderCompileJob> jobs;
	const auto screenSpaceGiCount = AddScreenSpaceGI(jobs, argv[1]);
	const auto lightingCounts = AddLighting(jobs, argv[2]);
	const auto vertexCount = AddVertexPermutations(jobs, argv[2]);

	std::printf(
		"ShaderCompile checked %zu ScreenSpaceGI permutations\n",
		screenSpaceGiCount);
	std::printf(
		"ShaderCompile checked %zu base registration permutations (%zu unique inputs) and %zu lighting explicit/composed permutations\n",
		lightingCounts.registrationDerived,
		lightingCounts.uniqueRegistrationInputs,
		lightingCounts.explicitPermutations);
	std::printf(
		"ShaderCompile checked %zu vertex permutations\n",
		vertexCount);

	const int failures = CompileAll(jobs);

	if (failures == 0)
		std::printf("ShaderCompile passed\n");
	else
		std::printf("%d shader(s) failed to compile\n", failures);

	return failures ? 1 : 0;
}
