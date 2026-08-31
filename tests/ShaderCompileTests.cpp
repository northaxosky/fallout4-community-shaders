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
#include <cstring>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <map>
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

	bool EnsureDeferredDrawAnchorInstalled()
	{
		return true;
	}
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

	enum class FeatureIdentityVariant : std::uint8_t
	{
		kBase,
		kWetness,
		kTerrainShadows,
		kInverseSquareLighting
	};

	struct FeatureOffIdentityExpectation
	{
		std::string            key;
		FeatureIdentityVariant variant = FeatureIdentityVariant::kBase;
		bool                   wetnessShouldDiffer = false;
		bool                   terrainShouldDiffer = false;
		bool                   inverseSquareShouldDiffer = false;
		bool                   expectTerrainVariant = false;
		bool                   expectInverseSquareVariant = false;
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
		bool                  validateXeGTAOCB = false;
		std::vector<UINT>     requiredTextureSlots;
		std::vector<UINT>     forbiddenTextureSlots;
		std::vector<UINT>     requiredSamplerSlots;
		std::vector<UINT>     forbiddenSamplerSlots;
		std::optional<FeatureOffIdentityExpectation> featureOffIdentity;
	};

	struct ShaderCompileResult
	{
		std::string error;
		Microsoft::WRL::ComPtr<ID3DBlob> blob;
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

	ShaderCompileJob& AddCompile(
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
		return a_jobs.back();
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

	// Named lookup catches fields optimized out of a permutation.
	std::string ValidateConstantBufferOffsets(
		ID3D11ShaderReflection* a_reflection,
		const char* a_name,
		UINT a_bindPoint,
		UINT a_size,
		std::span<const ExpectedVariable> a_variables)
	{
		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(a_reflection->GetDesc(&shaderDesc)))
			return "constant buffer reflection description failed";

		std::optional<D3D11_SHADER_INPUT_BIND_DESC> binding;
		for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC candidate{};
			if (SUCCEEDED(a_reflection->GetResourceBindingDesc(index, &candidate))
				&& candidate.Type == D3D_SIT_CBUFFER
				&& candidate.BindPoint == a_bindPoint) {
				binding = candidate;
				break;
			}
		}
		if (!binding)
			return std::string("missing reflected binding b")
				+ std::to_string(a_bindPoint);

		auto* buffer = a_reflection->GetConstantBufferByName(binding->Name);
		D3D11_SHADER_BUFFER_DESC bufferDesc{};
		if (!buffer || FAILED(buffer->GetDesc(&bufferDesc)))
			return std::string("missing reflected cbuffer ") + a_name;
		if (bufferDesc.Size != a_size) {
			return std::string("unexpected reflected size ")
				+ std::to_string(bufferDesc.Size) + " for " + a_name;
		}

		for (const auto& expected : a_variables) {
			auto* variable = buffer->GetVariableByName(expected.name);
			D3D11_SHADER_VARIABLE_DESC variableDesc{};
			if (!variable || FAILED(variable->GetDesc(&variableDesc))) {
				return std::string("missing reflected variable ")
					+ a_name + "." + expected.name;
			}
			if (variableDesc.StartOffset != expected.offset
				|| variableDesc.Size != expected.size) {
				return std::string("unexpected reflected variable layout ")
					+ a_name + "." + expected.name + " at "
					+ std::to_string(variableDesc.StartOffset);
			}
		}
		return {};
	}

	std::string ValidateXeGTAOConstantBuffer(ID3DBlob* a_blob)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(reflection.GetAddressOf())))) {
			return "D3DReflect failed for the XeGTAO constant buffer witness";
		}

		constexpr std::array giVariables{
			ExpectedVariable{ "NDCToViewMul", 0, 16 },
			ExpectedVariable{ "NDCToViewAdd", 16, 16 },
			ExpectedVariable{ "TexDim", 32, 8 },
			ExpectedVariable{ "RcpTexDim", 40, 8 },
			ExpectedVariable{ "FrameDim", 48, 8 },
			ExpectedVariable{ "RcpFrameDim", 56, 8 },
			ExpectedVariable{ "PrevFrameDim", 64, 8 },
			ExpectedVariable{ "RcpPrevFrameDim", 72, 8 },
			ExpectedVariable{ "FrameIndex", 80, 4 },
			ExpectedVariable{ "NumSlices", 84, 4 },
			ExpectedVariable{ "NumSteps", 88, 4 },
			ExpectedVariable{ "MinScreenRadius", 92, 4 },
			ExpectedVariable{ "AORadius", 96, 4 },
			ExpectedVariable{ "EffectRadius", 100, 4 },
			ExpectedVariable{ "Thickness", 104, 4 },
			ExpectedVariable{ "GIRadius", 108, 4 },
			ExpectedVariable{ "DepthFadeRange", 112, 8 },
			ExpectedVariable{ "DepthFadeScaleConst", 120, 4 },
			ExpectedVariable{ "BlurRadius", 124, 4 },
			ExpectedVariable{ "DistanceNormalisation", 128, 4 },
			ExpectedVariable{ "CenterBeta", 132, 4 },
			ExpectedVariable{ "DepthDisocclusion", 136, 4 },
			ExpectedVariable{ "MaxAccumFrames", 140, 4 },
			ExpectedVariable{ "TemporalFlags", 144, 4 },
			ExpectedVariable{ "RadianceScale", 160, 8 },
			ExpectedVariable{ "PrevNDCToViewMul", 176, 8 },
			ExpectedVariable{ "PrevNDCToViewAdd", 184, 8 },
			ExpectedVariable{ "ViewToWorld", 192, 48 },
			ExpectedVariable{ "PrevViewToWorld", 240, 48 },
			ExpectedVariable{ "CameraOrigin", 288, 16 },
			ExpectedVariable{ "PrevCameraOrigin", 304, 16 }
		};
		return ValidateConstantBufferOffsets(
			reflection.Get(), "XeGTAOCB", 0, 320, giVariables);
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
			ExpectedVariable{ "wetnessEffectsSettings", 32, 16 },
			ExpectedVariable{ "terrainShadowsSettings", 48, 48 },
			ExpectedVariable{
				"inverseSquareLightingSettings", 96, 16 },
			ExpectedVariable{ "waterEffectsSettings", 112, 16 },
			ExpectedVariable{
				"extendedTranslucencySettings", 128, 16 }
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
			144,
			featureVariables);
	}

	std::string ValidateTextureBindings(
		ID3DBlob* a_blob,
		const ShaderCompileJob& a_job)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(reflection.GetAddressOf())))) {
			return "D3DReflect failed for the texture binding witness";
		}

		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(reflection->GetDesc(&shaderDesc)))
			return "texture binding reflection description failed";

		std::set<UINT> boundTextures;
		std::set<UINT> boundSamplers;
		for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &binding))) {
				continue;
			}
			for (UINT slot = 0; slot < std::max(binding.BindCount, 1u); ++slot) {
				if (binding.Type == D3D_SIT_TEXTURE)
					boundTextures.insert(binding.BindPoint + slot);
				else if (binding.Type == D3D_SIT_SAMPLER)
					boundSamplers.insert(binding.BindPoint + slot);
			}
		}

		for (const UINT slot : a_job.requiredTextureSlots) {
			if (!boundTextures.contains(slot))
				return "missing reflected texture t" + std::to_string(slot);
		}
		for (const UINT slot : a_job.forbiddenTextureSlots) {
			if (boundTextures.contains(slot))
				return "unexpected reflected texture t" + std::to_string(slot);
		}
		for (const UINT slot : a_job.requiredSamplerSlots) {
			if (!boundSamplers.contains(slot))
				return "missing reflected sampler s" + std::to_string(slot);
		}
		for (const UINT slot : a_job.forbiddenSamplerSlots) {
			if (boundSamplers.contains(slot))
				return "unexpected reflected sampler s" + std::to_string(slot);
		}
		return {};
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
		auto blob = cs::util::CompileShaderToBlob(
			widePath.c_str(),
			defines,
			a_job.profile.c_str(),
			a_job.entryPoint.c_str(),
			&error);
		if (!blob)
			return { std::move(error) };
		if (a_job.substrateExpectation != SubstrateExpectation::kNone) {
			if (auto validation = ValidateSubstrateReflection(
					blob.Get(),
					a_job.substrateExpectation);
				!validation.empty()) {
				return { std::move(validation) };
			}
		}
		if (a_job.validateXeGTAOCB) {
			if (auto validation = ValidateXeGTAOConstantBuffer(blob.Get());
				!validation.empty()) {
				return { std::move(validation) };
			}
		}
		if (!a_job.requiredTextureSlots.empty()
			|| !a_job.forbiddenTextureSlots.empty()
			|| !a_job.requiredSamplerSlots.empty()
			|| !a_job.forbiddenSamplerSlots.empty()) {
			if (auto validation = ValidateTextureBindings(blob.Get(), a_job);
				!validation.empty()) {
				return { std::move(validation) };
			}
		}
		if (!a_job.featureOffIdentity)
			return {};
		return { {}, std::move(blob) };
	}

	struct FeatureOffIdentityPair
	{
		const ShaderCompileJob* featureOffJob = nullptr;
		const ShaderCompileResult* featureOffResult = nullptr;
		const ShaderCompileJob* wetnessJob = nullptr;
		const ShaderCompileResult* wetnessResult = nullptr;
		const ShaderCompileJob* terrainJob = nullptr;
		const ShaderCompileResult* terrainResult = nullptr;
		const ShaderCompileJob* inverseSquareJob = nullptr;
		const ShaderCompileResult* inverseSquareResult = nullptr;
		bool wetnessShouldDiffer = false;
		bool terrainShouldDiffer = false;
		bool inverseSquareShouldDiffer = false;
		bool expectTerrainVariant = false;
		bool expectInverseSquareVariant = false;
		bool hasExpectation = false;
	};

	std::optional<bool> HaveEqualStrippedShaderBytes(
		ID3DBlob* a_featureOff,
		ID3DBlob* a_featureOn,
		std::string& a_error)
	{
		constexpr UINT stripFlags = D3DCOMPILER_STRIP_DEBUG_INFO
			| D3DCOMPILER_STRIP_REFLECTION_DATA
			| D3DCOMPILER_STRIP_TEST_BLOBS
			| D3DCOMPILER_STRIP_PRIVATE_DATA;
		Microsoft::WRL::ComPtr<ID3DBlob> featureOff;
		if (FAILED(D3DStripShader(
				a_featureOff->GetBufferPointer(),
				a_featureOff->GetBufferSize(),
				stripFlags,
				featureOff.GetAddressOf()))) {
			a_error = "D3DStripShader failed for the feature-off blob";
			return std::nullopt;
		}
		Microsoft::WRL::ComPtr<ID3DBlob> featureOn;
		if (FAILED(D3DStripShader(
				a_featureOn->GetBufferPointer(),
				a_featureOn->GetBufferSize(),
				stripFlags,
				featureOn.GetAddressOf()))) {
			a_error = "D3DStripShader failed for the feature-on blob";
			return std::nullopt;
		}
		return featureOff->GetBufferSize() == featureOn->GetBufferSize()
			&& std::memcmp(
				featureOff->GetBufferPointer(),
				featureOn->GetBufferPointer(),
				featureOff->GetBufferSize())
				== 0;
	}

	int ValidateFeatureOffIdentityPairs(
		const std::vector<ShaderCompileJob>& a_jobs,
		const std::vector<ShaderCompileResult>& a_results)
	{
		std::map<std::string, FeatureOffIdentityPair> pairs;
		int failures = 0;
		for (std::size_t index = 0; index < a_jobs.size(); ++index) {
			const auto& job = a_jobs[index];
			if (!job.featureOffIdentity)
				continue;

			const auto& identity = *job.featureOffIdentity;
			auto& pair = pairs[identity.key];
			if (identity.variant == FeatureIdentityVariant::kBase) {
				pair.wetnessShouldDiffer = identity.wetnessShouldDiffer;
				pair.terrainShouldDiffer = identity.terrainShouldDiffer;
				pair.inverseSquareShouldDiffer =
					identity.inverseSquareShouldDiffer;
				pair.expectTerrainVariant = identity.expectTerrainVariant;
				pair.expectInverseSquareVariant =
					identity.expectInverseSquareVariant;
				pair.hasExpectation = true;
			}

			const ShaderCompileJob** pairedJob = nullptr;
			const ShaderCompileResult** pairedResult = nullptr;
			const char* variantName = "feature-off";
			switch (identity.variant) {
			case FeatureIdentityVariant::kBase:
				pairedJob = &pair.featureOffJob;
				pairedResult = &pair.featureOffResult;
				break;
			case FeatureIdentityVariant::kWetness:
				pairedJob = &pair.wetnessJob;
				pairedResult = &pair.wetnessResult;
				variantName = "wetness";
				break;
			case FeatureIdentityVariant::kTerrainShadows:
				pairedJob = &pair.terrainJob;
				pairedResult = &pair.terrainResult;
				variantName = "terrain-shadows";
				break;
			case FeatureIdentityVariant::kInverseSquareLighting:
				pairedJob = &pair.inverseSquareJob;
				pairedResult = &pair.inverseSquareResult;
				variantName = "inverse-square-lighting";
				break;
			}
			if (*pairedJob) {
				std::printf(
					"FAIL: feature-off identity %s has duplicate %s variants\n",
					identity.key.c_str(),
					variantName);
				++failures;
				continue;
			}
			*pairedJob = &job;
			*pairedResult = &a_results[index];
		}

		const auto compareVariant = [&failures](
			const std::string& a_key,
			const char* a_variantName,
			const FeatureOffIdentityPair& a_pair,
			const ShaderCompileJob* a_variantJob,
			const ShaderCompileResult* a_variantResult,
			bool a_shouldDiffer) {
			if (!a_variantJob) {
				std::printf(
					"FAIL: feature-off identity %s is missing a %s variant\n",
					a_key.c_str(),
					a_variantName);
				++failures;
				return;
			}
			if (!a_pair.featureOffResult->error.empty()
				|| !a_variantResult->error.empty()) {
				return;
			}
			std::string error;
			const auto equal = HaveEqualStrippedShaderBytes(
				a_pair.featureOffResult->blob.Get(),
				a_variantResult->blob.Get(),
				error);
			if (!equal) {
				std::printf(
					"FAIL: feature-off identity %s (%s)\n%s\n",
					a_key.c_str(),
					a_variantName,
					error.c_str());
				++failures;
				return;
			}
			if (*equal == a_shouldDiffer) {
				std::printf(
					"FAIL: feature-off identity %s (%s) expected stripped DXBC to %s\n",
					a_key.c_str(),
					a_variantName,
					a_shouldDiffer ? "differ" : "match");
				++failures;
			}
		};

		for (const auto& [key, pair] : pairs) {
			if (!pair.hasExpectation || !pair.featureOffJob) {
				std::printf(
					"FAIL: feature-off identity %s is missing a feature-off variant\n",
					key.c_str());
				++failures;
				continue;
			}
			compareVariant(
				key,
				"wetness",
				pair,
				pair.wetnessJob,
				pair.wetnessResult,
				pair.wetnessShouldDiffer);
			if (pair.expectTerrainVariant) {
				compareVariant(
					key,
					"terrain-shadows",
					pair,
					pair.terrainJob,
					pair.terrainResult,
					pair.terrainShouldDiffer);
			} else if (pair.terrainJob) {
				std::printf(
					"FAIL: feature-off identity %s carries an unexpected terrain-shadows variant\n",
					key.c_str());
				++failures;
			}
			if (pair.expectInverseSquareVariant) {
				compareVariant(
					key,
					"inverse-square-lighting",
					pair,
					pair.inverseSquareJob,
					pair.inverseSquareResult,
					pair.inverseSquareShouldDiffer);
			} else if (pair.inverseSquareJob) {
				std::printf(
					"FAIL: feature-off identity %s carries an unexpected inverse-square variant\n",
					key.c_str());
				++failures;
			}
		}
		return failures;
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
		failures += ValidateFeatureOffIdentityPairs(a_jobs, results);
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

	std::size_t AddMenuShaders(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto menuRoot = a_root / "Menu";
		AddCompile(a_jobs, menuRoot / "BackgroundBlurDownsample.hlsl", {}, "vs_5_0", "VS_Main");
		AddCompile(a_jobs, menuRoot / "BackgroundBlurDownsample.hlsl", {}, "ps_5_0", "PS_Main");
		AddCompile(a_jobs, menuRoot / "BackgroundBlurHorizontal.hlsl", {}, "ps_5_0", "PS_Main");
		AddCompile(a_jobs, menuRoot / "BackgroundBlurVertical.hlsl", {}, "ps_5_0", "PS_Main");
		AddCompile(a_jobs, menuRoot / "BackgroundBlurComposite.hlsl", {}, "ps_5_0", "PS_Main");
		return 5;
	}

	constexpr std::size_t kScreenSpaceGIPermutations = 9;

	std::size_t AddScreenSpaceGI(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)	{
		const auto firstJob = a_jobs.size();
		const auto screenSpaceGiRoot = a_root / "ScreenSpaceGI" / "XeGTAO";

		// Non-decode permutations share one XeGTAOCB layout.
		AddCompile(a_jobs, screenSpaceGiRoot / "decode.cs.hlsl", {});

		const std::array<ShaderCase, 8> xegtaoCases{ {
			{ "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "1" } } },
			{ "prefilterRadiance.cs.hlsl", {} },
			{ "prefilterNormal.cs.hlsl", {} },
			{ "radianceDisocc.cs.hlsl", {} },
			{ "gi.cs.hlsl", {} },
			{ "gi.cs.hlsl", { { "SSGI_BOUNCE", "1" } } },
			{ "denoise.cs.hlsl", {} },
			{ "denoise.cs.hlsl", { { "SSGI_BOUNCE", "1" } } }
		} };
		for (const auto& shaderCase : xegtaoCases) {
			auto& job = AddCompile(
				a_jobs,
				screenSpaceGiRoot / shaderCase.path,
				shaderCase.defines);
			job.validateXeGTAOCB = true;
		}
		return a_jobs.size() - firstJob;
	}

	constexpr std::size_t kUpscalingPermutations = 7;

	std::size_t AddUpscaling(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto firstJob = a_jobs.size();
		const auto upscalingRoot = a_root / "Upscaling";

		const std::array<ShaderCase, 4> encodeCases{ {
			{ "EncodeTexturesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" } } },
			{ "EncodeTexturesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" }, { "DLSS", "" } } },
			{ "EncodeTexturesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" }, { "FSR", "" } } },
			{ "EncodeTexturesCS.hlsl", { { "FO4CS_SUBSTRATE", "1" }, { "DEPTH_OUTPUT", "" } } }
		} };
		for (const auto& shaderCase : encodeCases) {
			AddCompile(
				a_jobs,
				upscalingRoot / shaderCase.path,
				shaderCase.defines,
				"cs_5_0",
				"main",
				"upscaling encode");
		}

		AddCompile(
			a_jobs,
			upscalingRoot / "DepthRefractionUpscalePS.hlsl",
			{ { "PSHADER", "" }, { "FO4CS_SUBSTRATE", "1" } },
			"ps_5_0",
			"main",
			"upscaling depth");

		AddCompile(
			a_jobs,
			upscalingRoot / "UpscaleVS.hlsl",
			{ { "VSHADER", "" } },
			"vs_5_0",
			"main",
			"upscaling fullscreen");

		AddCompile(
			a_jobs,
			upscalingRoot / "RCAS" / "RCAS.hlsl",
			{},
			"cs_5_0",
			"main",
			"upscaling sharpening");

		return a_jobs.size() - firstJob;
	}

	constexpr std::size_t kTerrainShadowsPermutations = 2;

	std::size_t AddTerrainShadows(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto firstJob = a_jobs.size();
		AddCompile(
			a_jobs,
			a_root / "TerrainShadows" / "ShadowUpdate.cs.hlsl",
			{},
			"cs_5_0",
			"main",
			"terrain shadow update");
		AddCompile(
			a_jobs,
			a_root / "TerrainShadows" / "ShadowStatistics.cs.hlsl",
			{},
			"cs_5_0",
			"main",
			"terrain shadow statistics");
		return a_jobs.size() - firstJob;
	}

	struct SlotExpectations
	{
		std::vector<UINT> requiredTextures;
		std::vector<UINT> forbiddenTextures;
		std::vector<UINT> requiredSamplers;
		std::vector<UINT> forbiddenSamplers;
	};

	void AddRegistration(
		std::vector<ShaderCompileJob>& a_jobs,
		const std::filesystem::path& a_root,
		const cs::engine::ShaderReplacementVariantRegistration& a_registration,
		const ShaderDefines& a_contributorDefines,
		std::set<std::string>* a_uniqueInputs = nullptr,
		SlotExpectations a_slots = {},
		std::optional<FeatureOffIdentityExpectation> a_featureOffIdentity = {})
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
		auto& job = AddCompile(
			a_jobs,
			path,
			std::move(defines),
			compileTestRequest->profile.c_str(),
			compileTestRequest->entryPoint.c_str(),
			"registration " + a_registration.name);
		job.requiredTextureSlots = std::move(a_slots.requiredTextures);
		job.forbiddenTextureSlots = std::move(a_slots.forbiddenTextures);
		job.requiredSamplerSlots = std::move(a_slots.requiredSamplers);
		job.forbiddenSamplerSlots = std::move(a_slots.forbiddenSamplers);
		job.featureOffIdentity = std::move(a_featureOffIdentity);
	}

	struct LightingCounts
	{
		std::size_t registrationDerived = 0;
		std::size_t uniqueRegistrationInputs = 0;
		std::size_t explicitPermutations = 0;
		std::size_t ambientCompositionRows = 0;
		std::size_t ambientNonTargetRows = 0;
		std::size_t wetnessDirectRows = 0;
		std::size_t wetnessDirectInertRows = 0;
		std::size_t terrainDirectRows = 0;
		std::size_t terrainDirectInertRows = 0;
		std::size_t terrainCompositeRows = 0;
		std::size_t terrainCompositeInertRows = 0;
		std::size_t terrainCompositeVertexRows = 0;
		std::size_t wetnessDebugCompositeRows = 0;
		std::size_t wetnessDebugCompositeInertRows = 0;
		std::size_t wetnessDebugCompositeVertexRows = 0;
		std::size_t wetnessCompositeRows = 0;
		std::size_t wetnessCompositeNeutralRows = 0;
		std::size_t wetnessCompositeVertexRows = 0;
		std::size_t inverseSquareRows = 0;
		std::size_t inverseSquareInertRows = 0;
		std::size_t extendedTranslucencyRows = 0;
	};

	// The SSGI composition extends the existing plugin texture block.
	constexpr std::array kCompositionTextureSlots{ 26u, 27u, 28u, 29u };
	constexpr UINT kGbufferNormalTextureSlot = 25;

	// only families that can isolate directional ambient carry the composition
	constexpr std::array kAmbientCompositionFamilies{
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY"
	};

	// the authoritative normal is declared by every family that darkens or coats
	constexpr std::array kWetnessCompositeFamilies{
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY",
		"BSDFCOMPOSITE_PS_2D_ACCUMULATOR",
		"BSDFCOMPOSITE_PS_2D_FOG",
		"BSDFCOMPOSITE_PS_CUBE_IBL"
	};
	constexpr std::array kWetnessDirectFamilies{
		"BSDFLIGHT_PS_DEFERRED",
		"BSDFLIGHT_PS_DIRSPLITS1",
		"BSDFLIGHT_PS_DIRSPLITS2",
		"BSDFLIGHT_PS_DIRSPLITS3",
		"BSDFLIGHT_PS_GOBO",
		"BSDFLIGHT_PS_UNSHADOWED"
	};
	constexpr std::array kTerrainDebugDepthFallbackFamilies{
		"BSDFCOMPOSITE_PS_2D_ACCUMULATOR",
		"BSDFCOMPOSITE_PS_NO_SRV_POSITION_TEXCOORD",
		"BSDFCOMPOSITE_PS_NO_SRV_POSITION",
		"BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR"
	};

	bool IsInverseSquareConsumer(
		const cs::engine::ShaderReplacementVariantRegistration&
			a_registration)
	{
		if (a_registration.targetId
				!= cs::engine::ShaderInjectionTarget::kBsdfLight
			|| a_registration.stage != cs::engine::ShaderStage::kPixel) {
			return false;
		}
		const auto& defines = a_registration.compilation.defines;
		if (defines.contains("BSDFLIGHT_PS_ATTENUATION_ONLY"))
			return true;
		if (defines.contains("BSDFLIGHT_PS_GOBO"))
			return defines.contains("POINTOMNI");
		if (defines.contains("BSDFLIGHT_PS_UNSHADOWED"))
			return defines.contains("POINTOMNI");
		if (!defines.contains("BSDFLIGHT_PS_DEFERRED"))
			return false;
		const auto lightType = defines.find("LIGHT_TYPE");
		return lightType != defines.end() && lightType->second != "1";
	}

	constexpr UINT kTerrainShadowTextureSlot = 30;
	constexpr UINT kTerrainSceneDepthTextureSlot = 31;
	constexpr UINT kTerrainShadowSamplerSlot = 13;

	constexpr UINT kWaterCausticsTextureSlot = 32;
	constexpr UINT kWaterSceneDepthTextureSlot = 33;
	constexpr UINT kWaterCausticsSamplerSlot = 14;

	constexpr std::size_t kExpectedAmbientCompositionRows = 26;
	constexpr std::size_t kExpectedAmbientNonTargetRows = 44;
	constexpr std::size_t kExpectedBsdfLightRows = 167;
	constexpr std::size_t kExpectedWetnessDirectRows = 146;
	constexpr std::size_t kExpectedWetnessDirectInertRows = 21;
	constexpr std::size_t kExpectedTerrainDirectRows = 81;
	constexpr std::size_t kExpectedTerrainDirectInertRows = 86;
	constexpr std::size_t kExpectedInverseSquareRows = 81;
	constexpr std::size_t kExpectedInverseSquareInertRows = 86;
	constexpr std::size_t kExpectedTerrainCompositeRows = 70;
	constexpr std::size_t kExpectedTerrainCompositeInertRows = 0;
	constexpr std::size_t kExpectedWetnessDebugCompositeRows = 70;
	constexpr std::size_t kExpectedWetnessDebugCompositeInertRows = 0;
	constexpr std::size_t kExpectedCompositeRegistrationRows = 74;
	constexpr std::size_t kExpectedWetnessCompositeRows = 58;
	constexpr std::size_t kExpectedWetnessCompositeNeutralRows = 12;
	constexpr std::size_t kExpectedWetnessCompositeVertexRows = 4;
	constexpr std::size_t kExpectedExtendedTranslucencyRows = 12;

	bool DeclaresFamily(
		const cs::engine::ShaderReplacementVariantRegistration& a_registration,
		std::span<const char* const> a_families)
	{
		return std::ranges::any_of(
			a_families,
			[&a_registration](const char* a_family) {
				return a_registration.compilation.defines.contains(a_family);
			});
	}

	bool HasDefine(const ShaderDefines& a_defines, std::string_view a_name)
	{
		return std::ranges::any_of(
			a_defines,
			[a_name](const auto& a_define) { return a_define.first == a_name; });
	}

	bool IsWetnessDirectConsumer(
		const cs::engine::ShaderReplacementVariantRegistration& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfLight
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& DeclaresFamily(a_registration, kWetnessDirectFamilies)
			&& !a_registration.compilation.defines.contains("ATTENUATION_ONLY");
	}

	bool IsWetnessCompositeConsumer(
		const cs::engine::ShaderReplacementVariantRegistration& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& DeclaresFamily(a_registration, kWetnessCompositeFamilies);
	}

	bool IsWetnessConsumer(
		const cs::engine::ShaderReplacementVariantRegistration& a_registration)
	{
		return IsWetnessDirectConsumer(a_registration)
			|| IsWetnessCompositeConsumer(a_registration);
	}

	bool IsTerrainShadowConsumer(
		const cs::engine::ShaderReplacementVariantRegistration& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfLight
			&& a_registration.stage == cs::engine::ShaderStage::kPixel
			&& a_registration.compilation.defines.contains("DIRECTIONAL");
	}

	bool IsTerrainDebugCompositeConsumer(
		const cs::engine::ShaderReplacementVariantRegistration& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite
			&& a_registration.stage == cs::engine::ShaderStage::kPixel;
	}

	bool IsWetnessDebugCompositeConsumer(
		const cs::engine::ShaderReplacementVariantRegistration& a_registration)
	{
		return a_registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfComposite
			&& a_registration.stage == cs::engine::ShaderStage::kPixel;
	}

	bool UsesTerrainDebugDepthFallback(
		const cs::engine::ShaderReplacementVariantRegistration& a_registration)
	{
		return IsTerrainDebugCompositeConsumer(a_registration)
			&& DeclaresFamily(
				a_registration,
				kTerrainDebugDepthFallbackFamilies);
	}

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
		std::size_t extendedTranslucencyRows = 0;
		for (const auto& registration : registrations) {
			std::optional<FeatureOffIdentityExpectation> identity;
			const bool directRow = registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfLight;
			if (directRow
				|| registration.targetId
					== cs::engine::ShaderInjectionTarget::kBsdfComposite) {
				identity = FeatureOffIdentityExpectation{
					.key = registration.name,
					.variant = FeatureIdentityVariant::kBase,
					.wetnessShouldDiffer = IsWetnessConsumer(registration),
					.terrainShouldDiffer =
						IsTerrainShadowConsumer(registration)
						|| IsTerrainDebugCompositeConsumer(registration),
					.inverseSquareShouldDiffer =
						IsInverseSquareConsumer(registration),
					.expectTerrainVariant = true,
					.expectInverseSquareVariant = directRow
				};
			}
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{},
				&uniqueRegistrationInputs,
				{},
				std::move(identity));
			if (registration.targetId
					== cs::engine::ShaderInjectionTarget::kBsLighting
				&& registration.stage
					== cs::engine::ShaderStage::kPixel) {
				AddRegistration(
					a_jobs,
					a_root,
					registration,
					{
						{
							cs::engine::shader_injection_defines::
								kExtendedTranslucency,
							"1"
						}
					});
				++extendedTranslucencyRows;
			}
		}
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
		std::array<ShaderCase, 8> terrainSssDebugCases;
		std::array<ShaderCase, 8> wetnessSssDebugCases;
		for (std::size_t index = 0; index < 4; ++index) {
			const auto shape = std::to_string(index + 1);
			terrainSssDebugCases[index * 2] = {
				"BSDFCompositeShader.hlsl",
				{
					{ "FO4CS_SUBSTRATE", "1" },
					{ "TERRAIN_SHADOWS", "1" },
					{ "TERRAIN_SHADOWS_FULLSCREEN_DEBUG", "1" },
					{ "BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL", "1" },
					{ "WAVE5B_SSS_RECORD_NORMAL_SHAPE", shape }
				},
				"ps_5_0"
			};
			terrainSssDebugCases[index * 2 + 1] = {
				"BSDFCompositeShader.hlsl",
				{
					{ "FO4CS_SUBSTRATE", "1" },
					{ "TERRAIN_SHADOWS", "1" },
					{ "TERRAIN_SHADOWS_FULLSCREEN_DEBUG", "1" },
					{ "BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT", "1" },
					{ "WAVE5B_SSS_SURFACE_CONTACT_SHAPE", shape }
				},
				"ps_5_0"
			};
			wetnessSssDebugCases[index * 2] = {
				"BSDFCompositeShader.hlsl",
				{
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" },
					{ "WETNESS_EFFECTS_FULLSCREEN_DEBUG", "1" },
					{ "BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL", "1" },
					{ "WAVE5B_SSS_RECORD_NORMAL_SHAPE", shape }
				},
				"ps_5_0"
			};
			wetnessSssDebugCases[index * 2 + 1] = {
				"BSDFCompositeShader.hlsl",
				{
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" },
					{ "WETNESS_EFFECTS_FULLSCREEN_DEBUG", "1" },
					{ "BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT", "1" },
					{ "WAVE5B_SSS_SURFACE_CONTACT_SHAPE", shape }
				},
				"ps_5_0"
			};
		}
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
		for (const auto& shader : terrainSssDebugCases) {
			auto& job = AddCompile(
				a_jobs,
				a_root / shader.path,
				shader.defines,
				shader.profile,
				shader.entryPoint);
			job.requiredTextureSlots = {
				kTerrainShadowTextureSlot
			};
			job.forbiddenTextureSlots = { kTerrainSceneDepthTextureSlot };
			job.requiredSamplerSlots = { kTerrainShadowSamplerSlot };
		}
		for (const auto& shader : wetnessSssDebugCases) {
			auto& job = AddCompile(
				a_jobs,
				a_root / shader.path,
				shader.defines,
				shader.profile,
				shader.entryPoint);
			job.requiredTextureSlots = { kGbufferNormalTextureSlot };
		}

		using namespace cs::engine::shader_injection_defines;
		const std::array<ShaderDefines, 8> directionalCompositions{ {
			{ { kScreenSpaceShadows, "1" } },
			{ { kTerrainShadows, "1" } },
			{ { kWetnessEffects, "1" } },
			{ { kWaterEffects, "1" } },
			{
				{ kScreenSpaceShadows, "1" },
				{ kTerrainShadows, "1" }
			},
			{
				{ kScreenSpaceShadows, "1" },
				{ kWetnessEffects, "1" }
			},
			{
				{ kTerrainShadows, "1" },
				{ kWaterEffects, "1" }
			},
			{
				{ kScreenSpaceShadows, "1" },
				{ kTerrainShadows, "1" },
				{ kWetnessEffects, "1" },
				{ kWaterEffects, "1" }
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
		const std::array<ShaderDefines, 2> inverseSquareCompositions{ {
			{ { kInverseSquareLighting, "1" } },
			{
				{ kInverseSquareLighting, "1" },
				{ kScreenSpaceShadows, "1" },
				{ kTerrainShadows, "1" },
				{ kWetnessEffects, "1" }
			}
		} };
		std::size_t contributorCompositionCount = 0;
		std::size_t ambientCompositionRows = 0;
		std::size_t ambientNonTargetRows = 0;
		std::size_t wetnessDirectRows = 0;
		std::size_t wetnessDirectInertRows = 0;
		std::size_t terrainDirectRows = 0;
		std::size_t terrainDirectInertRows = 0;
		std::size_t terrainCompositeRows = 0;
		std::size_t terrainCompositeInertRows = 0;
		std::size_t terrainCompositeVertexRows = 0;
		std::size_t wetnessDebugCompositeRows = 0;
		std::size_t wetnessDebugCompositeInertRows = 0;
		std::size_t wetnessDebugCompositeVertexRows = 0;
		std::size_t wetnessCompositeRows = 0;
		std::size_t wetnessCompositeNeutralRows = 0;
		std::size_t wetnessCompositeVertexRows = 0;
		std::size_t inverseSquareRows = 0;
		std::size_t inverseSquareInertRows = 0;
		for (const auto& registration : registrations) {
			if (registration.targetId
				== cs::engine::ShaderInjectionTarget::kBsdfLight) {
				if (IsWetnessDirectConsumer(registration))
					++wetnessDirectRows;
				else
					++wetnessDirectInertRows;
				if (IsTerrainShadowConsumer(registration))
					++terrainDirectRows;
				else
					++terrainDirectInertRows;
				if (IsInverseSquareConsumer(registration))
					++inverseSquareRows;
				else
					++inverseSquareInertRows;

				for (const auto& defines :
					inverseSquareCompositions) {
					const bool consumesTerrain =
						IsTerrainShadowConsumer(registration);
					SlotExpectations slots;
					slots.forbiddenTextures.push_back(
						kGbufferNormalTextureSlot);
					const bool terrainOn =
						HasDefine(defines, kTerrainShadows);
					auto& terrainTextures =
						terrainOn && consumesTerrain ?
							slots.requiredTextures :
							slots.forbiddenTextures;
					terrainTextures.push_back(
						kTerrainShadowTextureSlot);
					slots.forbiddenTextures.push_back(
						kTerrainSceneDepthTextureSlot);
					auto& terrainSamplers =
						terrainOn && consumesTerrain ?
							slots.requiredSamplers :
							slots.forbiddenSamplers;
					terrainSamplers.push_back(
						kTerrainShadowSamplerSlot);
					std::optional<FeatureOffIdentityExpectation>
						identity;
					if (defines.size() == 1) {
						identity = FeatureOffIdentityExpectation{
							.key = registration.name,
							.variant = FeatureIdentityVariant::
								kInverseSquareLighting
						};
					}
					AddRegistration(
						a_jobs,
						a_root,
						registration,
						defines,
						nullptr,
						std::move(slots),
						std::move(identity));
					++contributorCompositionCount;
				}
			}

			const auto* compositions =
				registration.targetId
						== cs::engine::ShaderInjectionTarget::kBsdfLight
				? &directionalCompositions
				: nullptr;
			if (compositions) {
				const bool consumesTerrain = IsTerrainShadowConsumer(registration);
				for (const auto& defines : *compositions) {
					// the authoritative normal belongs to composite rows only
					std::optional<FeatureOffIdentityExpectation> identity;
					if (defines.size() == 1) {
						if (HasDefine(defines, kWetnessEffects)) {
							identity = FeatureOffIdentityExpectation{
								.key = registration.name,
								.variant = FeatureIdentityVariant::kWetness
							};
						} else if (HasDefine(defines, kTerrainShadows)) {
							identity = FeatureOffIdentityExpectation{
								.key = registration.name,
								.variant = FeatureIdentityVariant::kTerrainShadows
							};
						}
					}
					SlotExpectations slots;
					slots.forbiddenTextures.push_back(kGbufferNormalTextureSlot);
					const bool terrainOn = HasDefine(defines, kTerrainShadows);
					auto& terrainTextures = terrainOn && consumesTerrain ?
						slots.requiredTextures :
						slots.forbiddenTextures;
					terrainTextures.push_back(kTerrainShadowTextureSlot);
					slots.forbiddenTextures.push_back(
						kTerrainSceneDepthTextureSlot);
					auto& terrainSamplers = terrainOn && consumesTerrain ?
						slots.requiredSamplers :
						slots.forbiddenSamplers;
					terrainSamplers.push_back(kTerrainShadowSamplerSlot);
					const bool waterOn = HasDefine(defines, kWaterEffects);
					auto& waterTextures = waterOn && consumesTerrain ?
						slots.requiredTextures :
						slots.forbiddenTextures;
					waterTextures.push_back(kWaterCausticsTextureSlot);
					slots.forbiddenTextures.push_back(
						kWaterSceneDepthTextureSlot);
					auto& waterSamplers = waterOn && consumesTerrain ?
						slots.requiredSamplers :
						slots.forbiddenSamplers;
					waterSamplers.push_back(kWaterCausticsSamplerSlot);
					AddRegistration(
						a_jobs,
						a_root,
						registration,
						defines,
						nullptr,
						std::move(slots),
						std::move(identity));
					++contributorCompositionCount;
				}
			}
			if (registration.targetId
				!= cs::engine::ShaderInjectionTarget::kBsdfComposite) {
				continue;
			}

			const bool pixelRow =
				registration.stage == cs::engine::ShaderStage::kPixel;
			const bool composesAmbient = pixelRow
				&& DeclaresFamily(registration, kAmbientCompositionFamilies);
			const bool composesWetness = pixelRow
				&& DeclaresFamily(registration, kWetnessCompositeFamilies);
			const bool consumesTerrainDebug =
				IsTerrainDebugCompositeConsumer(registration);
			const bool consumesWetnessDebug =
				IsWetnessDebugCompositeConsumer(registration);
			if (pixelRow) {
				if (composesAmbient)
					++ambientCompositionRows;
				else
					++ambientNonTargetRows;
				if (composesWetness)
					++wetnessCompositeRows;
				else
					++wetnessCompositeNeutralRows;
				if (consumesTerrainDebug)
					++terrainCompositeRows;
				else
					++terrainCompositeInertRows;
				if (consumesWetnessDebug)
					++wetnessDebugCompositeRows;
				else
					++wetnessDebugCompositeInertRows;
			} else if (registration.stage == cs::engine::ShaderStage::kVertex) {
				++wetnessCompositeVertexRows;
				++terrainCompositeVertexRows;
				++wetnessDebugCompositeVertexRows;
			} else {
				AddPreparationFailure(
					a_jobs,
					"unexpected kBsdfComposite stage",
					"Registration " + registration.name
						+ " is neither pixel nor vertex");
			}

			for (const auto& defines : ambientCompositions) {
				const bool screenSpaceGi = HasDefine(defines, kScreenSpaceGi);
				const bool wetnessEffects = HasDefine(defines, kWetnessEffects);
				SlotExpectations slots;
				if (pixelRow) {
					auto& compositionSlots = screenSpaceGi && composesAmbient ?
						slots.requiredTextures :
						slots.forbiddenTextures;
					compositionSlots.assign(
						kCompositionTextureSlots.begin(),
						kCompositionTextureSlots.end());
				}
				auto& normalSlot = wetnessEffects && composesWetness ?
					slots.requiredTextures :
					slots.forbiddenTextures;
				normalSlot.push_back(kGbufferNormalTextureSlot);
				if (pixelRow
					&& registration.compilation.defines.contains(
						"BSDFCOMPOSITE_PS_2D_ACCUMULATOR")) {
					slots.forbiddenTextures.push_back(7);
				}
				std::optional<FeatureOffIdentityExpectation> identity;
				if (defines.size() == 1 && wetnessEffects) {
					identity = FeatureOffIdentityExpectation{
						.key = registration.name,
						.variant = FeatureIdentityVariant::kWetness
					};
				}
				AddRegistration(
					a_jobs,
					a_root,
					registration,
					defines,
					nullptr,
					std::move(slots),
					std::move(identity));
				++contributorCompositionCount;
			}

			SlotExpectations wetnessDebugSlots;
			auto& wetnessDebugTextures = consumesWetnessDebug ?
				wetnessDebugSlots.requiredTextures :
				wetnessDebugSlots.forbiddenTextures;
			wetnessDebugTextures.push_back(kGbufferNormalTextureSlot);
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{
					{ kWetnessEffects, "1" },
					{ kWetnessEffectsFullscreenDebug, "1" }
				},
				nullptr,
				std::move(wetnessDebugSlots));
			++contributorCompositionCount;

			SlotExpectations terrainSlots;
			auto& terrainTextures = consumesTerrainDebug ?
				terrainSlots.requiredTextures :
				terrainSlots.forbiddenTextures;
			terrainTextures.push_back(kTerrainShadowTextureSlot);
			if (consumesTerrainDebug) {
				auto& terrainDepthTextures =
					UsesTerrainDebugDepthFallback(registration) ?
					terrainSlots.requiredTextures :
					terrainSlots.forbiddenTextures;
				terrainDepthTextures.push_back(
					kTerrainSceneDepthTextureSlot);
				terrainSlots.requiredSamplers.push_back(
					kTerrainShadowSamplerSlot);
			} else {
				terrainSlots.forbiddenTextures.push_back(
					kTerrainSceneDepthTextureSlot);
			}
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{
					{ kTerrainShadows, "1" },
					{ kTerrainShadowsFullscreenDebug, "1" }
				},
				nullptr,
				std::move(terrainSlots),
				FeatureOffIdentityExpectation{
					.key = registration.name,
					.variant = FeatureIdentityVariant::kTerrainShadows
				});
			++contributorCompositionCount;

			SlotExpectations waterSlots;
			auto& waterTextures = consumesTerrainDebug ?
				waterSlots.requiredTextures :
				waterSlots.forbiddenTextures;
			waterTextures.push_back(kWaterCausticsTextureSlot);
			auto& waterDepthTextures =
				consumesTerrainDebug
					&& UsesTerrainDebugDepthFallback(registration) ?
				waterSlots.requiredTextures :
				waterSlots.forbiddenTextures;
			waterDepthTextures.push_back(kWaterSceneDepthTextureSlot);
			AddRegistration(
				a_jobs,
				a_root,
				registration,
				{
					{ kWaterEffects, "1" },
					{ kWaterEffectsFullscreenDebug, "1" }
				},
				nullptr,
				std::move(waterSlots));
			++contributorCompositionCount;
		}
		if (ambientCompositionRows != kExpectedAmbientCompositionRows
			|| ambientNonTargetRows != kExpectedAmbientNonTargetRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite ambient composition coverage",
				"Expected "
					+ std::to_string(kExpectedAmbientCompositionRows)
					+ " composing and "
					+ std::to_string(kExpectedAmbientNonTargetRows)
					+ " non-target pixel rows, found "
					+ std::to_string(ambientCompositionRows)
					+ " and "
					+ std::to_string(ambientNonTargetRows));
		}
		if (wetnessCompositeRows != kExpectedWetnessCompositeRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite wetness coverage",
				"Expected "
					+ std::to_string(kExpectedWetnessCompositeRows)
					+ " wetness pixel rows, found "
					+ std::to_string(wetnessCompositeRows));
		}
		if (wetnessDirectRows != kExpectedWetnessDirectRows
			|| wetnessDirectInertRows != kExpectedWetnessDirectInertRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfLight wetness coverage",
				"Expected "
					+ std::to_string(kExpectedWetnessDirectRows)
					+ " wetness and "
					+ std::to_string(kExpectedWetnessDirectInertRows)
					+ " inert rows, found "
					+ std::to_string(wetnessDirectRows)
					+ " and "
					+ std::to_string(wetnessDirectInertRows));
		}
		if (wetnessCompositeRows != kExpectedWetnessCompositeRows
			|| wetnessCompositeNeutralRows
				!= kExpectedWetnessCompositeNeutralRows
			|| wetnessCompositeVertexRows
				!= kExpectedWetnessCompositeVertexRows
			|| wetnessCompositeRows + wetnessCompositeNeutralRows
					+ wetnessCompositeVertexRows
				!= kExpectedCompositeRegistrationRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite wetness registration partition",
				"Expected "
					+ std::to_string(kExpectedWetnessCompositeRows)
					+ " wetness, "
					+ std::to_string(kExpectedWetnessCompositeNeutralRows)
					+ " neutral pixel, and "
					+ std::to_string(kExpectedWetnessCompositeVertexRows)
					+ " vertex rows");
		}
		if (wetnessDirectRows + wetnessDirectInertRows
			!= kExpectedBsdfLightRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfLight wetness registration partition",
				"Expected "
					+ std::to_string(kExpectedBsdfLightRows)
					+ " total rows, found "
					+ std::to_string(
						wetnessDirectRows + wetnessDirectInertRows));
		}
		if (terrainDirectRows != kExpectedTerrainDirectRows
			|| terrainDirectInertRows != kExpectedTerrainDirectInertRows
			|| terrainDirectRows + terrainDirectInertRows
				!= kExpectedBsdfLightRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfLight terrain shadow coverage",
				"Expected "
					+ std::to_string(kExpectedTerrainDirectRows)
					+ " terrain and "
					+ std::to_string(kExpectedTerrainDirectInertRows)
					+ " inert rows, found "
					+ std::to_string(terrainDirectRows)
					+ " and "
					+ std::to_string(terrainDirectInertRows));
		}
		if (inverseSquareRows != kExpectedInverseSquareRows
			|| inverseSquareInertRows
				!= kExpectedInverseSquareInertRows
			|| inverseSquareRows + inverseSquareInertRows
				!= kExpectedBsdfLightRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfLight inverse-square coverage",
				"Expected "
					+ std::to_string(kExpectedInverseSquareRows)
					+ " punctual and "
					+ std::to_string(kExpectedInverseSquareInertRows)
					+ " inert rows, found "
					+ std::to_string(inverseSquareRows)
					+ " and "
					+ std::to_string(inverseSquareInertRows));
		}
		if (terrainCompositeRows != kExpectedTerrainCompositeRows
			|| terrainCompositeInertRows
				!= kExpectedTerrainCompositeInertRows
			|| terrainCompositeVertexRows
				!= kExpectedWetnessCompositeVertexRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite terrain debug coverage",
				"Expected "
					+ std::to_string(kExpectedTerrainCompositeRows)
					+ " terrain debug, "
					+ std::to_string(kExpectedTerrainCompositeInertRows)
					+ " inert pixel, and "
					+ std::to_string(kExpectedWetnessCompositeVertexRows)
					+ " vertex rows");
		}
		if (wetnessDebugCompositeRows != kExpectedWetnessDebugCompositeRows
			|| wetnessDebugCompositeInertRows
				!= kExpectedWetnessDebugCompositeInertRows
			|| wetnessDebugCompositeVertexRows
				!= kExpectedWetnessCompositeVertexRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsdfComposite wetness debug coverage",
				"Expected "
					+ std::to_string(kExpectedWetnessDebugCompositeRows)
					+ " wetness debug, "
					+ std::to_string(kExpectedWetnessDebugCompositeInertRows)
					+ " inert pixel, and "
					+ std::to_string(kExpectedWetnessCompositeVertexRows)
					+ " vertex rows");
		}
		if (extendedTranslucencyRows
			!= kExpectedExtendedTranslucencyRows) {
			AddPreparationFailure(
				a_jobs,
				"kBsLighting extended translucency coverage",
				"Expected "
					+ std::to_string(kExpectedExtendedTranslucencyRows)
					+ " pixel rows, found "
					+ std::to_string(extendedTranslucencyRows));
		}
		return {
			.registrationDerived = registrations.size(),
			.uniqueRegistrationInputs =
				uniqueRegistrationInputs.size(),
			.explicitPermutations =
				featureCompositionCases.size()
				+ explicitSourceCases.size()
				+ contributorCompositionCount,
			.ambientCompositionRows = ambientCompositionRows,
			.ambientNonTargetRows = ambientNonTargetRows,
			.wetnessDirectRows = wetnessDirectRows,
			.wetnessDirectInertRows = wetnessDirectInertRows,
			.terrainDirectRows = terrainDirectRows,
			.terrainDirectInertRows = terrainDirectInertRows,
			.terrainCompositeRows = terrainCompositeRows,
			.terrainCompositeInertRows = terrainCompositeInertRows,
			.terrainCompositeVertexRows = terrainCompositeVertexRows,
			.wetnessDebugCompositeRows = wetnessDebugCompositeRows,
			.wetnessDebugCompositeInertRows = wetnessDebugCompositeInertRows,
			.wetnessDebugCompositeVertexRows =
				wetnessDebugCompositeVertexRows,
			.wetnessCompositeRows = wetnessCompositeRows,
			.wetnessCompositeNeutralRows = wetnessCompositeNeutralRows,
			.wetnessCompositeVertexRows = wetnessCompositeVertexRows,
			.inverseSquareRows = inverseSquareRows,
			.inverseSquareInertRows = inverseSquareInertRows,
			.extendedTranslucencyRows = extendedTranslucencyRows
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
	const auto menuShaderCount = AddMenuShaders(jobs, argv[1]);
	const auto screenSpaceGiCount = AddScreenSpaceGI(jobs, argv[1]);
	if (screenSpaceGiCount != kScreenSpaceGIPermutations) {
		AddPreparationFailure(
			jobs,
			"ScreenSpaceGI permutation census",
			"expected " + std::to_string(kScreenSpaceGIPermutations)
				+ " permutations, prepared " + std::to_string(screenSpaceGiCount));
	}
	const auto lightingCounts = AddLighting(jobs, argv[1]);
	const auto terrainShadowsCount = AddTerrainShadows(jobs, argv[1]);
	if (terrainShadowsCount != kTerrainShadowsPermutations) {
		AddPreparationFailure(
			jobs,
			"TerrainShadows permutation census",
			"expected " + std::to_string(kTerrainShadowsPermutations)
				+ " permutations, prepared "
				+ std::to_string(terrainShadowsCount));
	}
	const auto vertexCount = AddVertexPermutations(jobs, argv[1]);
	const auto upscalingCount = AddUpscaling(jobs, argv[1]);
	if (upscalingCount != kUpscalingPermutations) {
		AddPreparationFailure(
			jobs,
			"Upscaling permutation census",
			"expected " + std::to_string(kUpscalingPermutations)
				+ " permutations, prepared " + std::to_string(upscalingCount));
	}

	std::printf(
		"ShaderCompile checked %zu shared substrate probes\n",
		sharedDataCount);
	std::printf(
		"ShaderCompile checked %zu menu shader entry points\n",
		menuShaderCount);
	std::printf(
		"ShaderCompile checked %zu ScreenSpaceGI permutations\n",
		screenSpaceGiCount);
	std::printf(
		"ShaderCompile checked %zu base registration permutations (%zu unique inputs) and %zu lighting explicit/composed permutations\n",
		lightingCounts.registrationDerived,
		lightingCounts.uniqueRegistrationInputs,
		lightingCounts.explicitPermutations);
	std::printf(
		"ShaderCompile witnessed t26-t29 on %zu composing and %zu non-target kBsdfComposite pixel rows\n",
		lightingCounts.ambientCompositionRows,
		lightingCounts.ambientNonTargetRows);
	std::printf(
		"ShaderCompile witnessed t30+t31/s13 on %zu terrain shadow and %zu inert kBsdfLight rows\n",
		lightingCounts.terrainDirectRows,
		lightingCounts.terrainDirectInertRows);
	std::printf(
		"ShaderCompile checked inverse-square on %zu punctual and %zu inert kBsdfLight rows\n",
		lightingCounts.inverseSquareRows,
		lightingCounts.inverseSquareInertRows);
	std::printf(
		"ShaderCompile checked extended translucency on %zu kBsLighting rows\n",
		lightingCounts.extendedTranslucencyRows);
	std::printf(
		"ShaderCompile checked %zu TerrainShadows permutations\n",
		terrainShadowsCount);
	std::printf(
		"ShaderCompile checked %zu vertex permutations\n",
		vertexCount);
	std::printf(
		"ShaderCompile checked %zu Upscaling permutations\n",
		upscalingCount);

	const int failures = CompileAll(jobs);

	if (failures == 0)
		std::printf("ShaderCompile passed\n");
	else
		std::printf("%d shader(s) failed to compile\n", failures);

	return failures ? 1 : 0;
}
