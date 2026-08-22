#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Render/ShaderVariantCompilation.h"
#include "Utils/ShaderCache/RevalidationContext.h"
#include "Utils/ShaderCache/ShaderCache.h"
#include "Utils/ShaderCompile.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
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

	bool EnsurePreSunLightDrawInstalled()
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
	using Clock = std::chrono::steady_clock;
	using Bytes = std::vector<std::uint8_t>;

	int g_failures = 0;

	void Fail(const std::string& a_what)
	{
		std::printf("FAIL: %s\n", a_what.c_str());
		++g_failures;
	}

	struct Registration
	{
		std::string                    name;
		cs::shader_cache::ShaderRecipe recipe;
	};

	std::vector<Registration> BuildRegistrations(const std::filesystem::path& a_root)
	{
		std::vector<Registration> registrations;
		for (const auto& variant : cs::engine::GetDefaultShaderReplacementVariants()) {
			const auto* target = cs::engine::GetShaderInjectionTarget(variant.targetId);
			if (!target) {
				Fail("registration target metadata is missing for " + variant.name);
				continue;
			}

			std::string error;
			const auto  request = cs::engine::BuildEffectiveShaderCompileRequest(
				*target,
				variant,
				{},
				&error);
			if (!request) {
				Fail("effective compile request for " + variant.name + ": " + error);
				continue;
			}

			cs::shader_cache::ShaderRecipe recipe;
			recipe.source = a_root / request->sourcePath;
			recipe.includeRoots.push_back(recipe.source.parent_path());
			for (const auto& [name, value] : request->defines)
				recipe.defines.emplace_back(name, value);
			recipe.entryPoint = request->entryPoint;
			recipe.profile    = request->profile;
			recipe.stage      = variant.stage == cs::engine::ShaderStage::kVertex
				? cs::shader_cache::ShaderCacheStage::kVertex
				: cs::shader_cache::ShaderCacheStage::kPixel;
			registrations.push_back({ variant.name, std::move(recipe) });
		}
		return registrations;
	}

	Bytes CompileWithOracle(const Registration& a_registration)
	{
		std::vector<std::pair<const char*, const char*>> defines;
		defines.reserve(a_registration.recipe.defines.size());
		for (const auto& [name, value] : a_registration.recipe.defines)
			defines.emplace_back(name.c_str(), value.c_str());

		std::string error;
		const auto  blob = cs::util::CompileShaderToBlob(
			a_registration.recipe.source.c_str(),
			defines,
			a_registration.recipe.profile.c_str(),
			a_registration.recipe.entryPoint.c_str(),
			&error);
		if (!blob) {
			Fail("oracle compile of " + a_registration.name + ": " + error);
			return {};
		}
		const auto* first = static_cast<const std::uint8_t*>(blob->GetBufferPointer());
		return { first, first + blob->GetBufferSize() };
	}

	struct Pass
	{
		std::vector<Bytes>        bytecode;
		std::chrono::milliseconds elapsed{ 0 };
		std::size_t               accepted = 0;
	};

	enum class Phase
	{
		kCold,
		kWarm
	};

	Pass RunOracle(const std::vector<Registration>& a_registrations)
	{
		Pass pass;
		pass.bytecode.reserve(a_registrations.size());
		const auto start = Clock::now();
		for (const auto& registration : a_registrations)
			pass.bytecode.push_back(CompileWithOracle(registration));
		pass.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
		for (const auto& produced : pass.bytecode)
			pass.accepted += produced.empty() ? 0 : 1;
		return pass;
	}

	Pass RunCache(
		const std::vector<Registration>&            a_registrations,
		const cs::shader_cache::ShaderCacheOptions& a_options,
		Phase                                       a_phase)
	{
		using namespace cs::shader_cache;

		const bool warm  = a_phase == Phase::kWarm;
		const auto label = warm ? "warm" : "cold";

		Pass pass;
		pass.bytecode.reserve(a_registrations.size());
		const auto start = Clock::now();
		for (const auto& registration : a_registrations) {
			auto outcome = LoadOrCompileShader(registration.recipe, a_options);
			if (!outcome.succeeded) {
				Fail(std::string(label) + " compile of " + registration.name + ": "
					+ outcome.error);
				pass.bytecode.emplace_back();
				continue;
			}

			if (warm && outcome.disposition != CacheDisposition::kHit) {
				Fail("warm lookup of " + registration.name + " missed ("
					+ DescribeDisposition(outcome.disposition) + ": " + outcome.cacheNote + ")");
			} else if (!warm && outcome.origin != CompileOrigin::kFreshCompile) {
				Fail("cold lookup of " + registration.name + " was served from a record");
			} else if (!warm && !outcome.recordWritten) {
				Fail("cold compile of " + registration.name + " published nothing: "
					+ outcome.cacheNote);
			} else {
				++pass.accepted;
			}
			pass.bytecode.push_back(std::move(outcome.bytecode));
		}
		pass.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
		return pass;
	}

	std::size_t CountIdentical(const Pass& a_pass, const Pass& a_reference)
	{
		std::size_t identical = 0;
		for (std::size_t index = 0; index < a_pass.bytecode.size(); ++index) {
			if (!a_pass.bytecode[index].empty()
				&& a_pass.bytecode[index] == a_reference.bytecode[index]) {
				++identical;
			}
		}
		return identical;
	}

	void ReportDifferences(
		const std::vector<Registration>& a_registrations,
		const Pass&                      a_pass,
		const Pass&                      a_reference,
		const char*                      a_label)
	{
		for (std::size_t index = 0; index < a_registrations.size(); ++index) {
			if (a_pass.bytecode[index] == a_reference.bytecode[index])
				continue;
			Fail(std::string(a_label) + " bytecode differs from the oracle for "
				+ a_registrations[index].name);
		}
	}
}

int main(int argc, char** argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "Usage: ShaderCacheBenchmarkTests <lighting shader directory>\n");
		return 2;
	}

	const auto registrations = BuildRegistrations(argv[1]);
	if (registrations.empty()) {
		std::printf("FAIL: no registrations to benchmark\n");
		return 1;
	}

	cs::shader_cache::ShaderCacheOptions cold;
	cold.cacheRoot = std::filesystem::temp_directory_path()
		/ ("fo4cs-shader-cache-benchmark-" + std::to_string(GetCurrentProcessId()));
	std::error_code cleanupError;
	std::filesystem::remove_all(cold.cacheRoot, cleanupError);

	// one snapshot and one read per dependency for the warm pass
	cs::shader_cache::RevalidationContext memo;
	auto                                  warm = cold;
	warm.revalidation                          = &memo;

	const auto oraclePass = RunOracle(registrations);
	const auto coldPass   = RunCache(registrations, cold, Phase::kCold);
	const auto warmPass   = RunCache(registrations, warm, Phase::kWarm);

	std::filesystem::remove_all(cold.cacheRoot, cleanupError);

	const auto total           = registrations.size();
	const auto coldIdentical   = CountIdentical(coldPass, oraclePass);
	const auto warmIdentical   = CountIdentical(warmPass, oraclePass);
	const auto warmMatchesCold = CountIdentical(warmPass, coldPass);
	ReportDifferences(registrations, coldPass, oraclePass, "cold");
	ReportDifferences(registrations, warmPass, oraclePass, "warm");

	std::printf("ShaderCacheBenchmark %zu registrations\n", total);
	std::printf(
		"  oracle CompileShaderToBlob  %8lld ms (%zu compiled)\n",
		static_cast<long long>(oraclePass.elapsed.count()),
		oraclePass.accepted);
	std::printf(
		"  cold   LoadOrCompileShader  %8lld ms (%zu compiled and published)\n",
		static_cast<long long>(coldPass.elapsed.count()),
		coldPass.accepted);
	std::printf(
		"  warm   LoadOrCompileShader  %8lld ms (%zu hits, %zu dependency paths, %zu reads)\n",
		static_cast<long long>(warmPass.elapsed.count()),
		warmPass.accepted,
		memo.ObservedPaths(),
		memo.Reads());
	std::printf("  %zu/%zu cold identical to the oracle\n", coldIdentical, total);
	std::printf("  %zu/%zu warm identical to the oracle\n", warmIdentical, total);
	std::printf("  %zu/%zu warm identical to cold\n", warmMatchesCold, total);

	if (memo.Reads() != memo.ObservedPaths())
		Fail("the warm pass re-read a dependency it had already observed");

	if (g_failures == 0)
		std::printf("ShaderCacheBenchmark passed\n");
	else
		std::printf("%d shader cache benchmark assertion(s) failed\n", g_failures);
	return g_failures ? 1 : 0;
}
