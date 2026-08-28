#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "Utils/ShaderCache/CacheRecord.h"
#include "Utils/ShaderCache/CacheStorage.h"
#include "Utils/ShaderCache/CompilerIdentity.h"
#include "Utils/ShaderCache/RevalidationContext.h"
#include "Utils/ShaderCache/ShaderCache.h"
#include "Utils/ShaderCompile.h"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	using namespace cs::shader_cache;

	int         g_failures = 0;
	const char* g_currentTest = "";

	class ScopedHandle
	{
	public:
		explicit ScopedHandle(HANDLE a_handle) noexcept :
			_handle(a_handle)
		{}

		~ScopedHandle()
		{
			Reset();
		}

		ScopedHandle(const ScopedHandle&) = delete;
		ScopedHandle& operator=(const ScopedHandle&) = delete;

		[[nodiscard]] bool Valid() const noexcept
		{
			return _handle != INVALID_HANDLE_VALUE;
		}

		void Reset() noexcept
		{
			if (_handle != INVALID_HANDLE_VALUE) {
				CloseHandle(_handle);
				_handle = INVALID_HANDLE_VALUE;
			}
		}

	private:
		HANDLE _handle;
	};

	void Fail(std::string_view a_what)
	{
		std::printf("FAIL [%s]: %.*s\n",
			g_currentTest,
			static_cast<int>(a_what.size()),
			a_what.data());
		++g_failures;
	}

	void Check(bool a_condition, std::string_view a_what)
	{
		if (!a_condition)
			Fail(a_what);
	}

	void CheckDisposition(
		const ShaderCacheOutcome& a_outcome,
		CacheDisposition          a_expected,
		std::string_view          a_what)
	{
		if (a_outcome.disposition != a_expected) {
			Fail(std::string(a_what) + ": expected "
				+ DescribeDisposition(a_expected) + ", got "
				+ DescribeDisposition(a_outcome.disposition) + " ("
				+ a_outcome.cacheNote + ")");
		}
	}

	void CheckRecordStatus(
		RecordStatus     a_actual,
		RecordStatus     a_expected,
		std::string_view a_what)
	{
		if (a_actual != a_expected) {
			Fail(std::string(a_what) + ": expected "
				+ DescribeRecordStatus(a_expected) + ", got "
				+ DescribeRecordStatus(a_actual));
		}
	}

	// isolated from Data\ShaderCache
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

		Workspace(const Workspace&) = delete;
		Workspace& operator=(const Workspace&) = delete;

		[[nodiscard]] std::filesystem::path Root() const { return _root; }
		[[nodiscard]] std::filesystem::path Sources() const { return _root / "Shaders"; }
		[[nodiscard]] std::filesystem::path CacheRoot() const { return _root / "Cache"; }
		[[nodiscard]] ShaderCacheOptions Options() const { return { CacheRoot() }; }

		void Write(const std::filesystem::path& a_relative, std::string_view a_text) const
		{
			const auto path = Sources() / a_relative;
			std::filesystem::create_directories(path.parent_path());
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
		}

		void WriteDefaultTree() const
		{
			Write("Root.hlsl", R"(#include "Sub/Wrapper.hlsli"

float4 main() : SV_Target
{
	return Wrapped() * 1.0;
}
)");
			Write("Sub/Wrapper.hlsli", R"(#include "Shared.hlsli"

float4 Wrapped()
{
	return SharedValue();
}
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
			recipe.profile    = "ps_5_0";
			recipe.stage      = ShaderCacheStage::kPixel;
			return recipe;
		}

	private:
		std::filesystem::path _root;
	};

	std::vector<std::uint8_t> ReadAll(const std::filesystem::path& a_path)
	{
		std::vector<std::uint8_t> bytes;
		ReadFileBytes(a_path, kMaxRecordBytes, bytes);
		return bytes;
	}

	constexpr std::string_view  kEmitIdentityFlag     = "--emit-compiler-identity";
	constexpr std::wstring_view kEmitIdentityFlagWide = L"--emit-compiler-identity";

	std::string FormatCompilerIdentity(const CompilerIdentity& a_identity)
	{
		return std::string(DescribeCompilerIdentityMechanism(
				   a_identity.mechanism))
			+ "|" + DescribeCompilerIdentityValue(a_identity) + "|"
			+ EncodeLocator(a_identity.modulePath) + "|"
			+ std::to_string(a_identity.moduleLength) + "|"
			+ cs::sha256::Sha256ToHex(a_identity.moduleDigest);
	}

	std::filesystem::path CurrentExecutablePath()
	{
		std::wstring buffer(MAX_PATH, L'\0');
		for (;;) {
			const DWORD written =
				GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (written == 0)
				return {};
			if (written < buffer.size()) {
				buffer.resize(written);
				return buffer;
			}
			if (buffer.size() >= 32768)
				return {};
			buffer.resize(buffer.size() * 2);
		}
	}

	int EmitCompilerIdentity(const std::filesystem::path& a_output)
	{
		const auto& identity = GetD3DCompilerIdentity();
		if (!identity.established)
			return 2;

		const auto line = FormatCompilerIdentity(identity);
		std::printf("%s\n", line.c_str());

		std::ofstream file(a_output, std::ios::binary | std::ios::trunc);
		file.write(line.data(), static_cast<std::streamsize>(line.size()));
		file.close();
		return file ? 0 : 3;
	}

	bool RunIdentityChild(
		const std::filesystem::path& a_executable,
		const std::filesystem::path& a_output,
		std::string&                 a_identity)
	{
		a_identity.clear();

		std::wstring commandLine = L"\"" + a_executable.wstring() + L"\" "
			+ std::wstring(kEmitIdentityFlagWide) + L" \"" + a_output.wstring() + L"\"";

		STARTUPINFOW        startup{};
		PROCESS_INFORMATION process{};
		startup.cb = sizeof(startup);
		if (!CreateProcessW(
				a_executable.c_str(),
				commandLine.data(),
				nullptr,
				nullptr,
				FALSE,
				CREATE_NO_WINDOW,
				nullptr,
				nullptr,
				&startup,
				&process)) {
			Fail("could not launch the identity child: "
				+ std::to_string(GetLastError()));
			return false;
		}

		const auto waited   = WaitForSingleObject(process.hProcess, 120000);
		DWORD      exitCode = ~0u;
		const bool reported = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);

		if (waited != WAIT_OBJECT_0) {
			Fail("the identity child did not exit in time");
			return false;
		}
		if (!reported || exitCode != 0) {
			Fail("the identity child failed with exit code " + std::to_string(exitCode));
			return false;
		}

		const auto bytes = ReadAll(a_output);
		a_identity.assign(bytes.begin(), bytes.end());
		if (a_identity.empty()) {
			Fail("the identity child wrote nothing");
			return false;
		}
		return true;
	}

	void WriteAll(
		const std::filesystem::path&     a_path,
		const std::vector<std::uint8_t>& a_bytes)
	{
		std::ofstream file(a_path, std::ios::binary | std::ios::trunc);
		file.write(
			reinterpret_cast<const char*>(a_bytes.data()),
			static_cast<std::streamsize>(a_bytes.size()));
	}

	std::vector<std::uint8_t> CompileWithPlainCompiler(const ShaderRecipe& a_recipe)
	{
		std::vector<std::pair<const char*, const char*>> defines;
		defines.reserve(a_recipe.defines.size());
		for (const auto& [name, value] : a_recipe.defines)
			defines.emplace_back(name.c_str(), value.c_str());

		std::string error;
		const auto  blob = cs::util::CompileShaderToBlob(
            a_recipe.source.c_str(),
            defines,
            a_recipe.profile.c_str(),
            a_recipe.entryPoint.c_str(),
            a_recipe.flags1,
            &error);
		if (!blob) {
			Fail("plain compiler failed: " + error);
			return {};
		}
		const auto* first = static_cast<const std::uint8_t*>(blob->GetBufferPointer());
		return { first, first + blob->GetBufferSize() };
	}

	std::size_t CountTemporaryFiles(const std::filesystem::path& a_directory)
	{
		std::size_t count = 0;
		std::error_code error;
		for (const auto& entry :
			std::filesystem::recursive_directory_iterator(a_directory, error)) {
			if (entry.is_regular_file() && entry.path().extension() == ".tmp")
				++count;
		}
		return count;
	}

	// fixed-header offsets from the record layout
	std::size_t PayloadLengthOffset(const std::string& a_profile)
	{
		return 8 + 4 + 32 * 3 + 1 + 4 + a_profile.size() + 8 + 4;
	}

	std::size_t ManifestLengthOffset(const std::string& a_profile)
	{
		return 8 + 4 + 32 * 3 + 1 + 4 + a_profile.size();
	}

	void PatchU64(
		std::vector<std::uint8_t>& a_bytes,
		std::size_t                a_offset,
		std::uint64_t              a_value)
	{
		for (unsigned index = 0; index < 8; ++index)
			a_bytes[a_offset + index] = static_cast<std::uint8_t>((a_value >> (index * 8)) & 0xFFu);
	}

	std::uint64_t ReadU64(
		const std::vector<std::uint8_t>& a_bytes,
		std::size_t                     a_offset)
	{
		std::uint64_t value = 0;
		for (unsigned index = 0; index < 8; ++index)
			value |= static_cast<std::uint64_t>(a_bytes[a_offset + index]) << (index * 8);
		return value;
	}

	void PatchDigest(
		std::vector<std::uint8_t>&       a_bytes,
		std::size_t                      a_offset,
		const cs::sha256::Sha256Result& a_digest)
	{
		std::ranges::copy(a_digest.bytes, a_bytes.begin() + a_offset);
	}

	struct PrimedCache
	{
		ShaderCacheOutcome        cold;
		ShaderCacheOutcome        warm;
		std::vector<std::uint8_t> recordBytes;
	};

	PrimedCache PrimeCache(const Workspace& a_workspace, const ShaderRecipe& a_recipe)
	{
		PrimedCache primed;
		primed.cold = LoadOrCompileShader(a_recipe, a_workspace.Options());
		Check(primed.cold.succeeded, "cold compile succeeded");
		Check(primed.cold.origin == CompileOrigin::kFreshCompile, "cold compile is fresh");
		Check(primed.cold.recordWritten, "cold compile published a record: " + primed.cold.cacheNote);

		primed.warm = LoadOrCompileShader(a_recipe, a_workspace.Options());
		Check(primed.warm.succeeded, "warm compile succeeded");
		CheckDisposition(primed.warm, CacheDisposition::kHit, "pristine record is a hit");
		Check(primed.warm.bytecode == primed.cold.bytecode, "warm bytecode equals cold bytecode");

		primed.recordBytes = ReadAll(primed.cold.recordPath);
		Check(!primed.recordBytes.empty(), "record file is readable");
		return primed;
	}

	void TestCompilerIdentity()
	{
		const auto& identity = GetD3DCompilerIdentity();
		Check(identity.established, "d3dcompiler identity is established");
		if (!identity.established)
			return;

		const auto name = identity.modulePath.filename().string();
		std::string lowered;
		lowered.reserve(name.size());
		for (const char character : name)
			lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
		Check(lowered.starts_with("d3dcompiler"),
			"identity names a d3dcompiler module, got " + name);
		Check(identity.moduleLength > 0, "identity records the module length");
		Check(!cs::sha256::Sha256IsZero(identity.moduleDigest), "identity records a module digest");
		Check(identity.mechanism != CompilerIdentityMechanism::kUnavailable,
			"identity records its mechanism");
		Check(DescribeCompilerIdentityValue(identity) != "unavailable",
			"identity records its value");

		Workspace workspace("identity");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();

		CompilerIdentity mutated = identity;
		mutated.moduleDigest.bytes[0] =
			static_cast<std::uint8_t>(mutated.moduleDigest.bytes[0] ^ 0xFF);
		const auto genuine = ComputeLogicalDigest(EncodeShaderRecipe(recipe, identity));
		const auto altered = ComputeLogicalDigest(EncodeShaderRecipe(recipe, mutated));
		Check(!(genuine == altered), "compiler identity participates in the logical digest");

		const auto serviced9168 = MakeVersionCompilerIdentity(
			identity.modulePath,
			4'669'440,
			{ 10, 0, 26'100, 9'168 });
		const auto serviced9278 = MakeVersionCompilerIdentity(
			identity.modulePath,
			4'669'440,
			{ 10, 0, 26'100, 9'278 });
		Check(
			DescribeCompilerIdentityValue(serviced9168)
				== "10.0.26100.9168",
			"version identity formats servicing build 9168");
		Check(
			DescribeCompilerIdentityValue(serviced9278)
				== "10.0.26100.9278",
			"version identity formats servicing build 9278");
		Check(serviced9168.moduleLength == serviced9278.moduleLength,
			"servicing identities retain the shared module length");
		Check(!(serviced9168.moduleDigest == serviced9278.moduleDigest),
			"servicing versions produce distinct identity digests");
		const auto digest9168 =
			ComputeLogicalDigest(EncodeShaderRecipe(recipe, serviced9168));
		const auto digest9278 =
			ComputeLogicalDigest(EncodeShaderRecipe(recipe, serviced9278));
		Check(!(digest9168 == digest9278),
			"servicing versions produce distinct logical digests");
	}

	// persisted identity must survive process boundaries
	void TestCompilerIdentityAcrossProcesses()
	{
		const auto executable = CurrentExecutablePath();
		if (executable.empty()) {
			Fail("could not resolve this test executable");
			return;
		}

		Workspace   workspace("identity-processes");
		std::string first;
		std::string second;
		if (!RunIdentityChild(executable, workspace.Root() / "identity-1.txt", first))
			return;
		if (!RunIdentityChild(executable, workspace.Root() / "identity-2.txt", second))
			return;

		Check(first == second,
			"two processes resolved the same compiler identity, got '" + first
				+ "' and '" + second + "'");

		const auto& identity = GetD3DCompilerIdentity();
		if (identity.established) {
			Check(FormatCompilerIdentity(identity) == first,
				"the child processes agree with this process");
		}
		std::printf("  cross-process compiler identity: %s\n", first.c_str());
	}

	void TestCompilerIdentityReset()
	{
		Workspace workspace("identity-reset");
		const auto cacheRoot = workspace.Root() / "CacheFamily";
		std::filesystem::create_directories(cacheRoot);
		const auto modulePath = workspace.Root() / "D3DCompiler_47.dll";
		const auto oldIdentity = MakeVersionCompilerIdentity(
			modulePath,
			4'669'440,
			{ 10, 0, 26'100, 9'168 });
		const auto newIdentity = MakeVersionCompilerIdentity(
			modulePath,
			4'669'440,
			{ 10, 0, 26'100, 9'278 });

		const auto initialized =
			SynchronizeCacheIdentity(
				cacheRoot,
				oldIdentity,
				kRecordSchemaVersion);
		Check(initialized.firstRun, "initial identity is a first run");
		Check(!initialized.reset, "initial identity does not reset");
		Check(initialized.error.empty(),
			"initial identity sidecar is written: " + initialized.error);

		const auto record = cacheRoot / "ps" / "record.fxc";
		std::filesystem::create_directories(record.parent_path());
		WriteAll(record, { 1, 2, 3, 4 });
		const auto changed =
			SynchronizeCacheIdentity(
				cacheRoot,
				newIdentity,
				kRecordSchemaVersion);
		Check(changed.reset, "changed identity resets the cache root");
		Check(!std::filesystem::exists(record),
			"changed identity removes old records");
		Check(
			changed.resetMessage
				== "shader cache reset: compiler 10.0.26100.9168 -> 10.0.26100.9278",
			"changed identity reports the servicing transition");
		Check(changed.error.empty(),
			"changed identity sidecar is written: " + changed.error);

		const auto unchanged =
			SynchronizeCacheIdentity(
				cacheRoot,
				newIdentity,
				kRecordSchemaVersion);
		Check(!unchanged.reset, "matching identity preserves the cache");
		Check(unchanged.resetMessage.empty(),
			"matching identity does not report a reset");

		std::filesystem::create_directories(record.parent_path());
		WriteAll(record, { 5, 6, 7, 8 });
		constexpr auto nextRecordSchemaVersion =
			kRecordSchemaVersion + 1;
		const auto schemaChanged =
			SynchronizeCacheIdentity(
				cacheRoot,
				newIdentity,
				nextRecordSchemaVersion);
		Check(schemaChanged.reset,
			"changed record schema resets the cache root");
		Check(!std::filesystem::exists(record),
			"changed record schema removes old records");
		Check(
			schemaChanged.resetMessage
				== "shader cache reset: record schema 1 -> 2",
			"changed record schema reports the format transition");

		std::filesystem::create_directories(record.parent_path());
		WriteAll(record, { 5, 6, 7, 8 });
		std::filesystem::remove(cacheRoot / "identity.txt");
		const auto missing =
			SynchronizeCacheIdentity(
				cacheRoot,
				newIdentity,
				nextRecordSchemaVersion);
		Check(missing.reset, "missing sidecar resets a populated cache");
		Check(!std::filesystem::exists(record),
			"missing sidecar removes untracked records");

		std::filesystem::create_directories(record.parent_path());
		WriteAll(record, { 9, 10, 11, 12 });
		WriteAll(
			cacheRoot / "identity.txt",
			std::vector<std::uint8_t>{ 'c', 'o', 'r', 'r', 'u', 'p', 't' });
		const auto corrupt =
			SynchronizeCacheIdentity(
				cacheRoot,
				newIdentity,
				nextRecordSchemaVersion);
		Check(corrupt.reset, "corrupt sidecar resets a populated cache");
		Check(!std::filesystem::exists(record),
			"corrupt sidecar removes untrusted records");
	}

	void TestCompilerIdentityFirstRun()
	{
		Workspace workspace("identity-first-run");
		const auto cacheRoot =
			workspace.Root() / "CacheFamily";
		std::filesystem::create_directories(cacheRoot);
		const auto abandonedRecord =
			cacheRoot / "abandoned-layout" / "record.fxc";
		std::filesystem::create_directories(
			abandonedRecord.parent_path());
		WriteAll(abandonedRecord, { 1, 2, 3, 4 });
		const auto identity = MakeVersionCompilerIdentity(
			workspace.Root() / "D3DCompiler_47.dll",
			4'669'440,
			{ 10, 0, 26'100, 9'278 });

		const auto result =
			SynchronizeCacheIdentity(
				cacheRoot,
				identity,
				kRecordSchemaVersion);
		Check(result.firstRun, "missing sidecar and no records is a first run");
		Check(!result.reset, "first run does not reset the cache root");
		Check(result.resetMessage.empty(),
			"first run does not report a spurious reset");
		Check(std::filesystem::exists(abandonedRecord),
			"first run leaves abandoned layouts untouched");
		Check(result.error.empty(),
			"first-run identity sidecar is written: " + result.error);
		Check(std::filesystem::exists(
				  cacheRoot / "identity.txt"),
			"first run publishes the identity sidecar");

		const auto sidecarBytes =
			ReadAll(cacheRoot / "identity.txt");
		const std::string sidecar(
			sidecarBytes.begin(),
			sidecarBytes.end());
		Check(sidecar.starts_with(
				  "FO4CS.compiler-identity.v1|record-schema=1|version-info|"
				  "10.0.26100.9278|4669440|"),
			"first run records the record and compiler schemas");
	}

	void TestCacheMissIsObservable()
	{
		Workspace workspace("observable-miss");
		workspace.WriteDefaultTree();
		ResetShaderCacheCounters();

		const auto outcome =
			LoadOrCompileShader(workspace.Recipe(), workspace.Options());
		Check(outcome.succeeded, "observable miss compiles");
		CheckDisposition(outcome, CacheDisposition::kAbsent,
			"cold lookup reports absent");
		Check(DescribeCacheOutcome(outcome) == "absent",
			"cold lookup renders the absent disposition");

		const auto counters = GetShaderCacheCounters();
		Check(counters.hit == 0, "observable miss records no hit");
		Check(counters.absent == 1, "observable miss increments absent");
		Check(counters.stale == 0, "observable miss records no stale entry");
		Check(counters.rejected == 0,
			"observable miss records no rejected entry");
		Check(counters.written == 1,
			"observable miss increments written");
	}

	void TestCompilerIdentityHashFallback()
	{
		Workspace workspace("identity-hash-fallback");
		const auto modulePath = workspace.Root() / "versionless.dll";
		WriteAll(modulePath, { 'A', 'A', 'A', 'A' });

		const auto first = ResolveCompilerIdentity(modulePath);
		const auto second = ResolveCompilerIdentity(modulePath);
		Check(first.established, "versionless file establishes an identity");
		Check(
			first.mechanism == CompilerIdentityMechanism::kContentHash,
			"versionless file uses the content hash fallback");
		Check(FormatCompilerIdentity(first) == FormatCompilerIdentity(second),
			"content hash fallback is stable");
		Check(first.moduleLength == 4,
			"content hash fallback records the file size");

		WriteAll(modulePath, { 'B', 'B', 'B', 'B' });
		const auto changed = ResolveCompilerIdentity(modulePath);
		Check(changed.established,
			"changed versionless file establishes an identity");
		Check(
			changed.mechanism == CompilerIdentityMechanism::kContentHash,
			"changed versionless file retains the hash fallback");
		Check(changed.moduleLength == first.moduleLength,
			"fallback comparison keeps file size fixed");
		Check(!(changed.moduleDigest == first.moduleDigest),
			"fallback content changes discriminate equal-size files");
	}

	void TestCacheRootPath()
	{
		const auto defaultRoot = DefaultCacheRoot();
		Check(defaultRoot.is_absolute(), "default cache root is absolute");
		Check(defaultRoot.filename() == L"ShaderCache",
			"default cache root is the shared shader cache");
		Check(defaultRoot.parent_path().filename() == L"Data",
			"default cache root is under Data");
		const auto executable = CurrentExecutablePath();
		Check(
			executable.empty()
				|| defaultRoot
					== (executable.parent_path() / "Data" / "ShaderCache")
						.lexically_normal(),
			"default cache root is executable-relative");
	}

	void TestHitMatchesFreshCompile()
	{
		Workspace workspace("hit");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();

		const auto primed = PrimeCache(workspace, recipe);
		const auto oracle = CompileWithPlainCompiler(recipe);
		Check(!oracle.empty(), "oracle produced bytecode");
		Check(primed.cold.bytecode == oracle, "fresh compile matches CompileShaderToBlob");
		Check(primed.warm.bytecode == oracle, "cache hit matches CompileShaderToBlob");

		ShaderCacheRecord record;
		CheckRecordStatus(
			ParseShaderCacheRecord(primed.recordBytes, record),
			RecordStatus::kOk,
			"pristine record parses");
		Check(record.payload == oracle, "record payload matches CompileShaderToBlob");
		Check(record.profile == recipe.profile, "record carries the profile");
		Check(record.stage == recipe.stage, "record carries the stage");
		Check(record.manifest.includes.size() == 2, "manifest records both includes");
	}

	void TestComputeStageEncoding()
	{
		Check(std::string_view(DescribeStage(ShaderCacheStage::kCompute)) == "cs",
			"compute stage has a stable path name");
		Check(IsKnownStage(static_cast<std::uint8_t>(ShaderCacheStage::kCompute)),
			"compute stage is recognized");
		Check(!IsKnownStage(0xFF), "future stage values are rejected");

		Workspace workspace("compute-stage");
		workspace.Write("Compute.hlsl", R"(RWTexture2D<float4> Output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	Output[id.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
)");
		auto recipe = workspace.Recipe();
		recipe.source = workspace.Sources() / "Compute.hlsl";
		recipe.profile = "cs_5_0";
		recipe.stage = ShaderCacheStage::kCompute;

		const auto primed = PrimeCache(workspace, recipe);
		Check(primed.cold.recordPath.parent_path().parent_path().filename() == "cs",
			"compute records use the compute stage directory");

		ShaderCacheRecord record;
		CheckRecordStatus(
			ParseShaderCacheRecord(primed.recordBytes, record),
			RecordStatus::kOk,
			"compute record parses");
		Check(record.stage == ShaderCacheStage::kCompute,
			"compute record preserves its stage byte");

		auto futureRecord = primed.recordBytes;
		constexpr std::size_t stageOffset = 8 + 4 + 32 * 3;
		futureRecord[stageOffset] = 0xFF;
		CheckRecordStatus(
			ParseShaderCacheRecord(futureRecord, record),
			RecordStatus::kUnknownStage,
			"unknown future stage is rejected without reinterpretation");
	}

	void TestCrossDirectoryTransitiveInvalidation()
	{
		Workspace workspace("cross-directory-transitive");
		workspace.Write("Upscaling/EncodeTexturesCS.hlsl", R"(#include "../Common/Something.hlsli"

RWTexture2D<float4> Output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	Output[id.xy] = Something();
}
)");
		workspace.Write("Common/Something.hlsli", R"(#include "Nested/Value.hlsli"

float4 Something()
{
	return IncludedValue();
}
)");
		workspace.Write("Common/Nested/Value.hlsli", R"(float4 IncludedValue()
{
	return float4(0.25, 0.5, 0.75, 1.0);
}
)");

		auto recipe = workspace.Recipe();
		recipe.source = workspace.Sources() / "Upscaling" / "EncodeTexturesCS.hlsl";
		recipe.includeRoots = { recipe.source.parent_path() };
		recipe.profile = "cs_5_0";
		recipe.stage = ShaderCacheStage::kCompute;

		const auto primed = PrimeCache(workspace, recipe);
		ShaderCacheRecord record;
		CheckRecordStatus(
			ParseShaderCacheRecord(primed.recordBytes, record),
			RecordStatus::kOk,
			"cross-directory compute record parses");
		Check(record.manifest.includes.size() == 2,
			"manifest records the parent-relative include and its transitive include");

		workspace.Write("Common/Nested/Value.hlsli", R"(float4 IncludedValue()
{
	return float4(1.0, 0.75, 0.5, 0.25);
}
)");
		const auto transitiveStale = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			transitiveStale,
			CacheDisposition::kStale,
			"changed transitive parent-boundary dependency is stale");
		Check(transitiveStale.origin == CompileOrigin::kFreshCompile,
			"changed transitive dependency recompiles");
		Check(!(transitiveStale.bytecode == primed.cold.bytecode),
			"transitive dependency change alters compute bytecode");

		const auto transitiveWarm = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			transitiveWarm,
			CacheDisposition::kHit,
			"recompiled transitive dependency hits");

		workspace.Write("Common/Something.hlsli", R"(#include "Nested/Value.hlsli"

float4 Something()
{
	return IncludedValue().zyxw;
}
)");
		const auto directStale = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			directStale,
			CacheDisposition::kStale,
			"changed parent-relative dependency is stale");
		Check(directStale.origin == CompileOrigin::kFreshCompile,
			"changed parent-relative dependency recompiles");
		Check(!(directStale.bytecode == transitiveStale.bytecode),
			"parent-relative dependency change alters compute bytecode");
	}

	void TestUnknownStageFailsClosed()
	{
		Workspace workspace("unknown-stage");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		auto futureRecord = primed.recordBytes;
		constexpr std::size_t stageOffset = 8 + 4 + 32 * 3;
		futureRecord[stageOffset] = 0xFF;
		WriteAll(primed.cold.recordPath, futureRecord);

		ShaderCacheRecord record;
		CheckRecordStatus(
			ParseShaderCacheRecord(futureRecord, record),
			RecordStatus::kUnknownStage,
			"unknown stage record is rejected");

		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			outcome,
			CacheDisposition::kRejected,
			"unknown stage falls through instead of becoming a hit");
		Check(outcome.succeeded && outcome.origin == CompileOrigin::kFreshCompile,
			"unknown stage recompiles successfully");
		Check(outcome.bytecode == primed.cold.bytecode,
			"unknown stage fallback returns fresh expected bytecode");
		Check(outcome.recordWritten, "unknown stage fallback republishes the record");

		const auto repairedBytes = ReadAll(outcome.recordPath);
		CheckRecordStatus(
			ParseShaderCacheRecord(repairedBytes, record),
			RecordStatus::kOk,
			"unknown stage fallback writes a valid record");
		Check(record.stage == recipe.stage,
			"unknown stage fallback restores the requested stage");
	}

	void TestDefineInvalidation()
	{
		Workspace workspace("defines");
		workspace.WriteDefaultTree();

		auto plain = workspace.Recipe();
		auto tinted = workspace.Recipe();
		tinted.defines.emplace_back("TINT", "1");

		const auto plainCold  = LoadOrCompileShader(plain, workspace.Options());
		const auto tintedCold = LoadOrCompileShader(tinted, workspace.Options());
		Check(plainCold.succeeded && tintedCold.succeeded, "both define sets compile");
		Check(!(plainCold.recordPath == tintedCold.recordPath),
			"different defines address different records");
		Check(!(plainCold.bytecode == tintedCold.bytecode),
			"different defines produce different bytecode");

		const auto plainWarm  = LoadOrCompileShader(plain, workspace.Options());
		const auto tintedWarm = LoadOrCompileShader(tinted, workspace.Options());
		CheckDisposition(plainWarm, CacheDisposition::kHit, "plain variant hits");
		CheckDisposition(tintedWarm, CacheDisposition::kHit, "tinted variant hits");
		Check(plainWarm.bytecode == plainCold.bytecode, "plain hit returns its own payload");
		Check(tintedWarm.bytecode == tintedCold.bytecode, "tinted hit returns its own payload");

		// define order participates in recipe identity
		auto reordered = workspace.Recipe();
		reordered.defines.emplace_back("SECOND", "2");
		reordered.defines.emplace_back("FIRST", "1");
		auto ordered = workspace.Recipe();
		ordered.defines.emplace_back("FIRST", "1");
		ordered.defines.emplace_back("SECOND", "2");
		const auto& identity = GetD3DCompilerIdentity();
		Check(!(ComputeLogicalDigest(EncodeShaderRecipe(reordered, identity))
				== ComputeLogicalDigest(EncodeShaderRecipe(ordered, identity))),
			"define order is preserved in the logical digest");
	}

	void TestRootSourceInvalidation()
	{
		Workspace workspace("root-source");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto originalSource = ReadAll(recipe.source);
		const auto primed = PrimeCache(workspace, recipe);

		workspace.Write("Root.hlsl", R"(#include "Sub/Wrapper.hlsli"

float4 main() : SV_Target
{
	return Wrapped() * 0.5;
}
)");
		Check(
			ReadAll(recipe.source).size() == originalSource.size(),
			"root mutation preserves the file length");

		const auto stale = LoadOrCompileShader(recipe, workspace.Options());
		Check(stale.succeeded, "recompile after a root edit succeeds");
		CheckDisposition(stale, CacheDisposition::kStale, "edited root invalidates the record");
		Check(!(stale.bytecode == primed.cold.bytecode), "edited root produces new bytecode");
		Check(stale.bytecode == CompileWithPlainCompiler(recipe),
			"recompiled bytecode matches CompileShaderToBlob");

		const auto rehit = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(rehit, CacheDisposition::kHit, "republished record hits");
		Check(rehit.bytecode == stale.bytecode, "republished record returns the new payload");
	}

	void TestTransitiveIncludeInvalidation()
	{
		Workspace workspace("transitive");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto includePath = workspace.Sources() / "Shared.hlsli";
		const auto originalInclude = ReadAll(includePath);
		const auto primed = PrimeCache(workspace, recipe);

		workspace.Write("Shared.hlsli", R"(float4 SharedValue()
{
#ifdef TINT
	return float4(0.25, 0.5, 0.75, 1.0);
#else
	return float4(0.0, 0.0, 1.0, 1.0);
#endif
}
)");
		Check(
			ReadAll(includePath).size() == originalInclude.size(),
			"include mutation preserves the file length");

		const auto stale = LoadOrCompileShader(recipe, workspace.Options());
		Check(stale.succeeded, "recompile after a transitive include edit succeeds");
		CheckDisposition(stale, CacheDisposition::kStale, "transitive include edit invalidates");
		Check(!(stale.bytecode == primed.cold.bytecode),
			"transitive include edit produces new bytecode");
	}

	void TestIncludeShadowing()
	{
		Workspace workspace("shadowing");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		ShaderCacheRecord record;
		CheckRecordStatus(
			ParseShaderCacheRecord(primed.recordBytes, record),
			RecordStatus::kOk,
			"record parses before shadowing");

		bool sawFailedHigherPriorityProbe = false;
		for (const auto& include : record.manifest.includes) {
			if (include.requestedName != "Shared.hlsli")
				continue;
			Check(include.probes.size() == 2, "shared include recorded both candidates");
			if (include.probes.size() == 2) {
				sawFailedHigherPriorityProbe =
					include.probes.front().status == ProbeStatus::kMissing
					&& include.probes.back().status == ProbeStatus::kSuccess;
			}
		}
		Check(sawFailedHigherPriorityProbe,
			"the failed parent-relative candidate is recorded before the base-root hit");

		workspace.Write("Sub/Shared.hlsli", R"(float4 SharedValue()
{
	return float4(0.0, 1.0, 0.0, 1.0);
}
)");

		const auto shadowed = LoadOrCompileShader(recipe, workspace.Options());
		Check(shadowed.succeeded, "recompile after shadowing succeeds");
		CheckDisposition(shadowed, CacheDisposition::kStale, "a new higher priority file invalidates");
		Check(!(shadowed.bytecode == primed.cold.bytecode),
			"the shadowing include changes the bytecode");
		Check(shadowed.bytecode == CompileWithPlainCompiler(recipe),
			"shadowed recompile matches CompileShaderToBlob");
	}

	void TestDirectoryProbeFallback()
	{
		Workspace workspace("directory-probe");
		workspace.WriteDefaultTree();
		std::filesystem::create_directory(workspace.Sources() / "Sub" / "Shared.hlsli");
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);
		Check(
			primed.cold.bytecode == CompileWithPlainCompiler(recipe),
			"a directory candidate falls back exactly like CompileShaderToBlob");

		ShaderCacheRecord record;
		CheckRecordStatus(
			ParseShaderCacheRecord(primed.recordBytes, record),
			RecordStatus::kOk,
			"directory-probe record parses");
		bool recordedDirectoryAsMissing = false;
		for (const auto& include : record.manifest.includes) {
			if (include.requestedName == "Shared.hlsli"
				&& include.probes.size() == 2) {
				recordedDirectoryAsMissing =
					include.probes.front().status == ProbeStatus::kMissing
					&& include.probes.back().status == ProbeStatus::kSuccess;
			}
		}
		Check(
			recordedDirectoryAsMissing,
			"a directory candidate is recorded before the root fallback");
	}

	void TestOpenFailureProbeFallback()
	{
		Workspace workspace("open-failure-probe");
		workspace.WriteDefaultTree();
		workspace.Write("Sub/Shared.hlsli", R"(float4 SharedValue()
{
	return float4(0.0, 1.0, 0.0, 1.0);
}
)");
		const auto recipe = workspace.Recipe();
		ScopedHandle lock(CreateFileW(
			(workspace.Sources() / "Sub" / "Shared.hlsli").c_str(),
			GENERIC_READ,
			0,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr));
		if (!lock.Valid()) {
			Fail("exclusive include lock opens");
			return;
		}

		const auto oracle = CompileWithPlainCompiler(recipe);
		const auto cold = LoadOrCompileShader(recipe, workspace.Options());
		if (!cold.succeeded) {
			Fail("an unopenable candidate falls through");
			return;
		}
		Check(
			cold.bytecode == oracle,
			"open-failure fallback is byte-identical to CompileShaderToBlob");

		ShaderCacheRecord record;
		CheckRecordStatus(
			ParseShaderCacheRecord(ReadAll(cold.recordPath), record),
			RecordStatus::kOk,
			"open-failure record parses");
		bool recordedOpenFailureAsMissing = false;
		for (const auto& include : record.manifest.includes) {
			if (include.requestedName == "Shared.hlsli"
				&& include.probes.size() == 2) {
				recordedOpenFailureAsMissing =
					include.probes.front().status == ProbeStatus::kMissing
					&& include.probes.back().status == ProbeStatus::kSuccess;
			}
		}
		Check(
			recordedOpenFailureAsMissing,
			"an open failure is recorded before the root fallback");

		lock.Reset();
		const auto shadowed = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(
			shadowed,
			CacheDisposition::kStale,
			"a newly readable candidate invalidates the fallback record");
		Check(!(shadowed.bytecode == cold.bytecode), "the readable candidate changes bytecode");
		Check(
			shadowed.bytecode == CompileWithPlainCompiler(recipe),
			"the readable candidate matches CompileShaderToBlob");
	}

	void TestCorruptMagic()
	{
		Workspace workspace("corrupt-magic");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		auto corrupted = primed.recordBytes;
		corrupted[0] = static_cast<std::uint8_t>(corrupted[0] ^ 0xFF);
		Check(!(corrupted == primed.recordBytes), "magic mutation changed the bytes");
		WriteAll(primed.cold.recordPath, corrupted);

		ShaderCacheRecord parsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(corrupted, parsed),
			RecordStatus::kBadMagic,
			"corrupt magic is rejected by the parser");

		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(outcome, CacheDisposition::kRejected, "corrupt magic falls through");
		Check(outcome.succeeded, "corrupt magic still compiles");
		Check(outcome.bytecode == primed.cold.bytecode, "fallback compile returns the same bytecode");
		Check(outcome.recordWritten, "fallback compile republishes the record");
		CheckDisposition(
			LoadOrCompileShader(recipe, workspace.Options()),
			CacheDisposition::kHit,
			"republished record hits again");
	}

	void TestCorruptPayload()
	{
		Workspace workspace("corrupt-payload");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		auto corrupted = primed.recordBytes;
		corrupted.back() = static_cast<std::uint8_t>(corrupted.back() ^ 0x5A);
		Check(!(corrupted == primed.recordBytes), "payload mutation changed the bytes");
		WriteAll(primed.cold.recordPath, corrupted);

		ShaderCacheRecord parsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(corrupted, parsed),
			RecordStatus::kPayloadDigestMismatch,
			"a flipped payload byte is rejected");

		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(outcome, CacheDisposition::kRejected, "corrupt payload falls through");
		Check(outcome.succeeded && outcome.bytecode == primed.cold.bytecode,
			"corrupt payload recompiles to the correct bytecode");
	}

	void TestCorruptManifest()
	{
		Workspace workspace("corrupt-manifest");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		ShaderCacheRecord record;
		ParseShaderCacheRecord(primed.recordBytes, record);

		const auto manifestStart =
			ManifestLengthOffset(recipe.profile) + 8 + 4 + 8 + 32;
		auto corrupted = primed.recordBytes;
		corrupted[manifestStart + 8] =
			static_cast<std::uint8_t>(corrupted[manifestStart + 8] ^ 0x7F);
		Check(!(corrupted == primed.recordBytes), "manifest mutation changed the bytes");
		WriteAll(primed.cold.recordPath, corrupted);

		ShaderCacheRecord parsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(corrupted, parsed),
			RecordStatus::kManifestDigestMismatch,
			"a tampered manifest is rejected");

		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(outcome, CacheDisposition::kRejected, "tampered manifest falls through");
		Check(outcome.succeeded && outcome.bytecode == primed.cold.bytecode,
			"tampered manifest recompiles to the correct bytecode");
	}

	void TestInvalidLocator()
	{
		Workspace workspace("invalid-locator");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		auto corrupted = primed.recordBytes;
		const auto manifestLengthOffset = ManifestLengthOffset(recipe.profile);
		const auto manifestStart = manifestLengthOffset + 8 + 4 + 8 + 32;
		const auto manifestLength =
			static_cast<std::size_t>(ReadU64(corrupted, manifestLengthOffset));
		const auto rootLocatorStart = manifestStart + 4;
		corrupted[rootLocatorStart] = 0xFF;

		const auto manifest = std::span<const std::uint8_t>(
			corrupted.data() + manifestStart,
			manifestLength);
		const auto dependencyDigest =
			cs::sha256::Sha256Compute(manifest.data(), manifest.size());
		const auto recipeBytes =
			EncodeShaderRecipe(recipe, GetD3DCompilerIdentity());
		const auto recipeDigest =
			ComputeFullRecipeDigest(recipeBytes, dependencyDigest);
		PatchDigest(corrupted, 8 + 4 + 32, recipeDigest);
		PatchDigest(corrupted, 8 + 4 + 32 + 32, dependencyDigest);
		Check(!(corrupted == primed.recordBytes), "locator mutation changed the bytes");
		WriteAll(primed.cold.recordPath, corrupted);

		ShaderCacheRecord parsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(corrupted, parsed),
			RecordStatus::kMalformedManifest,
			"an invalid UTF-8 locator is rejected");

		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(outcome, CacheDisposition::kRejected, "invalid locator falls through");
		Check(outcome.succeeded && outcome.bytecode == primed.cold.bytecode,
			"invalid locator recompiles to the correct bytecode");
	}

	void TestTruncation()
	{
		Workspace workspace("truncation");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		auto truncated = primed.recordBytes;
		truncated.resize(truncated.size() / 2);
		Check(!(truncated == primed.recordBytes), "truncation changed the bytes");
		WriteAll(primed.cold.recordPath, truncated);

		ShaderCacheRecord parsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(truncated, parsed),
			RecordStatus::kTruncated,
			"a halved record is rejected");

		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(outcome, CacheDisposition::kRejected, "truncated record falls through");
		Check(outcome.succeeded && outcome.bytecode == primed.cold.bytecode,
			"truncated record recompiles to the correct bytecode");

		auto header = primed.recordBytes;
		header.resize(4);
		ShaderCacheRecord headerParsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(header, headerParsed),
			RecordStatus::kTruncated,
			"a record shorter than the magic is rejected");
		CheckRecordStatus(
			ParseShaderCacheRecord({}, headerParsed),
			RecordStatus::kTruncated,
			"an empty record is rejected");

		auto trailing = primed.recordBytes;
		trailing.push_back(0);
		ShaderCacheRecord trailingParsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(trailing, trailingParsed),
			RecordStatus::kTrailingBytes,
			"appended bytes are rejected");
	}

	void TestHostileLengths()
	{
		Workspace workspace("hostile-lengths");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		auto hostilePayload = primed.recordBytes;
		PatchU64(
			hostilePayload,
			PayloadLengthOffset(recipe.profile),
			0xFFFFFFFFFFFFFFFFull);
		Check(!(hostilePayload == primed.recordBytes), "payload length mutation changed the bytes");
		ShaderCacheRecord parsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(hostilePayload, parsed),
			RecordStatus::kLimitExceeded,
			"a hostile payload length is rejected before allocation");

		auto hostileManifest = primed.recordBytes;
		PatchU64(
			hostileManifest,
			ManifestLengthOffset(recipe.profile),
			0xFFFFFFFFFFFFFFFFull);
		CheckRecordStatus(
			ParseShaderCacheRecord(hostileManifest, parsed),
			RecordStatus::kLimitExceeded,
			"a hostile manifest length is rejected before allocation");

		auto zeroPayload = primed.recordBytes;
		PatchU64(zeroPayload, PayloadLengthOffset(recipe.profile), 0);
		CheckRecordStatus(
			ParseShaderCacheRecord(zeroPayload, parsed),
			RecordStatus::kLimitExceeded,
			"an empty payload is rejected");

		WriteAll(primed.cold.recordPath, hostilePayload);
		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(outcome, CacheDisposition::kRejected, "hostile length falls through");
		Check(outcome.succeeded && outcome.bytecode == primed.cold.bytecode,
			"hostile length recompiles to the correct bytecode");
	}

	void TestSwappedRecord()
	{
		Workspace workspace("swapped");
		workspace.WriteDefaultTree();

		auto plain = workspace.Recipe();
		auto tinted = workspace.Recipe();
		tinted.defines.emplace_back("TINT", "1");

		const auto plainPrimed  = PrimeCache(workspace, plain);
		const auto tintedPrimed = PrimeCache(workspace, tinted);
		Check(!(plainPrimed.cold.bytecode == tintedPrimed.cold.bytecode),
			"the two variants differ");

		Check(!(tintedPrimed.recordBytes == plainPrimed.recordBytes),
			"swapping in another record changes the bytes");
		WriteAll(plainPrimed.cold.recordPath, tintedPrimed.recordBytes);

		ShaderCacheRecord parsed;
		CheckRecordStatus(
			ParseShaderCacheRecord(tintedPrimed.recordBytes, parsed),
			RecordStatus::kOk,
			"the swapped record is internally valid");

		const auto outcome = LoadOrCompileShader(plain, workspace.Options());
		CheckDisposition(outcome, CacheDisposition::kRejected,
			"a valid record for another request is rejected");
		Check(outcome.succeeded, "swapped record still compiles");
		Check(outcome.bytecode == plainPrimed.cold.bytecode,
			"swapped record does not leak the other variant's bytecode");
	}

	void TestFailedCompileIsNotCached()
	{
		Workspace workspace("failed-compile");
		workspace.WriteDefaultTree();
		workspace.Write("Root.hlsl", "this is not HLSL\n");

		const auto recipe  = workspace.Recipe();
		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		Check(!outcome.succeeded, "a broken shader fails");
		Check(!outcome.error.empty(), "a broken shader reports an error");
		Check(!outcome.recordWritten, "a failed compile publishes nothing");
		Check(!std::filesystem::exists(outcome.recordPath), "no record file exists after a failure");
		Check(CountTemporaryFiles(workspace.CacheRoot()) == 0, "no temporary file leaked");
	}

	void TestUnwritableDestinationKeepsResult()
	{
		Workspace workspace("unwritable-destination");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();

		const auto probe = LoadOrCompileShader(recipe, ShaderCacheOptions{ workspace.CacheRoot() });
		Check(probe.succeeded, "probe compile succeeded");
		const auto blockedDirectory = probe.recordPath.parent_path();
		std::filesystem::remove_all(workspace.CacheRoot());
		std::filesystem::create_directories(blockedDirectory.parent_path());
		WriteAll(blockedDirectory, { 0x00 });

		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		Check(outcome.succeeded, "a publication failure leaves the compile succeeding");
		Check(outcome.bytecode == probe.bytecode, "a publication failure preserves the bytecode");
		Check(!outcome.recordWritten, "a publication failure is reported");
		Check(!outcome.cacheNote.empty(), "a publication failure explains itself");
		Check(CountTemporaryFiles(workspace.CacheRoot()) == 0, "no temporary file leaked");
	}

	void TestConcurrentWriters()
	{
		Workspace workspace("concurrent");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		ShaderCacheRecord record;
		if (ParseShaderCacheRecord(primed.recordBytes, record) != RecordStatus::kOk) {
			Fail("could not parse the pristine record");
			return;
		}

		constexpr int             kWriters = 6;
		constexpr int             kRounds  = 24;
		std::vector<std::vector<std::uint8_t>> encoded(kWriters);
		for (int writer = 0; writer < kWriters; ++writer) {
			auto variant = record;
			variant.payload.back() = static_cast<std::uint8_t>(writer);
			if (!SerializeShaderCacheRecord(variant, encoded[writer])) {
				Fail("could not serialize a writer's record");
				return;
			}
		}

		std::atomic<int>  writeFailures{ 0 };
		std::atomic<int>  tornReads{ 0 };
		std::atomic<bool> running{ true };
		std::barrier      start(kWriters + 1);
		std::mutex        noteLock;
		std::string       firstFailure;

		std::vector<std::thread> writers;
		for (int writer = 0; writer < kWriters; ++writer) {
			writers.emplace_back([&, writer] {
				start.arrive_and_wait();
				for (int round = 0; round < kRounds; ++round) {
					std::string error;
					if (!WriteRecordAtomically(primed.cold.recordPath, encoded[writer], error)) {
						writeFailures.fetch_add(1);
						const std::scoped_lock lock(noteLock);
						if (firstFailure.empty())
							firstFailure = error;
					}
				}
			});
		}

		std::thread reader([&] {
			start.arrive_and_wait();
			while (running.load()) {
				std::vector<std::uint8_t> bytes;
				if (ReadFileBytes(primed.cold.recordPath, kMaxRecordBytes, bytes)
					!= FileReadStatus::kOk) {
					continue;
				}
				ShaderCacheRecord seen;
				if (ParseShaderCacheRecord(bytes, seen) != RecordStatus::kOk)
					tornReads.fetch_add(1);
			}
		});

		for (auto& writer : writers)
			writer.join();
		running.store(false);
		reader.join();

		Check(tornReads.load() == 0, "no reader ever observed a partial record");

		const auto finalBytes = ReadAll(primed.cold.recordPath);
		ShaderCacheRecord finalRecord;
		CheckRecordStatus(
			ParseShaderCacheRecord(finalBytes, finalRecord),
			RecordStatus::kOk,
			"the surviving record is complete");
		bool matchedOne = false;
		for (const auto& candidate : encoded)
			matchedOne = matchedOne || candidate == finalBytes;
		Check(matchedOne, "the surviving record is exactly one writer's record");
		Check(CountTemporaryFiles(workspace.CacheRoot()) == 0, "no temporary file leaked");
		std::printf(
			"  concurrent writers: %d publications, %d failed (%s)\n",
			kWriters * kRounds,
			writeFailures.load(),
			firstFailure.empty() ? "none" : firstFailure.c_str());
	}

	void TestConcurrentCompilers()
	{
		Workspace workspace("concurrent-compile");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();

		constexpr int                          kThreads = 6;
		std::vector<std::vector<std::uint8_t>> results(kThreads);
		std::vector<int>                       succeeded(kThreads, 0);
		std::barrier                           start(kThreads);
		std::vector<std::thread>               threads;
		for (int index = 0; index < kThreads; ++index) {
			threads.emplace_back([&, index] {
				start.arrive_and_wait();
				const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
				succeeded[index]   = outcome.succeeded ? 1 : 0;
				results[index]     = outcome.bytecode;
			});
		}
		for (auto& thread : threads)
			thread.join();

		for (int index = 0; index < kThreads; ++index) {
			Check(succeeded[index] == 1, "every concurrent compile succeeded");
			Check(results[index] == results[0], "every concurrent compile agrees on the bytecode");
		}
		Check(CountTemporaryFiles(workspace.CacheRoot()) == 0, "no temporary file leaked");

		const auto outcome = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(outcome, CacheDisposition::kHit, "the record left behind is usable");
		Check(outcome.bytecode == results[0], "the surviving record holds the agreed bytecode");
	}

	void TestRecompileModeRepublishes()
	{
		Workspace workspace("recompile-mode");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		const auto forced =
			LoadOrCompileShader(recipe, workspace.Options(), CacheMode::kRecompile);
		Check(forced.succeeded, "forced recompile succeeds");
		CheckDisposition(forced, CacheDisposition::kBypassed, "forced recompile bypasses the lookup");
		Check(forced.origin == CompileOrigin::kFreshCompile, "forced recompile is fresh");
		Check(forced.bytecode == primed.cold.bytecode, "forced recompile agrees with the cache");
		Check(forced.recordWritten, "forced recompile republishes");
	}

	void TestMemoizedRevalidation()
	{
		Workspace workspace("revalidation-memo");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		RevalidationContext context;
		auto                options = workspace.Options();
		options.revalidation        = &context;

		const auto first = LoadOrCompileShader(recipe, options);
		CheckDisposition(first, CacheDisposition::kHit, "the first memoized lookup hits");
		Check(context.ObservedPaths() == 4,
			"the memo observed the root and all three probed candidates, got "
				+ std::to_string(context.ObservedPaths()));
		Check(context.Reads() == context.ObservedPaths(), "each observed path was read once");

		const auto reads  = context.Reads();
		const auto second = LoadOrCompileShader(recipe, options);
		CheckDisposition(second, CacheDisposition::kHit, "the second memoized lookup hits");
		Check(context.Reads() == reads, "a repeated lookup re-reads nothing");

		// mid-batch edits cannot split the snapshot
		workspace.Write("Shared.hlsli", R"(float4 SharedValue()
{
	return float4(0.0, 1.0, 0.0, 1.0);
}
)");
		const auto frozen = LoadOrCompileShader(recipe, options);
		CheckDisposition(frozen, CacheDisposition::kHit, "the snapshot outlives the edit");
		Check(frozen.bytecode == primed.cold.bytecode, "the snapshot returns the recorded payload");

		const auto fresh = LoadOrCompileShader(recipe, workspace.Options());
		CheckDisposition(fresh, CacheDisposition::kStale, "a context-free lookup sees the edit");
	}

	void TestMemoizedRevalidationIsConcurrent()
	{
		Workspace workspace("revalidation-memo-threads");
		workspace.WriteDefaultTree();
		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);

		RevalidationContext context;
		auto                options = workspace.Options();
		options.revalidation        = &context;

		constexpr int                          kThreads = 8;
		std::vector<std::vector<std::uint8_t>> results(kThreads);
		std::vector<int>                       hits(kThreads, 0);
		std::barrier                           start(kThreads);
		std::vector<std::thread>               threads;
		for (int index = 0; index < kThreads; ++index) {
			threads.emplace_back([&, index] {
				start.arrive_and_wait();
				const auto outcome = LoadOrCompileShader(recipe, options);
				hits[index]        = outcome.disposition == CacheDisposition::kHit ? 1 : 0;
				results[index]     = outcome.bytecode;
			});
		}
		for (auto& thread : threads)
			thread.join();

		for (int index = 0; index < kThreads; ++index) {
			Check(hits[index] == 1, "every concurrent memoized lookup hit");
			Check(results[index] == primed.cold.bytecode,
				"every concurrent memoized lookup returned the recorded payload");
		}
		Check(context.Reads() == context.ObservedPaths(),
			"concurrent lookups read each path exactly once, got "
				+ std::to_string(context.Reads()) + " reads over "
				+ std::to_string(context.ObservedPaths()) + " paths");
	}

	void TestNonAsciiIncludeName()
	{
		Workspace workspace("non-ascii");
		workspace.WriteDefaultTree();

		// match the filename to FXC's raw directive bytes
		const std::filesystem::path wideName = L"Gr\u00f6\u00dfe.hlsli";
		std::string                 narrowName;
		try {
			narrowName = wideName.string();
		} catch (...) {
			std::printf("  skipped: the active code page cannot spell the test include\n");
			return;
		}
		if (narrowName.empty()) {
			std::printf("  skipped: the active code page cannot spell the test include\n");
			return;
		}

		workspace.Write(wideName, R"(float4 Tinted()
{
	return float4(0.125, 0.25, 0.375, 1.0);
}
)");
		workspace.Write(
			"Root.hlsl",
			"#include \"" + narrowName + "\"\n\nfloat4 main() : SV_Target\n{\n\treturn Tinted();\n}\n");

		const auto recipe = workspace.Recipe();
		const auto primed = PrimeCache(workspace, recipe);
		Check(primed.cold.bytecode == CompileWithPlainCompiler(recipe),
			"a non-ASCII include resolves exactly like the plain compiler");

		ShaderCacheRecord record;
		CheckRecordStatus(
			ParseShaderCacheRecord(primed.recordBytes, record),
			RecordStatus::kOk,
			"the record round-trips a non-ASCII dependency");
		Check(record.manifest.includes.size() == 1, "the non-ASCII include is recorded");
	}

	struct TestCase
	{
		const char* name;
		void (*run)();
	};

	constexpr TestCase kTests[]{
		{ "compiler-identity", &TestCompilerIdentity },
		{ "compiler-identity-across-processes", &TestCompilerIdentityAcrossProcesses },
		{ "compiler-identity-reset", &TestCompilerIdentityReset },
		{ "compiler-identity-first-run", &TestCompilerIdentityFirstRun },
		{ "cache-miss-observable", &TestCacheMissIsObservable },
		{ "compiler-identity-hash-fallback", &TestCompilerIdentityHashFallback },
		{ "cache-root-path", &TestCacheRootPath },
		{ "hit-matches-fresh-compile", &TestHitMatchesFreshCompile },
		{ "compute-stage-encoding", &TestComputeStageEncoding },
		{ "cross-directory-transitive-invalidation", &TestCrossDirectoryTransitiveInvalidation },
		{ "unknown-stage-fails-closed", &TestUnknownStageFailsClosed },
		{ "define-invalidation", &TestDefineInvalidation },
		{ "root-source-invalidation", &TestRootSourceInvalidation },
		{ "transitive-include-invalidation", &TestTransitiveIncludeInvalidation },
		{ "include-shadowing", &TestIncludeShadowing },
		{ "directory-probe-fallback", &TestDirectoryProbeFallback },
		{ "open-failure-probe-fallback", &TestOpenFailureProbeFallback },
		{ "memoized-revalidation", &TestMemoizedRevalidation },
		{ "memoized-revalidation-concurrency", &TestMemoizedRevalidationIsConcurrent },
		{ "non-ascii-include-name", &TestNonAsciiIncludeName },
		{ "corrupt-magic", &TestCorruptMagic },
		{ "corrupt-payload", &TestCorruptPayload },
		{ "corrupt-manifest", &TestCorruptManifest },
		{ "invalid-locator", &TestInvalidLocator },
		{ "truncation", &TestTruncation },
		{ "hostile-lengths", &TestHostileLengths },
		{ "swapped-record", &TestSwappedRecord },
		{ "failed-compile-not-cached", &TestFailedCompileIsNotCached },
		{ "unwritable-destination-keeps-result", &TestUnwritableDestinationKeepsResult },
		{ "concurrent-writers", &TestConcurrentWriters },
		{ "concurrent-compilers", &TestConcurrentCompilers },
		{ "recompile-mode", &TestRecompileModeRepublishes }
	};
}

int main(int argc, char** argv)
{
	if (argc == 3 && argv[1] == kEmitIdentityFlag)
		return EmitCompilerIdentity(argv[2]);

	const std::string_view selected =
		argc == 3 && std::string_view(argv[1]) == "--test"
		? std::string_view(argv[2])
		: std::string_view{};
	bool found = selected.empty();
	for (const auto& test : kTests) {
		if (!selected.empty() && selected != test.name)
			continue;
		found = true;
		g_currentTest              = test.name;
		const int  before          = g_failures;
		const auto start           = std::chrono::steady_clock::now();
		test.run();
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start);
		std::printf(
			"%-38s %s (%lld ms)\n",
			test.name,
			g_failures == before ? "ok" : "FAILED",
			static_cast<long long>(elapsed.count()));
	}

	if (!found) {
		std::printf("Unknown shader cache test: %.*s\n",
			static_cast<int>(selected.size()),
			selected.data());
		return 2;
	}
	if (g_failures == 0)
		std::printf("ShaderCache passed\n");
	else
		std::printf("%d shader cache assertion(s) failed\n", g_failures);
	return g_failures ? 1 : 0;
}
