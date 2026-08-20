#include "Utils/ShaderCache/CacheStorage.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <limits>
#include <system_error>

namespace cs::shader_cache
{
	namespace
	{
		std::atomic<std::uint64_t> g_temporarySequence{ 0 };

		// Sharing deletes matters: a reader without it blocks another process publishing a record.
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
			// Match FXC include fallback; only failures after opening are fatal.
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
		return std::filesystem::path(L"Data")
			/ L"ShaderCache" / L"FO4CommunityShaders" / L"content-v1";
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

			// Racing renames onto one destination transiently deny access; the retry is bounded.
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
