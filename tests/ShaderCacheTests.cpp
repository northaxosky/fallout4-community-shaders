#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include "Utils/ShaderCache/CacheRecord.h"
#include "Utils/ShaderCache/CacheStorage.h"
#include "Utils/ShaderCache/CompilerIdentity.h"
#include "Utils/ShaderCache/ShaderCache.h"
#include "Utils/ShaderCompile.h"

#include <atomic>
#include <barrier>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using namespace cs::shader_cache;

	int         failures = 0;
	const char* currentTest = "";

	void Fail(std::string_view a_message)
	{
		std::printf(
			"FAIL [%s]: %.*s\n",
			currentTest,
			static_cast<int>(a_message.size()),
			a_message.data());
		++failures;
	}

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			Fail(a_message);
	}

	void CheckDisposition(
		const ShaderCacheOutcome& a_outcome,
		CacheDisposition a_expected,
		std::string_view a_message)
	{
		if (a_outcome.disposition != a_expected) {
			Fail(
				std::string(a_message) + ": expected "
				+ DescribeDisposition(a_expected) + ", got "
				+ DescribeDisposition(a_outcome.disposition));
		}
	}

	class Workspace
	{
	public:
		explicit Workspace(std::string_view a_name)
		{
			static std::atomic<unsigned> counter{ 0 };
			_root = std::filesystem::temp_directory_path()
				/ ("fo4cs-shader-cache-" + std::string(a_name) + "-"
					+ std::to_string(counter.fetch_add(1)));
			std::filesystem::remove_all(_root);
			std::filesystem::create_directories(Sources() / "Sub");
			std::filesystem::create_directories(CacheRoot());
		}

		~Workspace()
		{
			std::error_code error;
			std::filesystem::remove_all(_root, error);
		}

		[[nodiscard]] std::filesystem::path Root() const { return _root; }
		[[nodiscard]] std::filesystem::path Sources() const { return _root / "Shaders"; }
		[[nodiscard]] std::filesystem::path CacheRoot() const { return _root / "Cache"; }
		[[nodiscard]] ShaderCacheOptions Options() const { return { CacheRoot() }; }

		void Write(
			const std::filesystem::path& a_relative,
			std::string_view a_text) const
		{
			const auto path = Sources() / a_relative;
			std::filesystem::create_directories(path.parent_path());
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
		}

		void WriteDefaultTree() const
		{
			Write("Root.hlsl", R"(#include "Sub/Wrapper.hlsli"
float4 main() : SV_Target { return Wrapped(); }
)");
			Write("Sub/Wrapper.hlsli", R"(#include "Shared.hlsli"
float4 Wrapped() { return SharedValue(); }
)");
			Write("Shared.hlsli", R"(float4 SharedValue()
{
#ifdef TINT
	return float4(0.25, 0.5, 0.75, 1.0);
#else
	return float4(1.0, 0.0, 0.0, 1.0);
#endif
}
)");
		}

		[[nodiscard]] ShaderRecipe Recipe() const
		{
			ShaderRecipe recipe;
			recipe.source = Sources() / "Root.hlsl";
			recipe.includeRoots.push_back(Sources());
			recipe.entryPoint = "main";
			recipe.profile = "ps_5_0";
			recipe.stage = ShaderCacheStage::kPixel;
			return recipe;
		}

	private:
		std::filesystem::path _root;
	};

	void WriteAll(
		const std::filesystem::path& a_path,
		const std::vector<std::uint8_t>& a_bytes)
	{
		std::ofstream file(a_path, std::ios::binary | std::ios::trunc);
		file.write(
			reinterpret_cast<const char*>(a_bytes.data()),
			static_cast<std::streamsize>(a_bytes.size()));
	}

	std::vector<std::uint8_t> ReadAll(const std::filesystem::path& a_path)
	{
		std::vector<std::uint8_t> bytes;
		ReadFileBytes(a_path, kMaxRecordBytes, bytes);
		return bytes;
	}

	std::vector<std::uint8_t> CompileDirect(const ShaderRecipe& a_recipe)
	{
		std::vector<std::pair<const char*, const char*>> defines;
		for (const auto& [name, value] : a_recipe.defines)
			defines.emplace_back(name.c_str(), value.c_str());

		std::string error;
		const auto blob = cs::util::CompileShaderToBlob(
			a_recipe.source.c_str(),
			defines,
			a_recipe.profile.c_str(),
			a_recipe.entryPoint.c_str(),
			a_recipe.flags1,
			&error);
		if (!blob) {
			Fail("direct compilation failed: " + error);
			return {};
		}
		const auto* begin =
			static_cast<const std::uint8_t*>(blob->GetBufferPointer());
		return { begin, begin + blob->GetBufferSize() };
	}

	std::size_t CountTemporaryFiles(const std::filesystem::path& a_root)
	{
		std::size_t count = 0;
		std::error_code error;
		for (const auto& entry :
			std::filesystem::recursive_directory_iterator(a_root, error)) {
			if (entry.is_regular_file() && entry.path().extension() == ".tmp")
				++count;
		}
		return count;
	}

	struct PrimedCache
	{
		ShaderCacheOutcome cold;
		std::vector<std::uint8_t> record;
	};

	PrimedCache Prime(const Workspace& a_workspace, const ShaderRecipe& a_recipe)
	{
		PrimedCache primed;
		primed.cold = LoadOrCompileShader(a_recipe, a_workspace.Options());
		Check(
			primed.cold.succeeded
				&& primed.cold.origin == CompileOrigin::kFreshCompile
				&& primed.cold.recordWritten,
			"cold compile must succeed and publish");
		const auto warm = LoadOrCompileShader(a_recipe, a_workspace.Options());
		CheckDisposition(warm, CacheDisposition::kHit, "second compile must hit");
		Check(warm.bytecode == primed.cold.bytecode, "cache hit must preserve bytecode");
		primed.record = ReadAll(primed.cold.recordPath);
		Check(!primed.record.empty(), "published record must be readable");
		return primed;
	}

	void TestHitAndInvalidation()
	{
		Workspace workspace("hit-invalidation");
		workspace.WriteDefaultTree();
		auto recipe = workspace.Recipe();
		const auto primed = Prime(workspace, recipe);
		Check(
			primed.cold.bytecode == CompileDirect(recipe),
			"cache output must match direct compilation");

		auto tinted = recipe;
		tinted.defines.emplace_back("TINT", "1");
		const auto tintedCold =
			LoadOrCompileShader(tinted, workspace.Options());
		Check(
			tintedCold.succeeded
				&& tintedCold.recordPath != primed.cold.recordPath
				&& tintedCold.bytecode != primed.cold.bytecode,
			"defines must address and compile distinct variants");

		workspace.Write("Root.hlsl", R"(#include "Sub/Wrapper.hlsli"
float4 main() : SV_Target { return Wrapped() * 0.5; }
)");
		const auto rootStale =
			LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			rootStale,
			CacheDisposition::kStale,
			"root edits must invalidate the record");
		Check(
			rootStale.succeeded
				&& rootStale.bytecode != primed.cold.bytecode,
			"root edits must return recompiled bytecode");

		workspace.Write("Shared.hlsli", R"(float4 SharedValue()
{
	return float4(0.0, 0.0, 1.0, 1.0);
}
)");
		const auto includeStale =
			LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			includeStale,
			CacheDisposition::kStale,
			"transitive include edits must invalidate the record");
		Check(
			includeStale.succeeded
				&& includeStale.bytecode != rootStale.bytecode,
			"transitive include edits must return recompiled bytecode");
	}

	void TestCompilerIdentityReset()
	{
		Workspace workspace("identity-reset");
		const auto root = workspace.CacheRoot();
		const auto module = workspace.Root() / "D3DCompiler_47.dll";
		const auto oldIdentity = MakeVersionCompilerIdentity(
			module, 4'669'440, { 10, 0, 26'100, 9'168 });
		const auto newIdentity = MakeVersionCompilerIdentity(
			module, 4'669'440, { 10, 0, 26'100, 9'278 });

		const auto initialized =
			SynchronizeCacheIdentity(root, oldIdentity, kRecordSchemaVersion);
		Check(
			initialized.firstRun && !initialized.reset
				&& initialized.error.empty(),
			"first identity must initialize without a reset");

		const auto record = root / "ps" / "record.fxc";
		std::filesystem::create_directories(record.parent_path());
		WriteAll(record, { 1, 2, 3, 4 });
		const auto changed =
			SynchronizeCacheIdentity(root, newIdentity, kRecordSchemaVersion);
		Check(
			changed.reset && changed.error.empty()
				&& !std::filesystem::exists(record),
			"compiler replacement must discard incompatible records");
	}

	void TestCorruptRecord()
	{
		Workspace workspace("corrupt");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = Prime(workspace, recipe);

		auto corrupted = primed.record;
		corrupted.front() ^= 0xFF;
		WriteAll(primed.cold.recordPath, corrupted);
		ShaderCacheRecord parsed;
		Check(
			ParseShaderCacheRecord(corrupted, parsed)
				== RecordStatus::kBadMagic,
			"corrupt records must be rejected");

		const auto repaired =
			LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			repaired,
			CacheDisposition::kRejected,
			"corrupt records must fall through");
		Check(
			repaired.succeeded && repaired.recordWritten
				&& repaired.bytecode == primed.cold.bytecode,
			"corrupt records must recompile and republish");
		CheckDisposition(
			LoadOrCompileShader(recipe, workspace.Options()),
			CacheDisposition::kHit,
			"repaired records must hit");
	}

	void TestFailedCompileIsNotCached()
	{
		Workspace workspace("failed-compile");
		workspace.WriteDefaultTree();
		workspace.Write("Root.hlsl", "this is not HLSL\n");
		const auto outcome =
			LoadOrCompileShader(workspace.Recipe(), workspace.Options());
		Check(
			!outcome.succeeded && !outcome.error.empty()
				&& !outcome.recordWritten
				&& !std::filesystem::exists(outcome.recordPath)
				&& CountTemporaryFiles(workspace.CacheRoot()) == 0,
			"failed compilation must publish no cache artifact");
	}

	void TestAtomicWriters()
	{
		Workspace workspace("atomic-writers");
		workspace.WriteDefaultTree();
		const auto primed = Prime(workspace, workspace.Recipe());

		ShaderCacheRecord record;
		if (ParseShaderCacheRecord(primed.record, record)
			!= RecordStatus::kOk) {
			Fail("pristine record must parse");
			return;
		}

		constexpr int writers = 4;
		constexpr int rounds = 16;
		std::vector<std::vector<std::uint8_t>> encoded(writers);
		for (int index = 0; index < writers; ++index) {
			auto variant = record;
			variant.payload.back() = static_cast<std::uint8_t>(index);
			Check(
				SerializeShaderCacheRecord(variant, encoded[index]),
				"writer record must serialize");
		}

		std::barrier start(writers + 1);
		std::atomic<bool> running{ true };
		std::atomic<int> tornReads{ 0 };
		std::vector<std::thread> threads;
		for (int index = 0; index < writers; ++index) {
			threads.emplace_back([&, index] {
				start.arrive_and_wait();
				for (int round = 0; round < rounds; ++round) {
					std::string error;
					if (!WriteRecordAtomically(
							primed.cold.recordPath,
							encoded[index],
							error)) {
						continue;
					}
				}
			});
		}
		std::thread reader([&] {
			start.arrive_and_wait();
			while (running.load()) {
				const auto bytes = ReadAll(primed.cold.recordPath);
				ShaderCacheRecord seen;
				if (!bytes.empty()
					&& ParseShaderCacheRecord(bytes, seen)
						!= RecordStatus::kOk) {
					++tornReads;
				}
			}
		});
		for (auto& thread : threads)
			thread.join();
		running.store(false);
		reader.join();

		const auto finalBytes = ReadAll(primed.cold.recordPath);
		bool matchedWriter = false;
		for (const auto& candidate : encoded)
			matchedWriter = matchedWriter || candidate == finalBytes;
		Check(
			tornReads.load() == 0 && matchedWriter
				&& CountTemporaryFiles(workspace.CacheRoot()) == 0,
			"atomic publication must expose one complete record and leak no temporaries");
	}

	void TestConcurrentCompilersAndRecompile()
	{
		Workspace workspace("concurrent-compilers");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();

		constexpr int threadCount = 4;
		std::barrier start(threadCount);
		std::vector<ShaderCacheOutcome> outcomes(threadCount);
		std::vector<std::thread> threads;
		for (int index = 0; index < threadCount; ++index) {
			threads.emplace_back([&, index] {
				start.arrive_and_wait();
				outcomes[index] =
					LoadOrCompileShader(recipe, workspace.Options());
			});
		}
		for (auto& thread : threads)
			thread.join();
		for (const auto& outcome : outcomes) {
			Check(
				outcome.succeeded
					&& outcome.bytecode == outcomes.front().bytecode,
				"concurrent compilers must agree on bytecode");
		}
		Check(
			CountTemporaryFiles(workspace.CacheRoot()) == 0,
			"concurrent compilers must leak no temporaries");

		const auto forced = LoadOrCompileShader(
			recipe, workspace.Options(), CacheMode::kRecompile);
		Check(
			forced.succeeded
				&& forced.disposition == CacheDisposition::kBypassed
				&& forced.origin == CompileOrigin::kFreshCompile
				&& forced.recordWritten
				&& forced.bytecode == outcomes.front().bytecode,
			"recompile mode must bypass and republish the cache");
	}

	struct TestCase
	{
		const char* name;
		void (*run)();
	};

	constexpr TestCase tests[]{
		{ "hit-and-invalidation", &TestHitAndInvalidation },
		{ "compiler-identity-reset", &TestCompilerIdentityReset },
		{ "corrupt-record", &TestCorruptRecord },
		{ "failed-compile-not-cached", &TestFailedCompileIsNotCached },
		{ "atomic-writers", &TestAtomicWriters },
		{ "concurrent-compilers-and-recompile",
			&TestConcurrentCompilersAndRecompile }
	};
}

int main()
{
	for (const auto& test : tests) {
		currentTest = test.name;
		const int before = failures;
		test.run();
		std::printf(
			"%-36s %s\n",
			test.name,
			failures == before ? "ok" : "FAILED");
	}
	std::printf(
		failures == 0 ? "ShaderCache passed\n"
					  : "%d shader cache assertion(s) failed\n",
		failures);
	return failures == 0 ? 0 : 1;
}
