#include "Log.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderVariantRecipe.h"
#include "Utils/CSSha1.h"
#include "Utils/CSSha256.h"
#include "Utils/ShaderCache/SourceCompile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
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

	std::vector<Row> ReadRows(const std::filesystem::path& a_path)
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
		std::vector<Row> rows;
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
		return rows;
	}

	void ResolveOwnership(std::vector<Row>& a_rows, const std::filesystem::path& a_shaderRoot)
	{
		Require(std::filesystem::is_directory(a_shaderRoot), "Shader root is not a directory: " + a_shaderRoot.string());
		for (auto& row : a_rows) {
			row.family.nativeName = row.nativeName;
			row.family.nativeClassName = row.nativeClassName;
			row.family.nativeSourceGroup = row.nativeSourceGroup;
			row.owned = cs::engine::IsShaderSourceAvailable(a_shaderRoot, row.family.nativeName);
		}
	}

	std::string Compile(const Row& a_row, const std::filesystem::path& a_shaderRoot)
	{
		const auto family = cs::engine::BuildShaderFamilyCompilationDescriptor(a_row.family);
		if (!family)
			return "unresolved";
		const auto request = cs::engine::BuildEffectiveShaderCompileRequest(
			*cs::engine::GetShaderInjectionTarget(a_row.family.target), a_row.family.stage, *family, {});
		if (!request)
			return "invalid-compile-request";
		cs::engine::ShaderVariantCompilationRequest variant;
		variant.sourcePath = a_shaderRoot / request->sourcePath;
		variant.entryPoint = request->entryPoint;
		variant.profile = request->profile;
		variant.stage = a_row.family.stage;
		variant.defines.assign(request->defines.begin(), request->defines.end());
		const auto compiled = cs::shader_cache::CompileSourceWithManifest(
			cs::engine::BuildShaderVariantRecipe(variant));
		if (!compiled.succeeded)
			return "compile-error: " + compiled.error.substr(0, 240);
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
		Require(a_argc == 3, "Usage: StockShaderIdentityTests <shader-root> <identity-table>");
		cs::sha1::Sha1InitOnce();
		cs::sha256::Sha256InitOnce();
		auto rows = ReadRows(a_argv[2]);
		const std::filesystem::path shaderRoot(a_argv[1]);
		ResolveOwnership(rows, shaderRoot);
		const auto start = std::chrono::steady_clock::now();
		const auto threadCount = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
		std::atomic_size_t next = 0;
		{
			std::vector<std::jthread> workers;
			for (unsigned i = 0; i < threadCount; ++i) {
				workers.emplace_back([&] {
					for (auto index = next.fetch_add(1); index < rows.size(); index = next.fetch_add(1)) {
						if (!rows[index].owned)
							continue;
						try {
							rows[index].actual = Compile(rows[index], shaderRoot);
						} catch (const std::exception& error) {
							rows[index].actual = "error: " + std::string(error.what()).substr(0, 240);
						}
					}
				});
			}
		}
		const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::size_t failed = 0;
		std::size_t unowned = 0;
		for (const auto& row : rows) {
			if (!row.owned) {
				++unowned;
				continue;
			}
			if (row.actual == row.expected)
				continue;
			if (++failed <= 50)
				std::printf("%s %s %s 0x%08x ordinal=%u expected=%s actual=%s\n",
					row.target.c_str(), row.nativeName.c_str(), row.stage.c_str(), row.family.descriptor, row.ordinal, row.expected.c_str(), row.actual.c_str());
		}
		if (failed > 50)
			std::printf("... %zu additional failures omitted\n", failed - 50);
		std::printf("Stock shader identity: %zu passed, %zu failed, %zu unowned, %zu total; %.3fs wall time (%u threads)\n",
			rows.size() - failed - unowned, failed, unowned, rows.size(), seconds, threadCount);
		return failed == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Stock shader identity: %s\n", error.what());
		return 1;
	}
}
