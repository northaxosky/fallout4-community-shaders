#include "CSSha1.h"
#include "FeatureShaderDeclarations.h"
#include "Log.h"
#include "ParallelFor.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderVariantRecipe.h"
#include "Render/StockShaderTable.h"
#include "ShaderABIChecks.h"
#include "ShaderTestCache.h"
#include "Utils/CSSha256.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace cs::log
{
	spdlog::logger* Get(const char*)
	{
		return spdlog::default_logger_raw();
	}
}

namespace
{
	struct Row
	{
		cs::engine::StockShaderRow source;
		std::string actual;
		bool owned = false;
		bool contributed = false;
	};

	void Require(bool a_condition, const std::string& a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	bool IsHex(std::string_view a_text, std::size_t a_length)
	{
		return a_text.size() == a_length && a_text.find_first_not_of("0123456789abcdef") == a_text.npos;
	}

	std::filesystem::path ModulePath(HMODULE a_module)
	{
		wchar_t path[32768];
		const auto length = GetModuleFileNameW(a_module, path, static_cast<DWORD>(std::size(path)));
		Require(length != 0 && length < std::size(path), "Cannot read module path");
		return std::filesystem::path(path, path + length);
	}

	// Output identity is the real verdict; an uncalibrated compiler can only fail rows, never pass them.
	void ReportCompiler(const std::string& a_calibrated)
	{
		const auto module = GetModuleHandleW(L"d3dcompiler_47.dll");
		Require(module != nullptr, "d3dcompiler_47.dll is not loaded");
		const auto loaded = ModulePath(module);
		cs::sha256::Sha256Result digest;
		std::uint64_t length = 0;
		Require(cs::sha256::Sha256ComputeFile(loaded, digest, length), "Cannot hash compiler: " + loaded.string());
		const auto actual = cs::sha256::Sha256ToHex(digest);
		std::printf("compiler %s sha256=%s%s\n", loaded.string().c_str(), actual.c_str(),
			actual == a_calibrated ? " (calibrated)" : " (UNCALIBRATED)");
	}

	struct Table
	{
		std::vector<Row> rows;
		cs::engine::GameRuntime runtime = cs::engine::GameRuntime::kAE;
		bool inheritsTargets = false;
	};

	Table ReadRows(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path, std::ios::binary);
		std::ostringstream text;
		text << file.rdbuf();
		auto parsed = cs::engine::ParseStockShaderTable(text.str());
		const auto& compiler = parsed.header.at("compiler_sha256");
		Require(IsHex(compiler, 64), "Invalid compiler pin");
		ReportCompiler(compiler);
		std::printf("runtime %s\n", parsed.header.at("runtime").c_str());
		Table table{ .runtime = parsed.runtime, .inheritsTargets = parsed.inheritsTargets };
		for (auto& source : parsed.rows)
			table.rows.push_back({ .source = std::move(source) });
		return table;
	}

	void ResolveOwnership(std::vector<Row>& a_rows, bool a_inheritsTargets, const std::filesystem::path& a_shaderRoot)
	{
		using namespace cs::engine;
		Require(std::filesystem::is_directory(a_shaderRoot), "Shader root is not a directory: " + a_shaderRoot.string());
		for (auto& row : a_rows) {
			row.owned = IsShaderSourceAvailable(a_shaderRoot, ResolveShaderSourceName(row.source.Family()));
			row.contributed = row.owned && std::ranges::any_of(GetFeatureShaderContributions(), [&](const auto& contribution) {
				return contribution.targetId == row.source.target;
			});
		}
		for (const auto& contribution : GetFeatureShaderContributions()) {
			// Inherited targets keep their AE identity and are covered by the AE table.
			if (a_inheritsTargets && std::ranges::none_of(a_rows, [&](const Row& row) { return row.source.target == contribution.targetId; }))
				continue;
			for (const auto stage : { ShaderStage::kVertex, ShaderStage::kPixel, ShaderStage::kCompute }) {
				if ((contribution.stages & ShaderStageBit(stage)) == 0)
					continue;
				Require(std::ranges::any_of(a_rows, [&](const Row& row) {
					return row.owned && row.source.target == contribution.targetId && row.source.stage == stage;
				}),
					"Feature contribution has no gated row: " + contribution.contributor + " / " + std::string(GetShaderInjectionTarget(contribution.targetId)->name));
			}
		}
	}

	std::string FirstErrorLine(std::string_view a_error)
	{
		const auto error = a_error.find(": error ");
		if (error != a_error.npos) {
			const auto line = a_error.rfind('\n', error);
			a_error.remove_prefix(line == a_error.npos ? 0 : line + 1);
		}
		const auto start = a_error.find_first_not_of("\r\n");
		if (start == a_error.npos)
			return "no compiler diagnostic";
		a_error.remove_prefix(start);
		return std::string(a_error.substr(0, std::min(a_error.find_first_of("\r\n"), std::size_t{ 300 })));
	}

	using Contributions = std::span<const cs::engine::ShaderReplacementRegistration>;

	Contributions AllContributions(bool a_featuresOn)
	{
		return a_featuresOn ? Contributions(cs::engine::GetFeatureShaderContributions()) : Contributions{};
	}

	auto CompileStage(const cs::engine::ShaderFamilyDescriptor& a_descriptor, const std::filesystem::path& a_shaderRoot,
		Contributions a_contributions, cs::engine::GameRuntime a_runtime)
	{
		const auto family = cs::engine::BuildShaderFamilyCompilationDescriptor(a_descriptor);
		Require(family.has_value(), "unresolved");
		std::string error;
		const auto contributions = a_contributions;
		const auto request = cs::engine::BuildEffectiveShaderCompileRequest(
			*cs::engine::GetShaderInjectionTarget(a_descriptor.target), a_descriptor.stage, *family, contributions, a_runtime, &error);
		Require(request.has_value(), "invalid-compile-request: " + error);
		cs::engine::ShaderVariantCompilationRequest variant;
		variant.sourcePath = a_shaderRoot / request->sourcePath;
		variant.entryPoint = request->entryPoint;
		variant.profile = request->profile;
		variant.stage = a_descriptor.stage;
		variant.defines.assign(request->defines.begin(), request->defines.end());
		const auto recipe = cs::engine::BuildShaderVariantRecipe(variant, a_shaderRoot);
		auto compiled = cs::shader_test::Compile(recipe);
		Require(compiled.succeeded, "compile-error: " + FirstErrorLine(compiled.error));
		return compiled;
	}

	auto ReflectSignature(const auto& a_bytecode, bool a_input)
	{
		winrt::com_ptr<ID3D11ShaderReflection> reflection;
		Require(SUCCEEDED(D3DReflect(a_bytecode.data(), a_bytecode.size(), IID_PPV_ARGS(reflection.put()))), "D3DReflect failed");
		D3D11_SHADER_DESC desc{};
		Require(SUCCEEDED(reflection->GetDesc(&desc)), "Signature description failed");
		std::vector<std::tuple<UINT, std::string, UINT, BYTE>> signature;
		for (UINT i = 0; i < (a_input ? desc.InputParameters : desc.OutputParameters); ++i) {
			D3D11_SIGNATURE_PARAMETER_DESC parameter{};
			Require(SUCCEEDED(a_input ? reflection->GetInputParameterDesc(i, &parameter) : reflection->GetOutputParameterDesc(i, &parameter)),
				"Signature parameter failed");
			if (a_input && parameter.SystemValueType == D3D_NAME_IS_FRONT_FACE)
				continue;
			signature.emplace_back(parameter.Register, parameter.SemanticName, parameter.SemanticIndex, parameter.Mask);
		}
		return signature;
	}

	std::string Compile(const Row& a_row, const std::filesystem::path& a_shaderRoot, bool a_featuresOn, cs::engine::GameRuntime a_runtime)
	{
		const auto family = a_row.source.Family();
		const auto compiled = CompileStage(family, a_shaderRoot, AllContributions(a_featuresOn), a_runtime);
		if (a_featuresOn) {
			if (family.stage != cs::engine::ShaderStage::kCompute &&
				std::ranges::any_of(cs::engine::GetFeatureShaderContributions(), [&](const auto& contribution) {
					return contribution.targetId == family.target && contribution.requiresGraphicsPair;
				})) {
				const bool isVertex = family.stage == cs::engine::ShaderStage::kVertex;
				auto paired = family;
				paired.stage = isVertex ? cs::engine::ShaderStage::kPixel : cs::engine::ShaderStage::kVertex;
				// Tessellated prepass variants use the native domain stage.
				if (cs::engine::BuildShaderFamilyCompilationDescriptor(family)->defines.contains("TESSELLATE_DISP_HEIGHT"))
					return {};
				const auto pairedCompiled = CompileStage(paired, a_shaderRoot, AllContributions(true), a_runtime);
				const auto outputs = ReflectSignature(isVertex ? compiled.bytecode : pairedCompiled.bytecode, false);
				for (const auto& input : ReflectSignature(isVertex ? pairedCompiled.bytecode : compiled.bytecode, true)) {
					Require(std::ranges::find(outputs, input) != outputs.end(),
						"Graphics pair signature mismatch: " + std::get<1>(input) + std::to_string(std::get<2>(input)) +
							" at register " + std::to_string(std::get<0>(input)));
				}
			}
			return {};
		}
		winrt::com_ptr<ID3DBlob> stripped;
		if (FAILED(D3DStripShader(compiled.bytecode.data(), compiled.bytecode.size(),
				D3DCOMPILER_STRIP_REFLECTION_DATA, stripped.put())))
			return "strip-error";
		return cs::sha1::Sha1ToHex(cs::sha1::Sha1Compute(stripped->GetBufferPointer(), stripped->GetBufferSize()));
	}

	// Features load alone, so each must compile without others' includes.
	std::string CompileIsolated(const Row& a_row, const std::filesystem::path& a_shaderRoot, cs::engine::GameRuntime a_runtime)
	{
		const auto family = a_row.source.Family();
		for (const auto& contribution : cs::engine::GetFeatureShaderContributions()) {
			if (contribution.targetId != family.target ||
				(contribution.stages & cs::engine::ShaderStageBit(family.stage)) == 0)
				continue;
			try {
				CompileStage(family, a_shaderRoot, Contributions(&contribution, 1), a_runtime);
			} catch (const std::exception& error) {
				return contribution.contributor + ": " + FirstErrorLine(error.what());
			}
		}
		return {};
	}
}

int main(int a_argc, char** a_argv)
{
	try {
		const std::string_view mode = a_argc == 4 ? a_argv[3] : "";
		Require(a_argc == 3 || (a_argc == 4 && (mode == "--features-on" || mode == "--features-isolated")),
			"Usage: StockShaderIdentityTests <shader-root> <identity-table> [--features-on|--features-isolated]");
		const bool isolated = mode == "--features-isolated";
		const bool featuresOn = a_argc == 4;
		cs::sha1::Sha1InitOnce();
		cs::sha256::Sha256InitOnce();
		auto table = ReadRows(a_argv[2]);
		auto& rows = table.rows;
		const std::filesystem::path shaderRoot(a_argv[1]);
		if (featuresOn) {
			const auto abiError = VerifyShaderABI(shaderRoot);
			Require(abiError.empty(), "Shader ABI: " + abiError);
		}
		ResolveOwnership(rows, table.inheritsTargets, shaderRoot);
		const auto total = featuresOn ?
		                       static_cast<std::size_t>(std::ranges::count(rows, true, &Row::contributed)) :
		                       rows.size();
		Require(total != 0, "No contributor targets in the identity corpus");
		const auto start = std::chrono::steady_clock::now();
		ParallelFor(rows.size(), [&](std::size_t index) {
			if (!rows[index].owned || (featuresOn && !rows[index].contributed))
				return;
			try {
				rows[index].actual = isolated ? CompileIsolated(rows[index], shaderRoot, table.runtime) :
				                                Compile(rows[index], shaderRoot, featuresOn, table.runtime);
			} catch (const std::exception& error) {
				rows[index].actual = "error: " + FirstErrorLine(error.what());
			}
		});
		const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::size_t failed = 0;
		std::size_t unowned = 0;
		std::map<std::tuple<std::string, std::string, std::string>, std::pair<const Row*, std::size_t>> featureFailures;
		std::map<std::string, std::pair<std::size_t, std::size_t>> targetResults;
		for (const auto& row : rows) {
			if (featuresOn && !row.contributed)
				continue;
			if (!row.owned) {
				++unowned;
				continue;
			}
			const bool passed = featuresOn ? row.actual.empty() : row.actual == row.source.expected;
			++(passed ? targetResults[row.source.targetName].first : targetResults[row.source.targetName].second);
			if (passed)
				continue;
			++failed;
			if (featuresOn) {
				auto& group = featureFailures[{ row.source.targetName, cs::engine::ProfileForStage(row.source.stage), row.actual }];
				group.first = group.first ? group.first : &row;
				++group.second;
			} else if (failed <= 50) {
				std::printf("%s %s %s 0x%08x ordinal=%u expected=%s actual=%s\n",
					row.source.targetName.c_str(), row.source.nativeName.c_str(), cs::engine::ProfileForStage(row.source.stage).c_str(), row.source.descriptor, row.source.ordinal, row.source.expected.c_str(), row.actual.c_str());
			}
		}
		for (const auto& [key, group] : featureFailures) {
			const auto& row = *group.first;
			std::printf("%s %s first=0x%08x ordinal=%u (%zu variants): %s\n",
				row.source.targetName.c_str(), cs::engine::ProfileForStage(row.source.stage).c_str(), row.source.descriptor, row.source.ordinal, group.second, row.actual.c_str());
		}
		if (!featuresOn && failed > 50)
			std::printf("... %zu additional failures omitted\n", failed - 50);
		for (const auto& [target, result] : targetResults)
			std::printf("  %s: %zu passed, %zu failed\n", target.c_str(), result.first, result.second);
		cs::shader_test::ReportCache();
		std::printf("%s: %zu passed, %zu failed, %zu unowned, %zu total; %.3fs wall time (%u threads)\n",
			isolated ? "Isolated feature shader corpus" : featuresOn ? "Feature-on shader corpus" :
																	   "Stock shader identity",
			total - failed - unowned, failed, unowned, total, seconds, ParallelThreadCount());
		return failed == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Stock shader identity: %s\n", error.what());
		return 1;
	}
}
