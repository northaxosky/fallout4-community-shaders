#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include "Utils/StreamlineModule.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
	namespace fs = std::filesystem;
	using sl::security::ProjectFileRole;
	using sl::security::ProjectLoadResult;
	using sl::security::ProjectTrustConfig;
	using sl::security::TrustFailure;

	constexpr std::string_view kReleaseId = "fo4cs-streamline-bootstrap-test-1";
	constexpr std::uint32_t kKeyId = 0x534C0001;
	constexpr wchar_t kFixtureSentinel[] =
		L"FO4CS_STREAMLINE_MODULE_FIXTURE_ATTACHED";

	int failures{};
	std::string_view currentTest;

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed [" << currentTest << "] at line "
					  << a_line << ": " << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	[[noreturn]] void Throw(std::string_view a_message)
	{
		throw std::runtime_error(std::string(a_message));
	}

	void Require(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			Throw(a_message);
	}

	void RequireNt(NTSTATUS a_status, std::string_view a_message)
	{
		if (a_status < 0)
			Throw(a_message);
	}

	class ScopedModule
	{
	public:
		explicit ScopedModule(HMODULE a_module = nullptr) noexcept :
			_module(a_module)
		{}

		~ScopedModule()
		{
			Reset();
		}

		ScopedModule(const ScopedModule&) = delete;
		ScopedModule& operator=(const ScopedModule&) = delete;

		void Reset() noexcept
		{
			if (_module) {
				FreeLibrary(_module);
				_module = nullptr;
			}
		}

	private:
		HMODULE _module;
	};

	struct AlgorithmOwner
	{
		~AlgorithmOwner()
		{
			if (value)
				BCryptCloseAlgorithmProvider(value, 0);
		}

		BCRYPT_ALG_HANDLE value{};
	};

	struct HashOwner
	{
		~HashOwner()
		{
			if (value)
				BCryptDestroyHash(value);
		}

		BCRYPT_HASH_HANDLE value{};
	};

	struct KeyOwner
	{
		~KeyOwner()
		{
			if (value)
				BCryptDestroyKey(value);
		}

		BCRYPT_KEY_HANDLE value{};
	};

	std::vector<std::uint8_t> ReadBytes(const fs::path& a_path)
	{
		std::ifstream file(a_path, std::ios::binary | std::ios::ate);
		Require(file.is_open(), "failed to open test input");
		const auto end = file.tellg();
		Require(end >= 0, "failed to determine test input size");
		const auto size = static_cast<std::uintmax_t>(end);
		Require(
			size <= static_cast<std::uintmax_t>(
				(std::numeric_limits<std::streamsize>::max)()),
			"test input is too large");
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
		file.seekg(0);
		if (!bytes.empty()) {
			file.read(
				reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			Require(file.good(), "failed to read test input");
		}
		return bytes;
	}

	void WriteBytes(const fs::path& a_path, std::span<const std::uint8_t> a_bytes)
	{
		std::ofstream file(a_path, std::ios::binary | std::ios::trunc);
		Require(file.is_open(), "failed to open test output");
		if (!a_bytes.empty()) {
			file.write(
				reinterpret_cast<const char*>(a_bytes.data()),
				static_cast<std::streamsize>(a_bytes.size()));
		}
		Require(file.good(), "failed to write test output");
	}

	std::array<std::uint8_t, 32> Sha256(
		std::span<const std::uint8_t> a_bytes)
	{
		AlgorithmOwner algorithm;
		RequireNt(
			BCryptOpenAlgorithmProvider(
				&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
			"failed to open SHA-256 provider");

		DWORD objectSize{};
		DWORD copied{};
		RequireNt(
			BCryptGetProperty(
				algorithm.value, BCRYPT_OBJECT_LENGTH,
				reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
				&copied, 0),
			"failed to query SHA-256 object size");
		Require(copied == sizeof(objectSize), "invalid SHA-256 object size");

		std::vector<std::uint8_t> object(objectSize);
		HashOwner hash;
		RequireNt(
			BCryptCreateHash(
				algorithm.value, &hash.value, object.data(), objectSize,
				nullptr, 0, 0),
			"failed to create SHA-256 hash");
		Require(
			a_bytes.size() <= (std::numeric_limits<ULONG>::max)(),
			"SHA-256 test input is too large");
		if (!a_bytes.empty()) {
			RequireNt(
				BCryptHashData(
					hash.value, const_cast<PUCHAR>(a_bytes.data()),
					static_cast<ULONG>(a_bytes.size()), 0),
				"failed to hash test input");
		}
		std::array<std::uint8_t, 32> digest{};
		RequireNt(
			BCryptFinishHash(
				hash.value, digest.data(),
				static_cast<ULONG>(digest.size()), 0),
			"failed to finish SHA-256 hash");
		return digest;
	}

	class EphemeralKey
	{
	public:
		EphemeralKey()
		{
			RequireNt(
				BCryptOpenAlgorithmProvider(
					&_algorithm.value, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0),
				"failed to open ECDSA P-256 provider");
			RequireNt(
				BCryptGenerateKeyPair(
					_algorithm.value, &_key.value, 256, 0),
				"failed to generate ECDSA P-256 key");
			RequireNt(
				BCryptFinalizeKeyPair(_key.value, 0),
				"failed to finalize ECDSA P-256 key");

			DWORD blobSize{};
			RequireNt(
				BCryptExportKey(
					_key.value, nullptr, BCRYPT_ECCPUBLIC_BLOB,
					nullptr, 0, &blobSize, 0),
				"failed to query ECDSA public key size");
			std::vector<std::uint8_t> blob(blobSize);
			RequireNt(
				BCryptExportKey(
					_key.value, nullptr, BCRYPT_ECCPUBLIC_BLOB,
					blob.data(), blobSize, &blobSize, 0),
				"failed to export ECDSA public key");

			BCRYPT_ECCKEY_BLOB header{};
			Require(
				blob.size() >= sizeof(header),
				"ECDSA public key blob is truncated");
			std::memcpy(&header, blob.data(), sizeof(header));
			Require(
				header.dwMagic == BCRYPT_ECDSA_PUBLIC_P256_MAGIC &&
					header.cbKey == 32 &&
					blob.size() == sizeof(header) + publicKey.size(),
				"unexpected ECDSA public key blob");
			std::copy_n(
				blob.begin() + static_cast<std::ptrdiff_t>(sizeof(header)),
				publicKey.size(), publicKey.begin());
		}

		EphemeralKey(const EphemeralKey&) = delete;
		EphemeralKey& operator=(const EphemeralKey&) = delete;

		[[nodiscard]] std::array<std::uint8_t, 64> PublicKey() const
		{
			return publicKey;
		}

		[[nodiscard]] std::array<std::uint8_t, 64> Sign(
			std::span<const std::uint8_t> a_bytes) const
		{
			const auto digest = Sha256(a_bytes);
			DWORD signatureSize{};
			RequireNt(
				BCryptSignHash(
					_key.value, nullptr,
					const_cast<PUCHAR>(digest.data()),
					static_cast<ULONG>(digest.size()),
					nullptr, 0, &signatureSize, 0),
				"failed to query ECDSA signature size");
			Require(signatureSize == 64, "unexpected ECDSA signature size");
			std::array<std::uint8_t, 64> signature{};
			RequireNt(
				BCryptSignHash(
					_key.value, nullptr,
					const_cast<PUCHAR>(digest.data()),
					static_cast<ULONG>(digest.size()),
					signature.data(), static_cast<ULONG>(signature.size()),
					&signatureSize, 0),
				"failed to sign project manifest");
			Require(signatureSize == signature.size(), "truncated ECDSA signature");
			return signature;
		}

	private:
		AlgorithmOwner _algorithm;
		KeyOwner _key;
		std::array<std::uint8_t, 64> publicKey{};
	};

	void AppendU16(std::vector<std::uint8_t>& a_bytes, std::uint16_t a_value)
	{
		a_bytes.push_back(static_cast<std::uint8_t>(a_value));
		a_bytes.push_back(static_cast<std::uint8_t>(a_value >> 8));
	}

	void AppendU32(std::vector<std::uint8_t>& a_bytes, std::uint32_t a_value)
	{
		for (unsigned shift = 0; shift != 32; shift += 8)
			a_bytes.push_back(static_cast<std::uint8_t>(a_value >> shift));
	}

	void AppendU64(std::vector<std::uint8_t>& a_bytes, std::uint64_t a_value)
	{
		for (unsigned shift = 0; shift != 64; shift += 8)
			a_bytes.push_back(static_cast<std::uint8_t>(a_value >> shift));
	}

	void StoreU32(
		std::vector<std::uint8_t>& a_bytes,
		std::size_t a_offset,
		std::uint32_t a_value)
	{
		Require(a_offset + 4 <= a_bytes.size(), "manifest patch is out of range");
		for (unsigned shift = 0; shift != 32; shift += 8)
			a_bytes[a_offset++] = static_cast<std::uint8_t>(a_value >> shift);
	}

	struct ManifestEntry
	{
		std::string_view basename;
		ProjectFileRole role;
		fs::path path;
	};

	std::vector<std::uint8_t> BuildManifest(
		std::string_view a_releaseId,
		std::uint32_t a_keyId,
		std::span<const ManifestEntry> a_entries)
	{
		constexpr std::array<std::uint8_t, 8> magic{
			'S', 'L', 'P', 'M', 'A', 'N', '0', '1'
		};
		Require(
			a_releaseId.size() <= (std::numeric_limits<std::uint32_t>::max)(),
			"release ID is too large");
		Require(
			a_entries.size() <= (std::numeric_limits<std::uint32_t>::max)(),
			"manifest has too many entries");

		std::vector<std::uint8_t> bytes(magic.begin(), magic.end());
		AppendU16(bytes, 1);
		AppendU16(bytes, 32);
		AppendU32(bytes, 0);
		AppendU32(bytes, static_cast<std::uint32_t>(a_entries.size()));
		AppendU32(bytes, static_cast<std::uint32_t>(a_releaseId.size()));
		AppendU32(bytes, a_keyId);
		AppendU32(bytes, 0);
		bytes.insert(bytes.end(), a_releaseId.begin(), a_releaseId.end());

		for (const auto& entry : a_entries) {
			Require(
				entry.basename.size() <=
					(std::numeric_limits<std::uint16_t>::max)(),
				"manifest basename is too large");
			const auto fileBytes = ReadBytes(entry.path);
			const auto digest = Sha256(fileBytes);
			AppendU16(bytes, static_cast<std::uint16_t>(entry.basename.size()));
			bytes.push_back(static_cast<std::uint8_t>(entry.role));
			bytes.push_back(0);
			AppendU64(bytes, static_cast<std::uint64_t>(fileBytes.size()));
			bytes.insert(bytes.end(), digest.begin(), digest.end());
			bytes.insert(
				bytes.end(), entry.basename.begin(), entry.basename.end());
		}
		Require(
			bytes.size() <= (std::numeric_limits<std::uint32_t>::max)(),
			"manifest is too large");
		StoreU32(bytes, 12, static_cast<std::uint32_t>(bytes.size()));
		return bytes;
	}

	void CopyFile(const fs::path& a_source, const fs::path& a_destination)
	{
		std::error_code error;
		fs::copy_file(
			a_source, a_destination,
			fs::copy_options::overwrite_existing, error);
		if (error)
			throw std::system_error(error, "failed to copy test fixture");
	}

	void CreateHardLink(const fs::path& a_link, const fs::path& a_existing)
	{
		if (!CreateHardLinkW(a_link.c_str(), a_existing.c_str(), nullptr)) {
			throw std::system_error(
				static_cast<int>(GetLastError()),
				std::system_category(),
				"failed to create test hard link");
		}
	}

	class Workspace
	{
	public:
		Workspace()
		{
			const auto name =
				std::wstring(L"fo4cs Streamline bootstrap \u6D4B\u8BD5 ")
				+ std::to_wstring(GetCurrentProcessId()) + L" "
				+ std::to_wstring(GetTickCount64());
			_root = fs::temp_directory_path() / name;
			std::error_code error;
			fs::remove_all(_root, error);
			error.clear();
			fs::create_directories(_root, error);
			if (error)
				throw std::system_error(error, "failed to create test workspace");
		}

		~Workspace()
		{
			std::error_code error;
			fs::remove_all(_root, error);
		}

		Workspace(const Workspace&) = delete;
		Workspace& operator=(const Workspace&) = delete;

		[[nodiscard]] fs::path Fresh(std::wstring_view a_name) const
		{
			const fs::path path = _root / a_name;
			std::error_code error;
			fs::remove_all(path, error);
			error.clear();
			fs::create_directories(path, error);
			if (error)
				throw std::system_error(error, "failed to create test case");
			return path;
		}

		[[nodiscard]] const fs::path& Root() const noexcept
		{
			return _root;
		}

	private:
		fs::path _root;
	};

	class TestContext
	{
	public:
		explicit TestContext(fs::path a_fixture) :
			fixture(std::move(a_fixture))
		{
			Require(fs::is_regular_file(fixture), "fixture DLL does not exist");
		}

		[[nodiscard]] ProjectTrustConfig TrustFor(
			const EphemeralKey& a_key,
			std::string_view a_releaseId = kReleaseId) const
		{
			return {
				true,
				kKeyId,
				a_key.PublicKey(),
				a_releaseId
			};
		}

		void PrepareDlls(const fs::path& a_directory) const
		{
			CopyFile(fixture, a_directory / L"sl.common.dll");
			CopyFile(fixture, a_directory / L"sl.interposer.dll");
		}

		void SignPackage(
			const fs::path& a_directory,
			const EphemeralKey& a_key,
			std::string_view a_releaseId = kReleaseId) const
		{
			const std::array entries{
				ManifestEntry{
					"sl.common.dll",
					ProjectFileRole::eProjectCommon,
					a_directory / L"sl.common.dll" },
				ManifestEntry{
					"sl.interposer.dll",
					ProjectFileRole::eProjectInterposer,
					a_directory / L"sl.interposer.dll" }
			};
			const auto manifest =
				BuildManifest(a_releaseId, kKeyId, entries);
			const auto signature = a_key.Sign(manifest);
			WriteBytes(
				a_directory / sl::security::kProjectManifestName,
				manifest);
			WriteBytes(
				a_directory / sl::security::kProjectManifestSignatureName,
				signature);
		}
		fs::path fixture;
		Workspace workspace;
		EphemeralKey signingKey;
		EphemeralKey wrongKey;
	};

	void ClearFixtureSentinel()
	{
		static_cast<void>(SetEnvironmentVariableW(kFixtureSentinel, nullptr));
	}

	[[nodiscard]] bool FixtureAttached()
	{
		std::array<wchar_t, 8> value{};
		return GetEnvironmentVariableW(
				   kFixtureSentinel, value.data(),
				   static_cast<DWORD>(value.size())) != 0;
	}

	void CheckFailure(
		const ProjectLoadResult& a_result,
		TrustFailure a_expected,
		std::string_view a_context)
	{
		if (a_result.failure != a_expected) {
			std::cerr << "CHECK failed [" << currentTest << "]: "
					  << a_context << " expected "
					  << sl::security::getTrustFailureMessage(a_expected)
					  << ", got "
					  << sl::security::getTrustFailureMessage(a_result.failure)
					  << " (system error " << a_result.systemError << ")\n";
			++failures;
		}
	}

	template <class Function>
	void RunTest(std::string_view a_name, Function&& a_function)
	{
		currentTest = a_name;
		ClearFixtureSentinel();
		try {
			std::forward<Function>(a_function)();
		} catch (const std::exception& exception) {
			std::cerr << "FAIL [" << currentTest << "]: "
					  << exception.what() << '\n';
			++failures;
		}
		ClearFixtureSentinel();
	}

	void TestValidSignedBootstrap(TestContext& a_context)
	{
		const auto directory = a_context.workspace.Fresh(L"valid signed package");
		a_context.PrepareDlls(directory);
		a_context.SignPackage(directory, a_context.signingKey);
		const auto trust = a_context.TrustFor(a_context.signingKey);

		const auto result =
			cs::files::LoadStreamlineInterposer(directory, trust);
		ScopedModule module(result.module);
		CHECK(static_cast<bool>(result));
		CHECK(result.module != nullptr);
		CHECK(FixtureAttached());
		if (result.module) {
			using FixtureValue = int (*)() noexcept;
			const auto address =
				GetProcAddress(result.module, "StreamlineModuleFixtureValue");
			const auto fixtureValue = std::bit_cast<FixtureValue>(address);
			CHECK(fixtureValue != nullptr);
			if (fixtureValue)
				CHECK(fixtureValue() == 0x534C);
		}
	}

	void TestDisabledTrustFailsClosed(TestContext& a_context)
	{
		const auto directory = a_context.workspace.Fresh(L"disabled trust");
		a_context.PrepareDlls(directory);
		a_context.SignPackage(directory, a_context.signingKey);

		const ProjectTrustConfig disabledTrust{};
		const auto result =
			cs::files::LoadStreamlineInterposer(directory, disabledTrust);
		ScopedModule module(result.module);
		CheckFailure(
			result, TrustFailure::eProjectTrustNotConfigured,
			"explicitly disabled project trust");
		CHECK(!FixtureAttached());
	}

	void TestWrongPublicKeyFailsClosed(TestContext& a_context)
	{
		const auto directory = a_context.workspace.Fresh(L"wrong public key");
		a_context.PrepareDlls(directory);
		a_context.SignPackage(directory, a_context.signingKey);
		const auto wrongTrust = a_context.TrustFor(a_context.wrongKey);

		const auto result =
			cs::files::LoadStreamlineInterposer(directory, wrongTrust);
		ScopedModule module(result.module);
		CheckFailure(
			result, TrustFailure::eSignatureInvalid,
			"manifest signed by another test key");
		CHECK(!FixtureAttached());
	}

	void TestTamperedDllFailsBeforeAttach(TestContext& a_context)
	{
		const auto directory = a_context.workspace.Fresh(L"tampered DLL");
		a_context.PrepareDlls(directory);
		a_context.SignPackage(directory, a_context.signingKey);
		const auto interposer = directory / L"sl.interposer.dll";
		auto bytes = ReadBytes(interposer);
		Require(!bytes.empty(), "fixture DLL is empty");
		bytes[bytes.size() / 2] ^= 0x01;
		WriteBytes(interposer, bytes);
		const auto trust = a_context.TrustFor(a_context.signingKey);

		const auto result =
			cs::files::LoadStreamlineInterposer(directory, trust);
		ScopedModule module(result.module);
		CheckFailure(
			result, TrustFailure::eFileHashMismatch,
			"same-size interposer tampering");
		CHECK(!FixtureAttached());
	}

	void TestUnsignedFixtureRejectedBeforeAttach(TestContext& a_context)
	{
		const auto directory = a_context.workspace.Fresh(L"unsigned fixture");
		CopyFile(a_context.fixture, directory / L"sl.interposer.dll");
		const auto trust = a_context.TrustFor(a_context.signingKey);

		const auto result =
			cs::files::LoadStreamlineInterposer(directory, trust);
		ScopedModule module(result.module);
		CheckFailure(
			result, TrustFailure::eNvidiaSignatureInvalid,
			"unsigned manifest-free fixture");
		CHECK(!FixtureAttached());
	}

}

int wmain(int a_argc, wchar_t** a_argv)
{
	if (a_argc != 2) {
		std::cerr << "usage: StreamlineModuleTests <fixture-dll>\n";
		return 2;
	}

	try {
		TestContext context(fs::absolute(a_argv[1]));
		RunTest("valid signed bootstrap", [&] {
			TestValidSignedBootstrap(context);
		});
		RunTest("disabled trust", [&] {
			TestDisabledTrustFailsClosed(context);
		});
		RunTest("wrong public key", [&] {
			TestWrongPublicKeyFailsClosed(context);
		});
		RunTest("tampered DLL", [&] {
			TestTamperedDllFailsBeforeAttach(context);
		});
		RunTest("unsigned fixture", [&] {
			TestUnsignedFixtureRejectedBeforeAttach(context);
		});
	} catch (const std::exception& exception) {
		std::cerr << "StreamlineModule test setup failed: "
				  << exception.what() << '\n';
		return 2;
	}

	if (failures != 0) {
		std::cerr << failures << " StreamlineModule check(s) failed\n";
		return 1;
	}
	std::cout << "StreamlineModule tests passed\n";
	return 0;
}
