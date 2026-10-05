#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>

#include "Render/ShaderVariantCompilation.h"
#include "Utils/ShaderCache/CacheRecord.h"
#include "Utils/ShaderCache/CacheStorage.h"
#include "Utils/ShaderCache/CompilerIdentity.h"
#include "Utils/ShaderCache/ShaderCache.h"
#include "Utils/ShaderCompile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using namespace cs::shader_cache;

	int failures = 0;
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
				std::string(a_message) + ": expected " + DescribeDisposition(a_expected) + ", got " + DescribeDisposition(a_outcome.disposition));
		}
	}

	class Workspace
	{
	public:
		explicit Workspace(std::string_view a_name)
		{
			static std::atomic<unsigned> counter{ 0 };
			_root = std::filesystem::temp_directory_path() / ("fo4cs-shader-cache-" + std::string(a_name) + "-" + std::to_string(counter.fetch_add(1)));
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
			&error,
			a_recipe.includeRoots.front());
		if (!blob) {
			Fail("direct compilation failed: " + error);
			return {};
		}
		const auto* begin =
			static_cast<const std::uint8_t*>(blob->GetBufferPointer());
		return { begin, begin + blob->GetBufferSize() };
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
			primed.cold.succeeded && primed.cold.origin == CompileOrigin::kFreshCompile && primed.cold.recordWritten,
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
		workspace.Write("Sub/Shared.hlsli", "#error Includes must resolve from the shader root\n");
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
			tintedCold.succeeded && tintedCold.recordPath != primed.cold.recordPath && tintedCold.bytecode != primed.cold.bytecode,
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
			rootStale.succeeded && rootStale.bytecode != primed.cold.bytecode,
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
			includeStale.succeeded && includeStale.bytecode != rootStale.bytecode,
			"transitive include edits must return recompiled bytecode");

		const auto forced = LoadOrCompileShader(
			recipe, workspace.Options(), CacheMode::kRecompile);
		Check(
			forced.succeeded && forced.disposition == CacheDisposition::kBypassed && forced.recordWritten && forced.bytecode == includeStale.bytecode,
			"recompile mode must bypass and republish the cache");

		workspace.Write("Root.hlsl", "this is not HLSL\n");
		auto broken = recipe;
		broken.defines.emplace_back("BROKEN", "1");
		const auto failed = LoadOrCompileShader(broken, workspace.Options());
		Check(
			!failed.succeeded && !failed.recordWritten && !std::filesystem::exists(failed.recordPath),
			"failed compilation must publish no cache artifact");
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
			initialized.firstRun && !initialized.reset && initialized.error.empty(),
			"first identity must initialize without a reset");

		const auto record = root / "ps" / "record.fxc";
		std::filesystem::create_directories(record.parent_path());
		WriteAll(record, { 1, 2, 3, 4 });
		const auto changed =
			SynchronizeCacheIdentity(root, newIdentity, kRecordSchemaVersion);
		Check(
			changed.reset && changed.error.empty() && !std::filesystem::exists(record),
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

		const auto repaired =
			LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			repaired,
			CacheDisposition::kRejected,
			"corrupt records must fall through");
		Check(
			repaired.succeeded && repaired.recordWritten && repaired.bytecode == primed.cold.bytecode,
			"corrupt records must recompile and republish");
		CheckDisposition(
			LoadOrCompileShader(recipe, workspace.Options()),
			CacheDisposition::kHit,
			"repaired records must hit");
	}

	void TestConcurrentWriters()
	{
		Workspace workspace("concurrent-writers");
		workspace.WriteDefaultTree();
		const auto primed = Prime(workspace, workspace.Recipe());

		ShaderCacheRecord record;
		if (ParseShaderCacheRecord(primed.record, record) != RecordStatus::kOk) {
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
		std::atomic<int> acceptedForeign{ 0 };
		std::vector<std::thread> threads;
		for (int index = 0; index < writers; ++index) {
			threads.emplace_back([&, index] {
				start.arrive_and_wait();
				for (int round = 0; round < rounds; ++round) {
					std::string error;
					WriteRecord(primed.cold.recordPath, encoded[index], error);
				}
			});
		}
		// torn reads are expected; accepting anything but a whole writer record is not
		std::thread reader([&] {
			start.arrive_and_wait();
			while (running.load()) {
				const auto bytes = ReadAll(primed.cold.recordPath);
				ShaderCacheRecord seen;
				if (!bytes.empty() && ParseShaderCacheRecord(bytes, seen) == RecordStatus::kOk && std::ranges::find(encoded, bytes) == encoded.end()) {
					++acceptedForeign;
				}
			}
		});
		for (auto& thread : threads)
			thread.join();
		running.store(false);
		reader.join();

		const auto finalBytes = ReadAll(primed.cold.recordPath);
		Check(
			acceptedForeign.load() == 0 && std::ranges::find(encoded, finalBytes) != encoded.end(),
			"concurrent writers must never yield an accepted partial record");
	}

	// The gate only pins when the in-flight compile finishes; the compile itself goes through the real cache.
	class CompilerGate
	{
	public:
		void Enter()
		{
			std::unique_lock lock(_mutex);
			_entered = true;
			_condition.notify_all();
			_condition.wait(lock, [&] { return _released; });
		}

		void WaitEntered()
		{
			std::unique_lock lock(_mutex);
			_condition.wait(lock, [&] { return _entered; });
		}

		void Release()
		{
			{
				std::scoped_lock lock(_mutex);
				_released = true;
			}
			_condition.notify_all();
		}

	private:
		std::mutex _mutex;
		std::condition_variable _condition;
		bool _entered = false;
		bool _released = false;
	};

	winrt::com_ptr<ID3D11Device> CreateWarpDevice()
	{
		winrt::com_ptr<ID3D11Device> device;
		const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
		if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, device.put(), nullptr, nullptr)))
			return nullptr;
		return device;
	}

	struct VariantFixture
	{
		explicit VariantFixture(std::string_view a_name, std::size_t a_workers = 1) :
			workspace(a_name)
		{
			workspace.WriteDefaultTree();
			device = CreateWarpDevice();
			cache = cs::engine::CreateAsyncShaderVariantCompilationCache(
				[this](cs::engine::ShaderVariantCompilationRequest a_request) {
					attempts.fetch_add(1);
					gate.Enter();
					cs::engine::ShaderVariantCompilationOutput output;
					const auto outcome = LoadOrCompileShader(workspace.Recipe(), workspace.Options());
					winrt::com_ptr<ID3D11PixelShader> shader;
					if (outcome.succeeded && SUCCEEDED(a_request.device->CreatePixelShader(outcome.bytecode.data(), outcome.bytecode.size(), nullptr, shader.put())))
						shader->QueryInterface(IID_PPV_ARGS(output.shader.put()));
					output.samplerMask = 0x6000;
					finished.store(true);
					return output;
				},
				a_workers);
		}

		cs::engine::ShaderVariantCompilationRequest Request(std::string_view a_tag) const
		{
			cs::engine::ShaderVariantCompilationRequest request;
			request.device = device;
			request.sourcePath = workspace.Sources() / "Root.hlsl";
			request.entryPoint = "main";
			request.profile = "ps_5_0";
			request.defines = { { "TAG", std::string(a_tag) } };
			return request;
		}

		Workspace workspace;
		winrt::com_ptr<ID3D11Device> device;
		CompilerGate gate;
		std::atomic<unsigned> attempts{ 0 };
		std::atomic<bool> finished{ false };
		std::shared_ptr<cs::engine::ShaderVariantCompilationCache> cache;
	};

	bool WaitForState(const std::shared_ptr<cs::engine::ShaderVariantCompilationHandle>& a_handle, cs::engine::ShaderVariantCompilationState a_state)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (a_handle->GetState() != a_state && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		return a_handle->GetState() == a_state;
	}

	void TestVariantCompilationCoalescesAndPublishes()
	{
		using namespace cs::engine;
		VariantFixture fixture("variant-coalesce", 2);
		if (!fixture.device) {
			Fail("could not create WARP device");
			return;
		}

		constexpr std::size_t requesterCount = 12;
		std::barrier start(static_cast<std::ptrdiff_t>(requesterCount));
		std::array<std::shared_ptr<ShaderVariantCompilationHandle>, requesterCount> handles;
		{
			std::vector<std::jthread> requesters;
			for (std::size_t index = 0; index < requesterCount; ++index) {
				requesters.emplace_back([&, index] {
					start.arrive_and_wait();
					handles[index] = fixture.cache->Request(fixture.Request("A"));
				});
			}
		}
		fixture.gate.WaitEntered();
		Check(
			std::ranges::all_of(handles, [&](const auto& handle) { return handle == handles.front(); }) && fixture.attempts.load() == 1,
			"concurrent requests must coalesce to one handle and one compile");
		Check(
			handles.front()->GetState() == ShaderVariantCompilationState::kPending && !handles.front()->Acquire(),
			"pending compilation must neither block nor return a shader");

		fixture.gate.Release();
		Check(
			WaitForState(handles.front(), ShaderVariantCompilationState::kReady) && !!handles.front()->Acquire() && handles.front()->GetSamplerMask() == 0x6000,
			"completed compilation must publish its shader and sampler usage");
	}

	void TestVariantCompilationSafeShutdown()
	{
		using namespace cs::engine;
		VariantFixture fixture("variant-shutdown");
		if (!fixture.device) {
			Fail("could not create WARP device");
			return;
		}

		const auto inflight = fixture.cache->Request(fixture.Request("A"));
		const auto queued = fixture.cache->Request(fixture.Request("B"));
		fixture.gate.WaitEntered();

		std::thread stopping([&] { fixture.cache->Stop(); });
		// Stop fails queued work before it joins, so the worker is provably still blocked here.
		Check(
			WaitForState(queued, ShaderVariantCompilationState::kFailed) && !fixture.finished.load(),
			"shutdown must fail queued work while the in-flight compile is still running");
		fixture.gate.Release();
		stopping.join();
		Check(
			fixture.finished.load() && inflight->GetState() == ShaderVariantCompilationState::kFailed && !inflight->Acquire(),
			"shutdown must wait for the in-flight worker and drop its result");
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
		{ "concurrent-writers", &TestConcurrentWriters },
		{ "variant-compilation-coalesces", &TestVariantCompilationCoalescesAndPublishes },
		{ "variant-compilation-shutdown", &TestVariantCompilationSafeShutdown }
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
		failures == 0 ? "ShaderCache passed\n" : "%d shader cache assertion(s) failed\n",
		failures);
	return failures == 0 ? 0 : 1;
}
