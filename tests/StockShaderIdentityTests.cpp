#include "FeatureShaderDeclarations.h"
#include "Log.h"
#include "ParallelFor.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderVariantRecipe.h"
#include "ShaderABIChecks.h"
#include "Utils/CSSha1.h"
#include "Utils/CSSha256.h"
#include "Utils/ShaderCache/SourceCompile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
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
		cs::engine::ShaderFamilyDescriptor family;
		std::string target;
		std::string stage;
		std::string expected;
		std::string actual;
		std::uint32_t ordinal = 0;
		std::string nativeName;
		std::string nativeClassName;
		std::string nativeSourceGroup;
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

	cs::engine::GameRuntime ParseRuntime(std::string_view a_build)
	{
		using cs::engine::GameRuntime;
		for (const auto [prefix, runtime] : { std::pair{ "OG-", GameRuntime::kOG }, std::pair{ "NG-", GameRuntime::kNG },
				 std::pair{ "AE-", GameRuntime::kAE } }) {
			if (a_build.starts_with(prefix))
				return runtime;
		}
		throw std::runtime_error("Unknown identity table runtime: " + std::string(a_build));
	}

	Table ReadRows(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path);
		std::string line;
		Require(static_cast<bool>(std::getline(file, line)) && line.starts_with("# "), "Missing identity table header");
		std::istringstream header(line.substr(2));
		std::map<std::string, std::string> fields;
		for (std::string token; header >> token;) {
			const auto separator = token.find('=');
			Require(separator != token.npos && fields.emplace(token.substr(0, separator), token.substr(separator + 1)).second,
				"Invalid identity table header");
		}
		Require(IsHex(fields.at("compiler_sha256"), 64), "Invalid compiler pin");
		ReportCompiler(fields.at("compiler_sha256"));
		Table table{ .runtime = ParseRuntime(fields.at("runtime")),
			.inheritsTargets = std::stoull(fields.at("excluded_inherited")) != 0 };
		std::printf("runtime %s\n", fields.at("runtime").c_str());
		auto& rows = table.rows;
		std::set<std::tuple<std::string, std::string, std::uint32_t, std::uint32_t>> keys;
		while (std::getline(file, line)) {
			Row row;
			unsigned early = 0;
			unsigned macroCount = 0;
			std::istringstream input(line);
			Require(static_cast<bool>(input >> row.target >> row.stage >> std::hex >> row.family.descriptor >> std::dec >> early >> row.expected >> row.ordinal >>
									  std::quoted(row.nativeName) >> std::quoted(row.nativeClassName) >> std::quoted(row.nativeSourceGroup) >> macroCount),
				"Malformed identity row: " + line);
			for (unsigned i = 0; i < macroCount; ++i) {
				std::string name;
				std::string value;
				Require(static_cast<bool>(input >> std::quoted(name) >> std::quoted(value)) &&
							row.family.nativeMacros.emplace(name, value).second,
					"Invalid native macros: " + line);
			}
			input >> std::ws;
			Require(input.eof() && early <= 1 && IsHex(row.expected, 40), "Invalid identity row: " + line);
			for (const auto& target : cs::engine::GetShaderInjectionTargets()) {
				if (target.name == row.target)
					row.family.target = target.id;
			}
			if (row.stage == "vertex")
				row.family.stage = cs::engine::ShaderStage::kVertex;
			else if (row.stage == "pixel")
				row.family.stage = cs::engine::ShaderStage::kPixel;
			else if (row.stage == "compute")
				row.family.stage = cs::engine::ShaderStage::kCompute;
			else
				throw std::runtime_error("Unknown stage: " + row.stage);
			row.family.forceEarlyDepthStencil = early != 0;
			Require(keys.emplace(row.target, row.stage, row.family.descriptor, row.ordinal).second, "Duplicate identity row: " + line);
			rows.push_back(std::move(row));
		}
		Require(file.eof() && !rows.empty() && rows.size() == std::stoull(fields.at("rows")), "Identity row count mismatch");
		Require(static_cast<std::size_t>(std::ranges::count_if(rows, [](const auto& row) { return row.nativeName.empty(); })) ==
					std::stoull(fields.at("rows_unnamed")),
			"Unnamed identity row count mismatch");
		return table;
	}

	void ResolveOwnership(std::vector<Row>& a_rows, bool a_inheritsTargets, const std::filesystem::path& a_shaderRoot)
	{
		using namespace cs::engine;
		Require(std::filesystem::is_directory(a_shaderRoot), "Shader root is not a directory: " + a_shaderRoot.string());
		for (auto& row : a_rows) {
			row.family.nativeName = row.nativeName;
			row.family.nativeClassName = row.nativeClassName;
			row.family.nativeSourceGroup = row.nativeSourceGroup;
			row.owned = IsShaderSourceAvailable(a_shaderRoot, row.family.nativeName);
			row.contributed = row.owned && std::ranges::any_of(GetFeatureShaderContributions(), [&](const auto& contribution) {
				return contribution.targetId == row.family.target;
			});
		}
		for (const auto& contribution : GetFeatureShaderContributions()) {
			// Inherited targets keep their AE identity and are covered by the AE table.
			if (a_inheritsTargets && std::ranges::none_of(a_rows, [&](const Row& row) { return row.family.target == contribution.targetId; }))
				continue;
			for (const auto stage : { ShaderStage::kVertex, ShaderStage::kPixel, ShaderStage::kCompute }) {
				if ((contribution.stages & ShaderStageBit(stage)) == 0)
					continue;
				Require(std::ranges::any_of(a_rows, [&](const Row& row) {
					return row.owned && row.family.target == contribution.targetId && row.family.stage == stage;
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

	auto CompileStage(const cs::engine::ShaderFamilyDescriptor& a_descriptor, const std::filesystem::path& a_shaderRoot, bool a_featuresOn,
		cs::engine::GameRuntime a_runtime)
	{
		const auto family = cs::engine::BuildShaderFamilyCompilationDescriptor(a_descriptor);
		Require(family.has_value(), "unresolved");
		std::string error;
		const auto contributions = a_featuresOn ?
		                               std::span<const cs::engine::ShaderReplacementRegistration>(cs::engine::GetFeatureShaderContributions()) :
		                               std::span<const cs::engine::ShaderReplacementRegistration>{};
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
		auto compiled = cs::shader_cache::CompileSourceWithManifest(recipe);
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
		const auto compiled = CompileStage(a_row.family, a_shaderRoot, a_featuresOn, a_runtime);
		if (a_featuresOn) {
			if (a_row.family.stage != cs::engine::ShaderStage::kCompute &&
				std::ranges::any_of(cs::engine::GetFeatureShaderContributions(), [&](const auto& contribution) {
					return contribution.targetId == a_row.family.target && contribution.requiresGraphicsPair;
				})) {
				const bool isVertex = a_row.family.stage == cs::engine::ShaderStage::kVertex;
				auto paired = a_row.family;
				paired.stage = isVertex ? cs::engine::ShaderStage::kPixel : cs::engine::ShaderStage::kVertex;
				// Tessellated prepass variants use the native domain stage.
				if (cs::engine::BuildShaderFamilyCompilationDescriptor(a_row.family)->defines.contains("TESSELLATE_DISP_HEIGHT"))
					return {};
				const auto pairedCompiled = CompileStage(paired, a_shaderRoot, true, a_runtime);
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
}

int main(int a_argc, char** a_argv)
{
	try {
		Require(a_argc == 3 || (a_argc == 4 && std::string_view(a_argv[3]) == "--features-on"),
			"Usage: StockShaderIdentityTests <shader-root> <identity-table> [--features-on]");
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
				rows[index].actual = Compile(rows[index], shaderRoot, featuresOn, table.runtime);
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
			const bool passed = featuresOn ? row.actual.empty() : row.actual == row.expected;
			++(passed ? targetResults[row.target].first : targetResults[row.target].second);
			if (passed)
				continue;
			++failed;
			if (featuresOn) {
				auto& group = featureFailures[{ row.target, row.stage, row.actual }];
				group.first = group.first ? group.first : &row;
				++group.second;
			} else if (failed <= 50) {
				std::printf("%s %s %s 0x%08x ordinal=%u expected=%s actual=%s\n",
					row.target.c_str(), row.nativeName.c_str(), row.stage.c_str(), row.family.descriptor, row.ordinal, row.expected.c_str(), row.actual.c_str());
			}
		}
		for (const auto& [key, group] : featureFailures) {
			const auto& row = *group.first;
			std::printf("%s %s first=0x%08x ordinal=%u (%zu variants): %s\n",
				row.target.c_str(), row.stage.c_str(), row.family.descriptor, row.ordinal, group.second, row.actual.c_str());
		}
		if (!featuresOn && failed > 50)
			std::printf("... %zu additional failures omitted\n", failed - 50);
		for (const auto& [target, result] : targetResults)
			std::printf("  %s: %zu passed, %zu failed\n", target.c_str(), result.first, result.second);
		std::printf("%s: %zu passed, %zu failed, %zu unowned, %zu total; %.3fs wall time (%u threads)\n",
			featuresOn ? "Feature-on shader corpus" : "Stock shader identity",
			total - failed - unowned, failed, unowned, total, seconds, ParallelThreadCount());
		return failed == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Stock shader identity: %s\n", error.what());
		return 1;
	}
}
