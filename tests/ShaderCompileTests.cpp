#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/SharedData.h"
#include "Utils/ShaderCompile.h"
#include "generated/VertexShaderCompilePermutations.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
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
		CreateCachingShaderVariantCompilationPolicy()
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

namespace cs::render
{
	void EnsureSharedDataUpdateInstalled()
	{}

	bool IsSharedDataReady() noexcept
	{
		return true;
	}

	void BindSharedData(ID3D11DeviceContext*) noexcept
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

	enum class SubstrateExpectation
	{
		kNone,
		kAbsent,
		kPresent
	};

	struct ShaderCompileJob
	{
		std::filesystem::path path;
		ShaderDefines         defines;
		std::string           profile;
		std::string           entryPoint;
		std::string           description;
		std::string           preparationError;
		SubstrateExpectation  substrateExpectation =
			SubstrateExpectation::kNone;
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
		std::string a_context = {},
		SubstrateExpectation a_substrateExpectation =
			SubstrateExpectation::kNone)
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
			.description = std::move(description),
			.substrateExpectation = a_substrateExpectation
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

	struct ExpectedVariable
	{
		const char* name;
		UINT        offset;
		UINT        size;
	};

	std::string ValidateConstantBuffer(
		ID3D11ShaderReflection* a_reflection,
		const char* a_name,
		UINT a_bindPoint,
		UINT a_size,
		std::span<const ExpectedVariable> a_variables)
	{
		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(a_reflection->GetDesc(&shaderDesc)))
			return "shared substrate reflection description failed";

		std::optional<D3D11_SHADER_INPUT_BIND_DESC> binding;
		for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC candidate{};
			if (SUCCEEDED(a_reflection->GetResourceBindingDesc(
					index,
					&candidate))
				&& candidate.Type == D3D_SIT_CBUFFER
				&& candidate.BindPoint == a_bindPoint) {
				binding = candidate;
				break;
			}
		}
		if (!binding)
			return std::string("missing reflected binding b")
				+ std::to_string(a_bindPoint);
		if (binding->BindCount != 1) {
			return std::string("unexpected binding for ") + a_name;
		}

		auto* buffer = a_reflection->GetConstantBufferByName(binding->Name);
		D3D11_SHADER_BUFFER_DESC bufferDesc{};
		if (!buffer || FAILED(buffer->GetDesc(&bufferDesc)))
			return std::string("missing reflected cbuffer ") + a_name;
		if (bufferDesc.Size != a_size
			|| bufferDesc.Variables != a_variables.size()) {
			return std::string("unexpected reflected layout for ") + a_name;
		}

		for (std::size_t index = 0; index < a_variables.size(); ++index) {
			const auto& expected = a_variables[index];
			auto* variable = buffer->GetVariableByIndex(
				static_cast<UINT>(index));
			D3D11_SHADER_VARIABLE_DESC variableDesc{};
			if (!variable || FAILED(variable->GetDesc(&variableDesc))) {
				return std::string("missing reflected variable ")
					+ a_name + "." + expected.name;
			}
			if (variableDesc.StartOffset != expected.offset
				|| variableDesc.Size != expected.size) {
				return std::string("unexpected reflected variable layout ")
					+ a_name + "." + expected.name;
			}
		}
		return {};
	}

	std::string ValidateSubstrateReflection(
		ID3DBlob* a_blob,
		SubstrateExpectation a_expectation)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(reflection.GetAddressOf())))) {
			return "D3DReflect failed for the shared substrate probe";
		}

		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(reflection->GetDesc(&shaderDesc)))
			return "shared substrate reflection description failed";
		if (a_expectation == SubstrateExpectation::kAbsent) {
			if (shaderDesc.ConstantBuffers != 0
				|| shaderDesc.BoundResources != 0) {
				return "inactive shared substrate probe emitted resources";
			}
			return {};
		}

		constexpr std::array sharedVariables{
			ExpectedVariable{ "CameraData", 0, 16 },
			ExpectedVariable{ "BufferDim", 16, 16 },
			ExpectedVariable{ "DynamicResolution", 32, 16 },
			ExpectedVariable{ "NDCToViewMul", 48, 16 },
			ExpectedVariable{ "NDCToViewAdd", 64, 16 },
			ExpectedVariable{ "SunDirection", 80, 16 },
			ExpectedVariable{ "Timer", 96, 4 },
			ExpectedVariable{ "DeltaTime", 100, 4 },
			ExpectedVariable{ "FrameCount", 104, 4 },
			ExpectedVariable{ "InInterior", 108, 4 }
		};
		constexpr std::array featureVariables{
			ExpectedVariable{ "screenSpaceShadowsSettings", 0, 16 },
			ExpectedVariable{ "screenSpaceGISettings", 16, 16 },
			ExpectedVariable{ "wetnessEffectsSettings", 32, 16 }
		};
		if (shaderDesc.ConstantBuffers != 2
			|| shaderDesc.BoundResources != 2) {
			return "active shared substrate probe emitted the wrong resource count";
		}
		if (auto error = ValidateConstantBuffer(
				reflection.Get(),
				"SharedData",
				5,
				112,
				sharedVariables);
			!error.empty()) {
			return error;
		}
		return ValidateConstantBuffer(
			reflection.Get(),
			"FeatureData",
			6,
			48,
			featureVariables);
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
		if (!blob)
			return { std::move(error) };
		if (a_job.substrateExpectation != SubstrateExpectation::kNone) {
			return {
				ValidateSubstrateReflection(
					blob.Get(),
					a_job.substrateExpectation)
			};
		}
		return {};
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

	std::size_t AddSharedDataProbes(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto probe = a_root / "SharedDataProbe.hlsl";
		AddCompile(
			a_jobs,
			probe,
			{},
			"ps_5_0",
			"main",
			"shared substrate inactive",
			SubstrateExpectation::kAbsent);
		AddCompile(
			a_jobs,
			probe,
			{ { "FO4CS_SUBSTRATE", "1" } },
			"ps_5_0",
			"main",
			"shared substrate active",
			SubstrateExpectation::kPresent);
		return 2;
	}

	std::size_t AddScreenSpaceGI(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto firstJob = a_jobs.size();
		const auto screenSpaceGiRoot = a_root / "ScreenSpaceGI";
		AddCompile(a_jobs, screenSpaceGiRoot / "XeGTAO" / "decode.cs.hlsl", {});
		AddCompile(a_jobs, screenSpaceGiRoot / "XeGTAO" / "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "1" } });
		AddCompile(a_jobs, screenSpaceGiRoot / "XeGTAO" / "gi.cs.hlsl", {});
		AddCompile(a_jobs, screenSpaceGiRoot / "XeGTAO" / "gi.cs.hlsl", { { "SSGI_BOUNCE", "1" } });
		AddCompile(a_jobs, screenSpaceGiRoot / "XeGTAO" / "denoise.cs.hlsl", {});
		AddCompile(a_jobs, screenSpaceGiRoot / "XeGTAO" / "denoise.cs.hlsl", { { "SSGI_BOUNCE", "1" } });
		AddCompile(a_jobs, screenSpaceGiRoot / "ResolveCS.hlsl", {});
		AddCompile(a_jobs, screenSpaceGiRoot / "BounceTelemetryCS.hlsl", {});
		AddCompile(a_jobs, screenSpaceGiRoot / "BounceIntegrationPS.hlsl", {}, "vs_5_0", "VSMain");
		AddCompile(a_jobs, screenSpaceGiRoot / "BounceIntegrationPS.hlsl", {}, "ps_5_0", "PSMain");
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
		if (uniqueRegistrationInputs.size() != registrations.size()) {
			AddPreparationFailure(
				a_jobs,
				"base registration compile inputs",
				"Expected " + std::to_string(registrations.size())
					+ " unique inputs, found "
					+ std::to_string(
						uniqueRegistrationInputs.size()));
		}

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

	struct VertexFamilySource
	{
		std::string_view family;
		const char* source;
	};

	constexpr std::array kVertexFamilySources{
		VertexFamilySource{ "BSSky", "BSSkyShader.hlsl" },
		VertexFamilySource{ "BSWater", "BSWaterShader.hlsl" },
		VertexFamilySource{ "BSLighting", "BSLightingShader.hlsl" }
	};

	const VertexFamilySource* SourceForVertexFamily(
		std::string_view a_family)
	{
		const auto family = std::ranges::find(
			kVertexFamilySources,
			a_family,
			&VertexFamilySource::family);
		return family == kVertexFamilySources.end() ?
			nullptr :
			&*family;
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
		std::array<bool, kVertexFamilySources.size()> represented{};
		for (const auto& permutation : permutations) {
			const auto* family =
				SourceForVertexFamily(permutation.family);
			if (!family) {
				AddPreparationFailure(
					a_jobs,
					"vertex " + std::string(permutation.label),
					"Unknown vertex permutation family '"
						+ std::string(permutation.family) + "'");
				continue;
			}
			const auto familyIndex = static_cast<std::size_t>(
				family - kVertexFamilySources.data());
			represented[familyIndex] = true;
			ShaderDefines defines;
			defines.reserve(permutation.defines.size());
			for (const auto& [name, value] : permutation.defines)
				defines.emplace_back(name, value);
			AddCompile(
				a_jobs,
				a_root / family->source,
				std::move(defines),
				"vs_5_0",
				"main",
				"vertex " + std::string(permutation.family)
					+ "/" + permutation.label);
		}
		for (std::size_t index = 0;
			index < kVertexFamilySources.size();
			++index) {
			if (represented[index])
				continue;
			AddPreparationFailure(
				a_jobs,
				"vertex family "
					+ std::string(kVertexFamilySources[index].family),
				"No vertex shader permutations were discovered for this family");
		}
		return permutations.size();
	}
}

int main(int argc, char** argv)
{
	if (argc != 2) {
		std::fprintf(
			stderr,
			"Usage: ShaderCompileTests <shader directory>\n");
		return 2;
	}

	std::vector<ShaderCompileJob> jobs;
	const auto sharedDataCount = AddSharedDataProbes(jobs, argv[1]);
	const auto screenSpaceGiCount = AddScreenSpaceGI(jobs, argv[1]);
	const auto lightingCounts = AddLighting(jobs, argv[1]);
	const auto vertexCount = AddVertexPermutations(jobs, argv[1]);

	std::printf(
		"ShaderCompile checked %zu shared substrate probes\n",
		sharedDataCount);
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
