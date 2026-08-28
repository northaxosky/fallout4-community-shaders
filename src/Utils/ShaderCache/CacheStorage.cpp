#include "Utils/ShaderCache/CacheStorage.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>

namespace cs::shader_cache
{
	namespace
	{
		std::atomic<std::uint64_t> g_temporarySequence{ 0 };
		constexpr std::string_view kIdentitySchema =
			"FO4CS.compiler-identity.v1";
		constexpr std::uint64_t kMaxIdentityBytes = 256ull * 1024ull;

		// FILE_SHARE_DELETE: else a reader blocks publication
		class ScopedHandle
		{
		public:
			explicit ScopedHandle(HANDLE a_handle) noexcept :
				_handle(a_handle)
			{}

			~ScopedHandle()
			{
				if (_handle != INVALID_HANDLE_VALUE)
					CloseHandle(_handle);
			}

			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;

			[[nodiscard]] HANDLE Get() const noexcept { return _handle; }
			[[nodiscard]] bool Valid() const noexcept
			{
				return _handle != INVALID_HANDLE_VALUE;
			}

		private:
			HANDLE _handle;
		};

		std::string FormatWin32Error(const char* a_stage, DWORD a_error)
		{
			char buffer[128]{};
			std::snprintf(
				buffer,
				std::size(buffer),
				"%s failed (%lu)",
				a_stage,
				a_error);
			return buffer;
		}

		std::filesystem::path MakeTemporaryPath(
			const std::filesystem::path& a_directory,
			std::uint64_t                a_attempt)
		{
			wchar_t name[96]{};
			std::swprintf(
				name,
				std::size(name),
				L"write-%lu-%lu-%llu-%llu.tmp",
				GetCurrentProcessId(),
				GetCurrentThreadId(),
				g_temporarySequence.fetch_add(1, std::memory_order_relaxed),
				a_attempt);
			return a_directory / name;
		}

		bool WriteAll(HANDLE a_file, std::span<const std::uint8_t> a_bytes) noexcept
		{
			std::size_t written = 0;
			while (written < a_bytes.size()) {
				const auto remaining = a_bytes.size() - written;
				const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
					remaining,
					std::numeric_limits<DWORD>::max()));
				DWORD produced = 0;
				if (!WriteFile(a_file, a_bytes.data() + written, chunk, &produced, nullptr))
					return false;
				if (produced == 0)
					return false;
				written += produced;
			}
			return true;
		}

		bool ResolveExecutablePath(std::filesystem::path& a_path)
		{
			std::wstring buffer(MAX_PATH, L'\0');
			for (;;) {
				const DWORD written = GetModuleFileNameW(
					nullptr,
					buffer.data(),
					static_cast<DWORD>(buffer.size()));
				if (written == 0)
					return false;
				if (written < buffer.size()) {
					buffer.resize(written);
					a_path = std::move(buffer);
					return true;
				}
				if (buffer.size() >= 32768)
					return false;
				buffer.resize(buffer.size() * 2);
			}
		}

		std::string IdentityMechanismToken(
			CompilerIdentityMechanism a_mechanism)
		{
			switch (a_mechanism) {
			case CompilerIdentityMechanism::kVersionInfo:
				return "version-info";
			case CompilerIdentityMechanism::kContentHash:
				return "content-hash";
			default:
				return {};
			}
		}

		std::string EncodeCompilerIdentityFields(
			const CompilerIdentity& a_identity)
		{
			const auto mechanism =
				IdentityMechanismToken(a_identity.mechanism);
			const auto path = EncodeLocator(a_identity.modulePath);
			if (!a_identity.established || mechanism.empty() || path.empty())
				return {};
			return mechanism + "|" + DescribeCompilerIdentityValue(a_identity) + "|"
				+ std::to_string(a_identity.moduleLength) + "|" + path;
		}

		std::string EncodeCacheIdentity(
			const CompilerIdentity& a_identity,
			std::uint32_t           a_recordSchemaVersion)
		{
			const auto compilerFields =
				EncodeCompilerIdentityFields(a_identity);
			if (a_recordSchemaVersion == 0 || compilerFields.empty())
				return {};
			return std::string(kIdentitySchema) + "|record-schema="
				+ std::to_string(a_recordSchemaVersion) + "|"
				+ compilerFields;
		}

		bool IsVersionValue(std::string_view a_value)
		{
			for (unsigned component = 0; component < 4; ++component) {
				const auto separator = a_value.find('.');
				const auto token = a_value.substr(0, separator);
				if (token.empty())
					return false;
				unsigned value = 0;
				const auto parsed =
					std::from_chars(token.data(), token.data() + token.size(), value);
				if (parsed.ec != std::errc{}
					|| parsed.ptr != token.data() + token.size()
					|| value > std::numeric_limits<std::uint16_t>::max()) {
					return false;
				}
				if (component == 3)
					return separator == std::string_view::npos;
				if (separator == std::string_view::npos)
					return false;
				a_value.remove_prefix(separator + 1);
			}
			return false;
		}

		bool IsHashValue(std::string_view a_value)
		{
			constexpr std::string_view prefix = "sha256:";
			if (!a_value.starts_with(prefix)
				|| a_value.size() != prefix.size() + 64) {
				return false;
			}
			return std::ranges::all_of(
				a_value.substr(prefix.size()),
				[](char a_character) {
					return std::isxdigit(
						static_cast<unsigned char>(a_character)) != 0;
				});
		}

		struct StoredCacheIdentity
		{
			std::uint32_t recordSchemaVersion = 0;
			std::string   compiler;
			std::string   compilerFields;
		};

		bool ParseCacheIdentity(
			std::string_view    a_text,
			StoredCacheIdentity& a_identity)
		{
			a_identity = {};
			const auto schemaEnd = a_text.find('|');
			if (schemaEnd == std::string_view::npos
				|| a_text.substr(0, schemaEnd) != kIdentitySchema) {
				return false;
			}
			a_text.remove_prefix(schemaEnd + 1);

			const auto recordSchemaEnd = a_text.find('|');
			if (recordSchemaEnd == std::string_view::npos)
				return false;
			const auto recordSchema = a_text.substr(0, recordSchemaEnd);
			constexpr std::string_view prefix = "record-schema=";
			if (!recordSchema.starts_with(prefix))
				return false;
			const auto versionText = recordSchema.substr(prefix.size());
			const auto versionParsed = std::from_chars(
				versionText.data(),
				versionText.data() + versionText.size(),
				a_identity.recordSchemaVersion);
			if (versionParsed.ec != std::errc{}
				|| versionParsed.ptr != versionText.data() + versionText.size()
				|| a_identity.recordSchemaVersion == 0) {
				return false;
			}
			a_text.remove_prefix(recordSchemaEnd + 1);
			a_identity.compilerFields.assign(a_text);

			const auto mechanismEnd = a_text.find('|');
			if (mechanismEnd == std::string_view::npos)
				return false;
			const auto mechanism = a_text.substr(0, mechanismEnd);
			a_text.remove_prefix(mechanismEnd + 1);

			const auto valueEnd = a_text.find('|');
			if (valueEnd == std::string_view::npos)
				return false;
			const auto value = a_text.substr(0, valueEnd);
			a_text.remove_prefix(valueEnd + 1);

			const auto lengthEnd = a_text.find('|');
			if (lengthEnd == std::string_view::npos)
				return false;
			const auto lengthText = a_text.substr(0, lengthEnd);
			a_text.remove_prefix(lengthEnd + 1);

			std::uint64_t length = 0;
			const auto parsed = std::from_chars(
				lengthText.data(),
				lengthText.data() + lengthText.size(),
				length);
			if (parsed.ec != std::errc{}
				|| parsed.ptr != lengthText.data() + lengthText.size()
				|| length == 0
				|| a_text.empty()
				|| a_text.contains('\r')
				|| a_text.contains('\n')) {
				return false;
			}

			if ((mechanism == "version-info" && !IsVersionValue(value))
				|| (mechanism == "content-hash" && !IsHashValue(value))
				|| (mechanism != "version-info"
					&& mechanism != "content-hash")) {
				return false;
			}

			a_identity.compiler.assign(value);
			return true;
		}

		bool CacheRootHasRecords(const std::filesystem::path& a_cacheRoot)
		{
			std::error_code error;
			if (!std::filesystem::exists(a_cacheRoot, error))
				return static_cast<bool>(error);
			if (!std::filesystem::is_directory(a_cacheRoot, error))
				return true;
			for (const auto stage : {
					 ShaderCacheStage::kVertex,
					 ShaderCacheStage::kPixel,
					 ShaderCacheStage::kCompute }) {
				if (std::filesystem::exists(
						a_cacheRoot / DescribeStage(stage),
						error)) {
					return true;
				}
				if (error)
					return true;
			}
			return false;
		}
	}

	FileReadStatus ReadFileBytes(
		const std::filesystem::path& a_path,
		std::uint64_t                a_maxBytes,
		std::vector<std::uint8_t>&   a_bytes) noexcept
	{
		a_bytes.clear();
		try {
			const ScopedHandle file(CreateFileW(
				a_path.c_str(),
				GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				nullptr));
			// match FXC fallback: only post-open failures are fatal
			if (!file.Valid())
				return FileReadStatus::kMissing;

			LARGE_INTEGER size{};
			if (!GetFileSizeEx(file.Get(), &size) || size.QuadPart < 0)
				return FileReadStatus::kReadFailed;
			if (static_cast<std::uint64_t>(size.QuadPart) > a_maxBytes)
				return FileReadStatus::kTooLarge;

			a_bytes.resize(static_cast<std::size_t>(size.QuadPart));
			std::size_t read = 0;
			while (read < a_bytes.size()) {
				const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
					a_bytes.size() - read,
					std::numeric_limits<DWORD>::max()));
				DWORD produced = 0;
				if (!ReadFile(file.Get(), a_bytes.data() + read, chunk, &produced, nullptr)
					|| produced == 0) {
					a_bytes.clear();
					return FileReadStatus::kReadFailed;
				}
				read += produced;
			}
			return FileReadStatus::kOk;
		} catch (...) {
			a_bytes.clear();
			return FileReadStatus::kReadFailed;
		}
	}

	std::filesystem::path DefaultCacheRoot()
	{
		static const std::filesystem::path root = [] {
			std::filesystem::path executable;
			if (ResolveExecutablePath(executable)) {
				return (executable.parent_path() / L"Data" / L"ShaderCache")
					.lexically_normal();
			}

			std::error_code error;
			auto fallback = std::filesystem::absolute(
				std::filesystem::path(L"Data") / L"ShaderCache",
				error);
			if (error)
				fallback = std::filesystem::path(L"Data") / L"ShaderCache";
			return fallback.lexically_normal();
		}();
		return root;
	}

	CacheIdentitySyncResult SynchronizeCacheIdentity(
		const std::filesystem::path& a_cacheRoot,
		const CompilerIdentity&      a_identity,
		std::uint32_t                a_recordSchemaVersion) noexcept
	{
		CacheIdentitySyncResult result;
		try {
			const auto currentCompiler =
				DescribeCompilerIdentityValue(a_identity);
			const auto currentCompilerFields =
				EncodeCompilerIdentityFields(a_identity);
			const auto current =
				EncodeCacheIdentity(a_identity, a_recordSchemaVersion);
			if (current.empty()) {
				result.error = "cache identity unavailable";
				return result;
			}

			const auto identityPath =
				a_cacheRoot / L"identity.txt";
			std::vector<std::uint8_t> bytes;
			const auto readStatus =
				ReadFileBytes(identityPath, kMaxIdentityBytes, bytes);

			bool resetRequired = false;
			if (readStatus == FileReadStatus::kMissing) {
				result.firstRun = !CacheRootHasRecords(a_cacheRoot);
				resetRequired = !result.firstRun;
				if (resetRequired) {
					result.resetMessage =
						"shader cache reset: identity sidecar missing";
				}
			} else if (readStatus == FileReadStatus::kOk) {
				const std::string stored(bytes.begin(), bytes.end());
				StoredCacheIdentity storedIdentity;
				if (!ParseCacheIdentity(stored, storedIdentity)) {
					resetRequired = true;
					result.resetMessage =
						"shader cache reset: identity sidecar invalid";
				} else {
					const bool schemaChanged =
						storedIdentity.recordSchemaVersion
						!= a_recordSchemaVersion;
					const bool compilerChanged =
						storedIdentity.compilerFields
						!= currentCompilerFields;
					if (!schemaChanged && !compilerChanged)
						return result;
					resetRequired = true;
					if (schemaChanged) {
						result.resetMessage =
							"shader cache reset: record schema "
							+ std::to_string(
								storedIdentity.recordSchemaVersion)
							+ " -> "
							+ std::to_string(a_recordSchemaVersion);
						if (compilerChanged) {
							result.resetMessage += ", compiler "
								+ storedIdentity.compiler + " -> "
								+ currentCompiler;
						}
					} else {
						result.resetMessage =
							"shader cache reset: compiler "
							+ storedIdentity.compiler + " -> "
							+ currentCompiler;
					}
				}
			} else {
				resetRequired = true;
				result.resetMessage =
					"shader cache reset: identity sidecar unreadable";
			}

			if (resetRequired) {
				std::error_code error;
				std::filesystem::remove_all(a_cacheRoot, error);
				if (error) {
					result.error =
						"cache reset failed: " + error.message();
					return result;
				}
				result.reset = true;
			}

			const auto* first =
				reinterpret_cast<const std::uint8_t*>(current.data());
			std::string writeError;
			if (!WriteRecordAtomically(
					identityPath,
					std::span(first, current.size()),
					writeError)) {
				result.error =
					"identity sidecar write failed: " + writeError;
			}
		} catch (const std::exception& exception) {
			result.error = exception.what();
		} catch (...) {
			result.error = "unexpected cache identity failure";
		}
		return result;
	}

	std::filesystem::path BuildRecordPath(
		const std::filesystem::path& a_cacheRoot,
		ShaderCacheStage             a_stage,
		const sha256::Sha256Result&  a_logicalDigest)
	{
		const auto digest = sha256::Sha256ToHex(a_logicalDigest);
		return a_cacheRoot
			/ DescribeStage(a_stage)
			/ digest.substr(0, 2)
			/ (digest + ".fxc");
	}

	bool WriteRecordAtomically(
		const std::filesystem::path&  a_path,
		std::span<const std::uint8_t> a_bytes,
		std::string&                  a_error) noexcept
	{
		a_error.clear();
		HANDLE                file = INVALID_HANDLE_VALUE;
		std::filesystem::path temporaryPath;
		try {
			const auto directory = a_path.parent_path();
			if (!directory.empty()) {
				std::error_code error;
				std::filesystem::create_directories(directory, error);
				if (error && !std::filesystem::is_directory(directory)) {
					a_error = "create_directories failed: " + error.message();
					return false;
				}
			}

			for (std::uint64_t attempt = 0; attempt < 8; ++attempt) {
				temporaryPath = MakeTemporaryPath(directory, attempt);
				file          = CreateFileW(
                    temporaryPath.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
				if (file != INVALID_HANDLE_VALUE)
					break;
				const DWORD error = GetLastError();
				if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
					a_error = FormatWin32Error("CreateFileW", error);
					return false;
				}
			}
			if (file == INVALID_HANDLE_VALUE) {
				a_error = "CreateFileW exhausted temporary names";
				return false;
			}

			const bool  written = WriteAll(file, a_bytes);
			const DWORD writeError = written ? ERROR_SUCCESS : GetLastError();
			const bool  flushed = written && FlushFileBuffers(file);
			const DWORD flushError = flushed ? ERROR_SUCCESS : GetLastError();
			const bool closed = CloseHandle(file) != FALSE;
			const DWORD closeError = closed ? ERROR_SUCCESS : GetLastError();
			file = INVALID_HANDLE_VALUE;

			if (!written || !flushed || !closed) {
				DeleteFileW(temporaryPath.c_str());
				a_error = FormatWin32Error(
					!written ? "WriteFile" : !flushed ? "FlushFileBuffers" : "CloseHandle",
					!written ? writeError : !flushed ? flushError : closeError);
				return false;
			}

			// bounded retry for racing publication
			DWORD moveError = ERROR_SUCCESS;
			for (unsigned attempt = 0; attempt < 4; ++attempt) {
				if (MoveFileExW(
						temporaryPath.c_str(),
						a_path.c_str(),
						MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
					return true;
				}
				moveError = GetLastError();
				if (moveError != ERROR_ACCESS_DENIED
					&& moveError != ERROR_SHARING_VIOLATION) {
					break;
				}
				Sleep(1);
			}

			DeleteFileW(temporaryPath.c_str());
			a_error = FormatWin32Error("MoveFileExW", moveError);
			return false;
		} catch (...) {
			if (file != INVALID_HANDLE_VALUE)
				CloseHandle(file);
			if (!temporaryPath.empty())
				DeleteFileW(temporaryPath.c_str());
			a_error = "unexpected failure while publishing the record";
			return false;
		}
	}
}
