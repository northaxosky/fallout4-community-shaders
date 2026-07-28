#include "Provenance.h"

#include "Sha1.h"

#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace cs::features::catalog
{
	namespace
	{
		constexpr std::string_view kIdentifierCharacters =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._:-";

		bool GuardedCopyRaw(
			void* a_destination,
			const void* a_source,
			std::size_t a_size) noexcept
		{
			__try {
				std::memcpy(a_destination, a_source, a_size);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool GuardedStringRaw(
			char* a_destination,
			std::size_t a_capacity,
			const char* a_source,
			std::size_t* a_length,
			bool* a_truncated) noexcept
		{
			__try {
				std::size_t length = 0;
				while (length < a_capacity && a_source[length] != '\0') {
					a_destination[length] = a_source[length];
					++length;
				}
				*a_length = length;
				*a_truncated = length == a_capacity;
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		void AppendU32(std::vector<std::byte>& a_bytes, std::uint32_t a_value)
		{
			for (unsigned shift = 0; shift < 32; shift += 8)
				a_bytes.push_back(static_cast<std::byte>((a_value >> shift) & 0xffu));
		}

		bool AppendSemantic(
			std::vector<std::byte>& a_bytes,
			const char* a_semantic,
			bool& a_truncated)
		{
			if (!a_semantic) {
				a_bytes.push_back(std::byte{ 0 });
				AppendU32(a_bytes, 0);
				return true;
			}

			std::array<char, kMaxSemanticBytes + 1> semantic{};
			std::size_t length = 0;
			bool truncated = false;
			if (!GuardedStringRaw(
					semantic.data(), semantic.size(), a_semantic,
					&length, &truncated))
				return false;
			a_bytes.push_back(truncated ? std::byte{ 2 } : std::byte{ 1 });
			const auto storedLength = static_cast<std::uint32_t>(
				std::min<std::size_t>(length, kMaxSemanticBytes));
			AppendU32(a_bytes, storedLength);
			const auto* begin = reinterpret_cast<const std::byte*>(
				semantic.data());
			a_bytes.insert(a_bytes.end(), begin, begin + storedLength);
			a_truncated = a_truncated || truncated;
			return true;
		}

		std::optional<std::string> ReadEnvironmentValue(const wchar_t* a_name)
		{
			SetLastError(ERROR_SUCCESS);
			const DWORD required = GetEnvironmentVariableW(a_name, nullptr, 0);
			if (required == 0) {
				return GetLastError() == ERROR_ENVVAR_NOT_FOUND
					? std::nullopt
					: std::optional<std::string>(std::string{});
			}

			std::wstring wide(required, L'\0');
			const DWORD written = GetEnvironmentVariableW(a_name, wide.data(), required);
			if (written == 0 || written >= required)
				return std::string{};
			wide.resize(written);

			const int utf8Size = WideCharToMultiByte(
				CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
				nullptr, 0, nullptr, nullptr);
			if (utf8Size <= 0)
				return std::string{};
			std::string utf8(static_cast<std::size_t>(utf8Size), '\0');
			if (WideCharToMultiByte(
					CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
					utf8.data(), utf8Size, nullptr, nullptr) != utf8Size)
				return std::string{};
			return utf8;
		}

		std::optional<std::filesystem::path> Utf8Path(std::string_view a_value)
		{
			if (a_value.empty())
				return std::nullopt;
			const int required = MultiByteToWideChar(
				CP_UTF8, MB_ERR_INVALID_CHARS,
				a_value.data(), static_cast<int>(a_value.size()),
				nullptr, 0);
			if (required <= 0)
				return std::nullopt;
			std::wstring wide(static_cast<std::size_t>(required), L'\0');
			if (MultiByteToWideChar(
					CP_UTF8, MB_ERR_INVALID_CHARS,
					a_value.data(), static_cast<int>(a_value.size()),
					wide.data(), required) != required)
				return std::nullopt;
			return std::filesystem::path(std::move(wide));
		}

		bool ValidIdentifier(const std::optional<std::string>& a_value)
		{
			if (!a_value)
				return true;
			if (a_value->empty() || a_value->size() > kMaxCatalogIdentifierBytes)
				return false;
			return a_value->find_first_not_of(kIdentifierCharacters) == std::string::npos
				&& a_value->find("..") == std::string::npos;
		}

		void ValidateIdentifier(
			const char* a_name,
			std::optional<std::string>& a_value,
			RunPolicy& a_policy)
		{
			if (ValidIdentifier(a_value))
				return;
			a_policy.environmentValid = false;
			a_policy.errors.emplace_back(std::string(a_name) + " is not a valid bounded identifier");
			a_value.reset();
		}

		bool HashSha256(const void* a_data, std::size_t a_size, std::array<std::uint8_t, 32>& a_result) noexcept
		{
			if (!a_data || a_size == 0 || a_size > static_cast<std::size_t>(ULONG_MAX))
				return false;

			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			DWORD objectLength = 0;
			DWORD resultLength = 0;
			std::unique_ptr<unsigned char[]> object;
			bool success = false;

			if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(
					&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
				goto cleanup;
			if (!NT_SUCCESS(BCryptGetProperty(
					algorithm, BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
					&resultLength, 0)))
				goto cleanup;
			object.reset(new (std::nothrow) unsigned char[objectLength]);
			if (!object)
				goto cleanup;
			if (!NT_SUCCESS(BCryptCreateHash(
					algorithm, &hash, object.get(), objectLength, nullptr, 0, 0)))
				goto cleanup;
			if (!NT_SUCCESS(BCryptHashData(
					hash,
					const_cast<PUCHAR>(static_cast<const unsigned char*>(a_data)),
					static_cast<ULONG>(a_size), 0)))
				goto cleanup;
			success = NT_SUCCESS(BCryptFinishHash(
				hash, a_result.data(), static_cast<ULONG>(a_result.size()), 0));

		cleanup:
			if (hash)
				BCryptDestroyHash(hash);
			if (algorithm)
				BCryptCloseAlgorithmProvider(algorithm, 0);
			return success;
		}

		std::size_t Utf8SequenceLength(
			std::string_view a_value,
			std::size_t a_offset) noexcept
		{
			const auto first = static_cast<unsigned char>(a_value[a_offset]);
			if (first < 0x80)
				return 1;
			std::size_t length = 0;
			std::uint32_t codePoint = 0;
			std::uint32_t minimum = 0;
			if (first >= 0xc2 && first <= 0xdf) {
				length = 2;
				codePoint = first & 0x1f;
				minimum = 0x80;
			} else if (first >= 0xe0 && first <= 0xef) {
				length = 3;
				codePoint = first & 0x0f;
				minimum = 0x800;
			} else if (first >= 0xf0 && first <= 0xf4) {
				length = 4;
				codePoint = first & 0x07;
				minimum = 0x10000;
			} else {
				return 0;
			}
			if (a_offset + length > a_value.size())
				return 0;
			for (std::size_t i = 1; i < length; ++i) {
				const auto continuation = static_cast<unsigned char>(
					a_value[a_offset + i]);
				if ((continuation & 0xc0) != 0x80)
					return 0;
				codePoint = (codePoint << 6) | (continuation & 0x3f);
			}
			if (codePoint < minimum || codePoint > 0x10ffff
				|| (codePoint >= 0xd800 && codePoint <= 0xdfff))
				return 0;
			return length;
		}

		std::string EscapeJson(std::string_view a_value)
		{
			std::string result;
			result.reserve(a_value.size() + 8);
			for (std::size_t i = 0; i < a_value.size();) {
				const auto ch = static_cast<unsigned char>(a_value[i]);
				if (ch >= 0x80) {
					const auto length = Utf8SequenceLength(a_value, i);
					if (length == 0) {
						result += "\xef\xbf\xbd";
						++i;
					} else {
						result.append(a_value.substr(i, length));
						i += length;
					}
					continue;
				}
				switch (ch) {
				case '"':
					result += "\\\"";
					break;
				case '\\':
					result += "\\\\";
					break;
				case '\b':
					result += "\\b";
					break;
				case '\f':
					result += "\\f";
					break;
				case '\n':
					result += "\\n";
					break;
				case '\r':
					result += "\\r";
					break;
				case '\t':
					result += "\\t";
					break;
				default:
					if (ch < 0x20) {
						char escaped[7]{};
						std::snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
						result += escaped;
					} else {
						result.push_back(static_cast<char>(ch));
					}
					break;
				}
				++i;
			}
			return result;
		}

		void JsonString(std::ostringstream& a_json, std::string_view a_value)
		{
			a_json << '"' << EscapeJson(a_value) << '"';
		}

		void JsonOptionalString(
			std::ostringstream& a_json,
			const std::optional<std::string>& a_value)
		{
			if (a_value)
				JsonString(a_json, *a_value);
			else
				a_json << "null";
		}

		void JsonOptionalInt(std::ostringstream& a_json, const std::optional<int>& a_value)
		{
			if (a_value)
				a_json << *a_value;
			else
				a_json << "null";
		}

		void JsonBool(std::ostringstream& a_json, bool a_value)
		{
			a_json << (a_value ? "true" : "false");
		}

		std::wstring LongPath(const std::filesystem::path& a_path)
		{
			std::wstring result = std::filesystem::absolute(a_path).wstring();
			if (result.starts_with(LR"(\\?\)"))
				return result;
			if (result.starts_with(LR"(\\)"))
				return LR"(\\?\UNC\)" + result.substr(2);
			return LR"(\\?\)" + result;
		}

		struct PublicationState
		{
			std::vector<HANDLE> directories;
			HANDLE file = INVALID_HANDLE_VALUE;
			HANDLE target = INVALID_HANDLE_VALUE;
			std::filesystem::path temporaryPath;
			std::filesystem::path relativePath;
			std::wstring targetFilename;
			std::wstring targetPath;
			std::wstring rootFinalPath;
			std::size_t expectedSize = 0;
			std::string expectedSha256;
			bool published = false;

			~PublicationState()
			{
				if (file != INVALID_HANDLE_VALUE)
					CloseHandle(file);
				if (target != INVALID_HANDLE_VALUE)
					CloseHandle(target);
				if (!published && !temporaryPath.empty())
					DeleteFileW(LongPath(temporaryPath).c_str());
				for (auto it = directories.rbegin(); it != directories.rend(); ++it)
					CloseHandle(*it);
			}
		};

#ifdef FO4CS_SHADER_CATALOG_TESTING
		std::atomic<BeforeDirectoryCreateForTesting>
			g_beforeDirectoryCreate{ nullptr };
		std::atomic<bool> g_holdNextPublishedWinner{ false };
		std::atomic<bool> g_publishedWinnerHeld{ false };
		std::atomic<bool> g_releasePublishedWinner{ false };
		std::atomic<bool> g_publicationCollisionRetried{ false };
#endif

		bool HandleAttributes(
			HANDLE a_handle,
			bool a_directory,
			std::string& a_error)
		{
			FILE_ATTRIBUTE_TAG_INFO info{};
			if (!GetFileInformationByHandleEx(
					a_handle, FileAttributeTagInfo, &info, sizeof(info))) {
				a_error = "unable to inspect publication path attributes";
				return false;
			}
			if ((info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
				a_error = "publication path contains a reparse point";
				return false;
			}
			const bool isDirectory =
				(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			if (isDirectory != a_directory) {
				a_error = "publication path has the wrong object type";
				return false;
			}
			return true;
		}

		std::optional<std::wstring> FinalPath(
			HANDLE a_handle,
			std::string& a_error)
		{
			const DWORD required = GetFinalPathNameByHandleW(
				a_handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (required == 0) {
				a_error = "unable to resolve final publication path";
				return std::nullopt;
			}
			std::wstring path(required, L'\0');
			const DWORD written = GetFinalPathNameByHandleW(
				a_handle, path.data(), required,
				FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (written == 0 || written >= required) {
				a_error = "unable to resolve final publication path";
				return std::nullopt;
			}
			path.resize(written);
			return path;
		}

		bool IsBeneath(
			std::wstring_view a_root,
			std::wstring_view a_path) noexcept
		{
			if (a_path.size() < a_root.size()
				|| CompareStringOrdinal(
					a_root.data(), static_cast<int>(a_root.size()),
					a_path.data(), static_cast<int>(a_root.size()), TRUE)
					!= CSTR_EQUAL)
				return false;
			return a_path.size() == a_root.size()
				|| a_path[a_root.size()] == L'\\';
		}

		bool IsSharingCollision(DWORD a_error) noexcept
		{
			return a_error == ERROR_SHARING_VIOLATION
				|| a_error == ERROR_LOCK_VIOLATION;
		}

		bool OpenPinnedDirectories(
			const std::filesystem::path& a_root,
			const std::filesystem::path& a_relative,
			std::shared_ptr<PublicationState>& a_state,
			std::filesystem::path& a_directory,
			std::string& a_error)
		{
			if (!a_root.is_absolute()) {
				a_error = "publication root is not absolute";
				return false;
			}
			a_state = std::make_shared<PublicationState>();
			const auto absoluteRoot =
				std::filesystem::absolute(a_root).lexically_normal();
			a_directory = absoluteRoot.root_path();
			auto openDirectory = [&](
				const std::filesystem::path& a_path,
				bool a_publicationRoot) {
				const HANDLE handle = CreateFileW(
					LongPath(a_path).c_str(), FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
					nullptr);
				if (handle == INVALID_HANDLE_VALUE) {
					a_error = "unable to pin publication directory";
					return false;
				}
				if (!HandleAttributes(handle, true, a_error)) {
					CloseHandle(handle);
					return false;
				}
				const auto final = FinalPath(handle, a_error);
				if (!final) {
					CloseHandle(handle);
					return false;
				}
				if (a_publicationRoot)
					a_state->rootFinalPath = *final;
				else if (!a_state->rootFinalPath.empty()
					&& !IsBeneath(a_state->rootFinalPath, *final)) {
					CloseHandle(handle);
					a_error = "publication directory escaped pinned root";
					return false;
				}
				a_state->directories.push_back(handle);
				return true;
			};
			if (!openDirectory(
					a_directory, a_directory == absoluteRoot))
				return false;
			for (const auto& component : absoluteRoot.relative_path()) {
				if (component == "." || component == "..") {
					a_error = "publication root traversal rejected";
					return false;
				}
				a_directory /= component;
				if (!openDirectory(
						a_directory, a_directory == absoluteRoot))
					return false;
			}
			if (a_state->rootFinalPath.empty()) {
				a_error = "unable to pin publication root";
				return false;
			}
			for (const auto& component : a_relative) {
				if (component == "." || component == ".."
					|| component.has_root_path()) {
					a_error = "publication path traversal rejected";
					return false;
				}
				a_directory /= component;
				std::error_code ec;
				const bool exists =
					std::filesystem::exists(a_directory, ec);
				if (ec) {
					a_error = "unable to inspect publication directory";
					return false;
				}
				if (!exists) {
#ifdef FO4CS_SHADER_CATALOG_TESTING
					if (const auto callback =
							g_beforeDirectoryCreate.load(
								std::memory_order_acquire)) {
						callback(a_directory);
					}
#endif
					ec.clear();
					(void)std::filesystem::create_directory(
						a_directory, ec);
					if (ec) {
						a_error = "unable to create publication directory";
						return false;
					}
				}
				if (!openDirectory(a_directory, false))
					return false;
			}
			return true;
		}

		bool VerifyFileHandle(
			HANDLE a_file,
			const PublicationState& a_state,
			std::string& a_error)
		{
			if (!HandleAttributes(a_file, false, a_error))
				return false;
			const auto finalPath = FinalPath(a_file, a_error);
			if (!finalPath || !IsBeneath(a_state.rootFinalPath, *finalPath)) {
				a_error = "publication file escaped pinned root";
				return false;
			}
			LARGE_INTEGER fileSize{};
			if (!GetFileSizeEx(a_file, &fileSize)
				|| fileSize.QuadPart < 0
				|| static_cast<std::uint64_t>(fileSize.QuadPart)
					!= a_state.expectedSize) {
				a_error = "published file has the wrong size";
				return false;
			}
			std::unique_ptr<std::byte[]> bytes(
				new (std::nothrow) std::byte[a_state.expectedSize]);
			if (!bytes) {
				a_error = "unable to allocate publication verification buffer";
				return false;
			}
			LARGE_INTEGER zero{};
			if (!SetFilePointerEx(a_file, zero, nullptr, FILE_BEGIN)) {
				a_error = "unable to seek publication file";
				return false;
			}
			std::size_t offset = 0;
			while (offset < a_state.expectedSize) {
				const DWORD request = static_cast<DWORD>(
					std::min<std::size_t>(
						a_state.expectedSize - offset,
						std::numeric_limits<DWORD>::max()));
				DWORD read = 0;
				if (!ReadFile(
						a_file, bytes.get() + offset, request, &read, nullptr)
					|| read == 0) {
					a_error = "unable to read publication file";
					return false;
				}
				offset += read;
			}
			ContentDigest digest{};
			if (!ComputeDigests(bytes.get(), a_state.expectedSize, digest)
				|| HexLower(digest.sha256.data(), digest.sha256.size())
					!= a_state.expectedSha256) {
				a_error = "published file content does not match its digest";
				return false;
			}
			return true;
		}

		bool OpenVerifiedWinner(
			PublicationState& a_state,
			const std::wstring& a_targetPath,
			PublicationResult& a_result)
		{
			a_result.success = false;
			a_result.alreadyExisted = true;
			constexpr unsigned kWinnerOpenAttempts = 64;
			for (unsigned attempt = 0;
				 attempt < kWinnerOpenAttempts;
				 ++attempt) {
				a_state.target = CreateFileW(
					a_targetPath.c_str(), GENERIC_READ,
					FILE_SHARE_READ, nullptr, OPEN_EXISTING,
					FILE_FLAG_OPEN_REPARSE_POINT
						| FILE_FLAG_SEQUENTIAL_SCAN,
					nullptr);
				DWORD winnerError = GetLastError();
				if (a_state.target != INVALID_HANDLE_VALUE) {
					SetLastError(ERROR_SUCCESS);
					a_result.success = VerifyFileHandle(
						a_state.target, a_state, a_result.error);
					if (a_result.success)
						return true;
					winnerError = GetLastError();
					CloseHandle(a_state.target);
					a_state.target = INVALID_HANDLE_VALUE;
				}
				if (!IsSharingCollision(winnerError)
					|| attempt + 1 == kWinnerOpenAttempts) {
					if (a_result.error.empty()
						|| IsSharingCollision(winnerError)) {
						a_result.error =
							"unable to verify publication winner (error "
							+ std::to_string(winnerError) + ")";
					}
					return false;
				}
#ifdef FO4CS_SHADER_CATALOG_TESTING
				g_publicationCollisionRetried.store(
					true, std::memory_order_release);
#endif
				Sleep(5);
			}
			a_result.error = "unable to verify publication winner";
			return false;
		}

		StagedManifestPublication StageImmutable(
			const std::filesystem::path& a_root,
			const std::filesystem::path& a_relative,
			const void* a_data,
			std::size_t a_size,
			std::string_view a_expectedSha256)
		{
			StagedManifestPublication staged;
			staged.result.relativePath = a_relative;
			if (!a_data || a_size == 0 || a_size > kMaxShaderBytecodeBytes) {
				staged.result.error =
					"publication payload is invalid or out of bounds";
				return staged;
			}
			std::shared_ptr<PublicationState> state;
			std::filesystem::path directory;
			if (!OpenPinnedDirectories(
					a_root, a_relative.parent_path(), state,
					directory, staged.result.error))
				return staged;
			state->relativePath = a_relative;
			state->targetFilename = a_relative.filename().wstring();
			state->expectedSize = a_size;
			state->expectedSha256 = a_expectedSha256;
			const auto target = directory / a_relative.filename();
			const HANDLE existing = CreateFileW(
				LongPath(target).c_str(), GENERIC_READ,
				FILE_SHARE_READ,
				nullptr, OPEN_EXISTING,
				FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
				nullptr);
			if (existing != INVALID_HANDLE_VALUE) {
				state->target = existing;
				staged.result.alreadyExisted = true;
				staged.result.success = VerifyFileHandle(
					existing, *state, staged.result.error);
				staged.state = std::move(state);
				return staged;
			}
			const DWORD existingError = GetLastError();
			if (IsSharingCollision(existingError)) {
				const auto targetPath = LongPath(target);
				(void)OpenVerifiedWinner(
					*state, targetPath, staged.result);
				if (staged.result.success)
					staged.state = std::move(state);
				return staged;
			}
			if (existingError != ERROR_FILE_NOT_FOUND
				&& existingError != ERROR_PATH_NOT_FOUND) {
				staged.result.error = "unable to inspect publication target";
				return staged;
			}
			const auto uuid = GenerateUuidV4();
			if (!uuid) {
				staged.result.error =
					"unable to generate publication temporary name";
				return staged;
			}
			state->temporaryPath =
				directory / (".catalog-" + *uuid + ".pending");
			state->file = CreateFileW(
				LongPath(state->temporaryPath).c_str(),
				GENERIC_WRITE | GENERIC_READ | DELETE, 0,
				nullptr, CREATE_NEW,
				FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH
					| FILE_FLAG_OPEN_REPARSE_POINT,
				nullptr);
			if (state->file == INVALID_HANDLE_VALUE) {
				staged.result.error =
					"unable to create publication temporary file";
				return staged;
			}
			const auto* bytes = static_cast<const std::byte*>(a_data);
			std::size_t offset = 0;
			while (offset < a_size) {
				const DWORD request = static_cast<DWORD>(
					std::min<std::size_t>(
						a_size - offset, std::numeric_limits<DWORD>::max()));
				DWORD written = 0;
				if (!WriteFile(
						state->file, bytes + offset, request,
						&written, nullptr)
					|| written == 0) {
					staged.result.error =
						"unable to write publication payload";
					return staged;
				}
				offset += written;
			}
			if (!FlushFileBuffers(state->file)
				|| !VerifyFileHandle(
					state->file, *state, staged.result.error))
				return staged;
			staged.result.success = true;
			staged.state = std::move(state);
			return staged;
		}

		PublicationResult PublishStagedState(
			StagedManifestPublication& a_staged)
		{
			auto state = std::static_pointer_cast<PublicationState>(
				a_staged.state);
			if (!a_staged.result.success || !state) {
				PublicationResult result = a_staged.result;
				result.success = false;
				if (result.error.empty())
					result.error = "publication was not staged";
				return result;
			}
			if (a_staged.result.alreadyExisted) {
				PublicationResult result = a_staged.result;
				result.success = state->target != INVALID_HANDLE_VALUE
					&& VerifyFileHandle(
						state->target, *state, result.error);
				return result;
			}
			std::string error;
			if (!VerifyFileHandle(state->file, *state, error)) {
				PublicationResult result = a_staged.result;
				result.success = false;
				result.error = std::move(error);
				return result;
			}
			const auto directoryPath = FinalPath(
				state->directories.back(), error);
			if (!directoryPath
				|| !IsBeneath(state->rootFinalPath, *directoryPath)) {
				PublicationResult result = a_staged.result;
				result.success = false;
				result.error = error.empty()
					? "publication directory escaped pinned root"
					: std::move(error);
				return result;
			}
			state->targetPath = *directoryPath + L"\\"
				+ state->targetFilename;
			const std::size_t bytes =
				sizeof(FILE_RENAME_INFO) + state->targetPath.size() * sizeof(wchar_t);
			std::vector<std::byte> storage(bytes);
			auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
			rename->ReplaceIfExists = FALSE;
			rename->RootDirectory = nullptr;
			rename->FileNameLength = static_cast<DWORD>(
				state->targetPath.size() * sizeof(wchar_t));
			std::memcpy(
				rename->FileName, state->targetPath.data(),
				rename->FileNameLength);
			PublicationResult result = a_staged.result;
			result.success = SetFileInformationByHandle(
				state->file, FileRenameInfo, rename,
				static_cast<DWORD>(storage.size())) != FALSE;
			if (!result.success) {
				const DWORD renameError = GetLastError();
				if (renameError == ERROR_ALREADY_EXISTS
					|| renameError == ERROR_FILE_EXISTS
					|| IsSharingCollision(renameError)) {
					(void)OpenVerifiedWinner(
						*state, state->targetPath, result);
				} else {
					result.error = "unable to atomically publish file (error "
						+ std::to_string(renameError) + ")";
				}
			} else {
				state->published = true;
#ifdef FO4CS_SHADER_CATALOG_TESTING
				if (g_holdNextPublishedWinner.exchange(
						false, std::memory_order_acq_rel)) {
					g_publishedWinnerHeld.store(
						true, std::memory_order_release);
					g_publishedWinnerHeld.notify_all();
					bool released =
						g_releasePublishedWinner.load(
							std::memory_order_acquire);
					while (!released) {
						g_releasePublishedWinner.wait(
							false, std::memory_order_acquire);
						released = g_releasePublishedWinner.load(
							std::memory_order_acquire);
					}
					g_publishedWinnerHeld.store(
						false, std::memory_order_release);
				}
#endif
				CloseHandle(state->file);
				state->file = INVALID_HANDLE_VALUE;
			}
			return result;
		}

		void AppendQuality(std::ostringstream& a_json, const QualityCounters& a_quality)
		{
			a_json << "{\"queue_overflow\":" << a_quality.queueOverflow
				   << ",\"malformed_bytecode\":" << a_quality.malformedBytecode
				   << ",\"unsupported_size\":" << a_quality.unsupportedSize
				   << ",\"allocation_failure\":" << a_quality.allocationFailure
				   << ",\"copy_failure\":" << a_quality.copyFailure
				   << ",\"hash_failure\":" << a_quality.hashFailure
				   << ",\"metadata_truncated\":" << a_quality.metadataTruncated
				   << ",\"db_write_failure\":" << a_quality.dbWriteFailure
				   << ",\"raw_export_failure\":" << a_quality.rawExportFailure
				   << ",\"manifest_failure\":" << a_quality.manifestFailure
				   << ",\"hook_observer_gap\":" << a_quality.hookObserverGap
				   << ",\"writer_drain_failure\":" << a_quality.writerDrainFailure
				   << ",\"lifecycle_failure\":" << a_quality.lifecycleFailure
				   << ",\"configuration_failure\":" << a_quality.configurationFailure
				   << '}';
		}

		void AppendShape(std::ostringstream& a_json, const ManifestShape& a_shape)
		{
			a_json << "{\"profile\":";
			JsonOptionalString(a_json, a_shape.profile);
			a_json << ",\"cb_count\":";
			JsonOptionalInt(a_json, a_shape.cbCount);
			a_json << ",\"srv_count\":";
			JsonOptionalInt(a_json, a_shape.srvCount);
			a_json << ",\"uav_count\":";
			JsonOptionalInt(a_json, a_shape.uavCount);
			a_json << ",\"sampler_count\":";
			JsonOptionalInt(a_json, a_shape.samplerCount);
			a_json << ",\"output_count\":";
			JsonOptionalInt(a_json, a_shape.outputCount);
			a_json << ",\"input_count\":";
			JsonOptionalInt(a_json, a_shape.inputCount);
			a_json << ",\"input_has_position_only\":";
			JsonOptionalInt(a_json, a_shape.inputHasPositionOnly);
			a_json << ",\"instruction_count\":";
			JsonOptionalInt(a_json, a_shape.instructionCount);
			a_json << ",\"sample_call_count\":";
			JsonOptionalInt(a_json, a_shape.sampleCallCount);
			a_json << ",\"input_signature_summary\":";
			JsonOptionalString(a_json, a_shape.inputSignatureSummary);
			a_json << ",\"output_signature_summary\":";
			JsonOptionalString(a_json, a_shape.outputSignatureSummary);
			a_json << ",\"resource_summary\":";
			JsonOptionalString(a_json, a_shape.resourceSummary);
			a_json << '}';
		}

		void AppendStreamOutput(
			std::ostringstream& a_json,
			const StreamOutputIdentity& a_streamOutput)
		{
			if (!a_streamOutput.present) {
				a_json << "null";
				return;
			}
			a_json << "{\"encoding\":\"fo4cs.d3d11-stream-output.v1\",\"state\":";
			JsonString(
				a_json, StreamOutputStateName(a_streamOutput.state));
			a_json << ",\"valid\":";
			JsonBool(a_json, a_streamOutput.valid);
			a_json << ",\"digest_sha256\":";
			if (a_streamOutput.digestSha256.empty())
				a_json << "null";
			else
				JsonString(a_json, a_streamOutput.digestSha256);
			a_json << ",\"declaration_state\":";
			JsonString(a_json, a_streamOutput.declarationState);
			a_json << ",\"declaration_count\":" << a_streamOutput.declarationCount
				   << ",\"strides_state\":";
			JsonString(a_json, a_streamOutput.stridesState);
			a_json << ",\"stride_count\":" << a_streamOutput.strideCount
				   << ",\"rasterized_stream\":" << a_streamOutput.rasterizedStream
				   << ",\"metadata_truncated\":";
			JsonBool(a_json, a_streamOutput.metadataTruncated);
			a_json << ",\"copy_failure\":";
			JsonBool(a_json, a_streamOutput.copyFailure);
			a_json << '}';
		}
	}

	bool IsValidUtf8(std::string_view a_value) noexcept
	{
		for (std::size_t offset = 0; offset < a_value.size();) {
			const auto length = Utf8SequenceLength(a_value, offset);
			if (length == 0)
				return false;
			offset += length;
		}
		return true;
	}

	bool QualityCounters::HasLossOrFailure() const noexcept
	{
		return queueOverflow != 0
			|| malformedBytecode != 0
			|| unsupportedSize != 0
			|| allocationFailure != 0
			|| copyFailure != 0
			|| hashFailure != 0
			|| metadataTruncated != 0
			|| dbWriteFailure != 0
			|| rawExportFailure != 0
			|| manifestFailure != 0
			|| hookObserverGap != 0
			|| writerDrainFailure != 0
			|| lifecycleFailure != 0
			|| configurationFailure != 0;
	}

	std::string_view BytecodeStateName(BytecodeState a_state) noexcept
	{
		switch (a_state) {
		case BytecodeState::kNull:
			return "null";
		case BytecodeState::kEmpty:
			return "empty";
		case BytecodeState::kExact:
			return "exact";
		case BytecodeState::kUnsupportedSize:
			return "unsupported_size";
		case BytecodeState::kAllocationFailure:
			return "allocation_failure";
		case BytecodeState::kCopyFailure:
			return "copy_failure";
		case BytecodeState::kHashFailure:
			return "hash_failure";
		}
		return "unknown";
	}

	std::string_view StreamOutputStateName(
		StreamOutputState a_state) noexcept
	{
		switch (a_state) {
		case StreamOutputState::kNotApplicable:
			return "not_applicable";
		case StreamOutputState::kExact:
			return "exact";
		case StreamOutputState::kUnsupportedSize:
			return "unsupported_size";
		case StreamOutputState::kAllocationFailure:
			return "allocation_failure";
		case StreamOutputState::kCopyFailure:
			return "copy_failure";
		case StreamOutputState::kHashFailure:
			return "hash_failure";
		case StreamOutputState::kMetadataTruncated:
			return "metadata_truncated";
		}
		return "unknown";
	}

	bool ComputeDigests(const void* a_data, std::size_t a_size, ContentDigest& a_result) noexcept
	{
		if (!a_data || a_size == 0)
			return false;
		Sha1InitOnce();
		const auto sha1 = Sha1Compute(a_data, a_size);
		if (Sha1IsZero(sha1)
			|| !HashSha256(a_data, a_size, a_result.sha256))
			return false;
		a_result.sha1 = sha1.bytes;
		return true;
	}

	std::string HexLower(const std::uint8_t* a_data, std::size_t a_size)
	{
		static constexpr char kHex[] = "0123456789abcdef";
		std::string result(a_size * 2, '0');
		for (std::size_t i = 0; i < a_size; ++i) {
			result[i * 2] = kHex[(a_data[i] >> 4) & 0xf];
			result[i * 2 + 1] = kHex[a_data[i] & 0xf];
		}
		return result;
	}

	bool IsLowerHexDigest(std::string_view a_value, std::size_t a_size) noexcept
	{
		return a_value.size() == a_size
			&& std::all_of(a_value.begin(), a_value.end(), [](char a_ch) {
				return (a_ch >= '0' && a_ch <= '9')
					|| (a_ch >= 'a' && a_ch <= 'f');
			});
	}

	std::optional<std::string> GenerateUuidV4() noexcept
	{
		std::array<unsigned char, 16> bytes{};
		if (!NT_SUCCESS(BCryptGenRandom(
				nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
				BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
			return std::nullopt;
		bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
		bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
		char output[37]{};
		std::snprintf(
			output, sizeof(output),
			"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			bytes[0], bytes[1], bytes[2], bytes[3],
			bytes[4], bytes[5], bytes[6], bytes[7],
			bytes[8], bytes[9], bytes[10], bytes[11],
			bytes[12], bytes[13], bytes[14], bytes[15]);
		return std::string(output);
	}

	RunPolicy ParseRunPolicy(const EnvironmentValues& a_values)
	{
		RunPolicy policy;
		if (a_values.evidenceMode) {
			if (*a_values.evidenceMode == "true") {
				policy.evidenceMode = true;
			} else if (*a_values.evidenceMode == "false") {
				policy.evidenceMode = false;
			} else {
				policy.environmentValid = false;
				policy.errors.emplace_back(
					"FO4CS_SHADER_CATALOG_EVIDENCE_MODE must be exactly true or false");
			}
		}

		policy.externalRunId = a_values.externalRunId;
		policy.scenarioId = a_values.scenarioId;
		policy.configId = a_values.configId;
		policy.sourceId = a_values.sourceId;
		ValidateIdentifier("FO4CS_SHADER_CATALOG_RUN_ID", policy.externalRunId, policy);
		ValidateIdentifier("FO4CS_SHADER_CATALOG_SCENARIO_ID", policy.scenarioId, policy);
		ValidateIdentifier("FO4CS_SHADER_CATALOG_CONFIG_ID", policy.configId, policy);
		ValidateIdentifier("FO4CS_SHADER_CATALOG_SOURCE_ID", policy.sourceId, policy);

		policy.evidenceIdsSatisfied = !policy.evidenceMode
			|| (policy.externalRunId.has_value() && policy.scenarioId.has_value());
		if (!policy.evidenceIdsSatisfied)
			policy.errors.emplace_back(
				"evidence mode requires valid run and scenario identifiers");

		if (a_values.corpusRoot) {
			policy.rawExportRequested = true;
			if (a_values.corpusRoot->empty()) {
				policy.environmentValid = false;
				policy.errors.emplace_back("FO4_SHADER_CORPUS_ROOT is empty");
			} else {
				const auto root = Utf8Path(*a_values.corpusRoot);
				if (!root || !root->is_absolute()
					|| (root->has_filename() && root->filename() == "..")) {
					policy.environmentValid = false;
					policy.errors.emplace_back(
						"FO4_SHADER_CORPUS_ROOT must be an absolute path");
				} else {
					policy.corpusRoot = root->lexically_normal();
					std::string error;
					policy.exportRootValid = ValidatePublicationRoot(*policy.corpusRoot, error);
					if (!policy.exportRootValid) {
						policy.environmentValid = false;
						policy.errors.emplace_back(
							"FO4_SHADER_CORPUS_ROOT rejected: " + error);
					}
				}
			}
		}
		return policy;
	}

	RunPolicy ReadRunPolicyFromEnvironment()
	{
		EnvironmentValues values;
		values.evidenceMode = ReadEnvironmentValue(L"FO4CS_SHADER_CATALOG_EVIDENCE_MODE");
		values.externalRunId = ReadEnvironmentValue(L"FO4CS_SHADER_CATALOG_RUN_ID");
		values.scenarioId = ReadEnvironmentValue(L"FO4CS_SHADER_CATALOG_SCENARIO_ID");
		values.configId = ReadEnvironmentValue(L"FO4CS_SHADER_CATALOG_CONFIG_ID");
		values.sourceId = ReadEnvironmentValue(L"FO4CS_SHADER_CATALOG_SOURCE_ID");
		values.corpusRoot = ReadEnvironmentValue(L"FO4_SHADER_CORPUS_ROOT");
		return ParseRunPolicy(values);
	}

	PreparedObservation PrepareObservation(
		char a_stage,
		const void* a_bytecode,
		std::size_t a_bytecodeSize,
		std::uint64_t a_sequence,
		ULONG a_stackFramesToSkip) noexcept
	{
		PreparedObservation result;
		result.stage = a_stage;
		result.submittedSize = a_bytecodeSize;
		result.sequence = a_sequence;
		result.sourceVa = reinterpret_cast<std::uintptr_t>(a_bytecode);
		result.threadId = GetCurrentThreadId();
		LARGE_INTEGER counter{};
		if (QueryPerformanceCounter(&counter))
			result.qpc = counter.QuadPart;
		void* frames[4]{};
		const USHORT captured = CaptureStackBackTrace(
			a_stackFramesToSkip, 4, frames, nullptr);
		for (USHORT i = 0; i < captured; ++i)
			result.stackFrames[i] = reinterpret_cast<std::uintptr_t>(frames[i]);

		if (!a_bytecode) {
			result.bytecodeState = BytecodeState::kNull;
			return result;
		}
		if (a_bytecodeSize == 0) {
			result.bytecodeState = BytecodeState::kEmpty;
			return result;
		}
		if (a_bytecodeSize > kMaxShaderBytecodeBytes) {
			result.bytecodeState = BytecodeState::kUnsupportedSize;
			return result;
		}

		result.bytecode.reset(new (std::nothrow) std::byte[a_bytecodeSize]);
		if (!result.bytecode) {
			result.bytecodeState = BytecodeState::kAllocationFailure;
			return result;
		}
		if (!GuardedCopyRaw(
				result.bytecode.get(), a_bytecode, a_bytecodeSize)) {
			result.bytecodeState = BytecodeState::kCopyFailure;
			result.bytecode.reset();
			return result;
		}

		ContentDigest digest{};
		if (!ComputeDigests(result.bytecode.get(), a_bytecodeSize, digest)) {
			result.bytecodeState = BytecodeState::kHashFailure;
			result.bytecode.reset();
			return result;
		}
		result.digest = digest;
		result.bytecodeState = BytecodeState::kExact;
		return result;
	}

	StreamOutputIdentity PrepareStreamOutputIdentity(
		const D3D11_SO_DECLARATION_ENTRY* a_declaration,
		UINT a_entryCount,
		const UINT* a_strides,
		UINT a_strideCount,
		UINT a_rasterizedStream) noexcept
	{
		StreamOutputIdentity result;
		result.present = true;
		result.state = StreamOutputState::kExact;
		result.declarationCount = a_entryCount;
		result.strideCount = a_strideCount;
		result.rasterizedStream = a_rasterizedStream;

		if (a_entryCount > kMaxStreamOutputEntries
			|| a_strideCount > kMaxStreamOutputStrides) {
			result.valid = false;
			result.state = StreamOutputState::kUnsupportedSize;
			result.declarationState = a_entryCount > kMaxStreamOutputEntries
				? "unsupported_count"
				: (a_declaration ? "present" : "null");
			result.stridesState = a_strideCount > kMaxStreamOutputStrides
				? "unsupported_count"
				: (a_strides ? "present" : "null");
			return result;
		}

		result.declarationState = a_declaration
			? (a_entryCount == 0 ? "empty" : "present")
			: "null";
		result.stridesState = a_strides
			? (a_strideCount == 0 ? "empty" : "present")
			: "null";
		if ((!a_declaration && a_entryCount != 0)
			|| (!a_strides && a_strideCount != 0)) {
			result.valid = false;
			result.state = StreamOutputState::kCopyFailure;
			result.copyFailure = true;
			return result;
		}

		try {
			std::vector<std::byte> encoded;
			encoded.reserve(
				32 + static_cast<std::size_t>(a_entryCount) * 64
				+ static_cast<std::size_t>(a_strideCount) * 4);
			constexpr char tag[] = "fo4cs.d3d11-stream-output.v1";
			const auto* tagBytes = reinterpret_cast<const std::byte*>(tag);
			encoded.insert(encoded.end(), tagBytes, tagBytes + sizeof(tag) - 1);
			encoded.push_back(a_declaration ? std::byte{ 1 } : std::byte{ 0 });
			AppendU32(encoded, a_entryCount);
			for (UINT i = 0; i < a_entryCount; ++i) {
				D3D11_SO_DECLARATION_ENTRY entry{};
				if (!GuardedCopyRaw(
						&entry, a_declaration + i, sizeof(entry))) {
					result.valid = false;
					result.state = StreamOutputState::kCopyFailure;
					result.copyFailure = true;
					result.declarationState = "copy_failure";
					return result;
				}
				AppendU32(encoded, entry.Stream);
				if (!AppendSemantic(
						encoded, entry.SemanticName,
						result.metadataTruncated)) {
					result.valid = false;
					result.state = StreamOutputState::kCopyFailure;
					result.copyFailure = true;
					result.declarationState = "copy_failure";
					return result;
				}
				AppendU32(encoded, entry.SemanticIndex);
				AppendU32(encoded, entry.StartComponent);
				AppendU32(encoded, entry.ComponentCount);
				AppendU32(encoded, entry.OutputSlot);
			}
			encoded.push_back(a_strides ? std::byte{ 1 } : std::byte{ 0 });
			AppendU32(encoded, a_strideCount);
			for (UINT i = 0; i < a_strideCount; ++i) {
				UINT stride = 0;
				if (!GuardedCopyRaw(
						&stride, a_strides + i, sizeof(stride))) {
					result.valid = false;
					result.state = StreamOutputState::kCopyFailure;
					result.copyFailure = true;
					result.stridesState = "copy_failure";
					return result;
				}
				AppendU32(encoded, stride);
			}
			AppendU32(encoded, a_rasterizedStream);

			ContentDigest digest{};
			if (!ComputeDigests(encoded.data(), encoded.size(), digest)) {
				result.valid = false;
				result.state = StreamOutputState::kHashFailure;
				return result;
			}
			result.digestSha256 = HexLower(digest.sha256.data(), digest.sha256.size());
			if (result.metadataTruncated)
				result.state = StreamOutputState::kMetadataTruncated;
		} catch (const std::bad_alloc&) {
			result.valid = false;
			result.state = StreamOutputState::kAllocationFailure;
		}
		return result;
	}

	std::string ObservationKey(const PreparedObservation& a_observation)
	{
		std::string key;
		key.reserve(160);
		key.push_back(a_observation.stage);
		key.push_back(':');
		key += BytecodeStateName(a_observation.bytecodeState);
		key.push_back(':');
		if (a_observation.digest) {
			key += HexLower(
				a_observation.digest->sha256.data(),
				a_observation.digest->sha256.size());
		} else {
			key += std::to_string(a_observation.submittedSize);
		}
		key += ":so:";
		if (!a_observation.streamOutput.present)
			key += "none";
		else if (!a_observation.streamOutput.digestSha256.empty())
			key += a_observation.streamOutput.digestSha256;
		else {
			key += "invalid:" + a_observation.streamOutput.declarationState + ":"
				+ std::to_string(a_observation.streamOutput.declarationCount) + ":"
				+ a_observation.streamOutput.stridesState + ":"
				+ std::to_string(a_observation.streamOutput.strideCount) + ":"
				+ std::to_string(a_observation.streamOutput.rasterizedStream) + ":"
				+ (a_observation.streamOutput.metadataTruncated ? "truncated" : "complete");
		}
		return key;
	}

	std::string BlobRelativePath(std::string_view a_sha256)
	{
		if (!IsLowerHexDigest(a_sha256, 64))
			return {};
		return "blobs/sha256/" + std::string(a_sha256.substr(0, 2)) + "/"
			+ std::string(a_sha256) + ".dxbc";
	}

	bool ValidatePublicationRoot(const std::filesystem::path& a_root, std::string& a_error)
	{
		std::shared_ptr<PublicationState> state;
		std::filesystem::path directory;
		return OpenPinnedDirectories(
			a_root, std::filesystem::path{}, state, directory, a_error);
	}

	PublicationResult PublishBlob(
		const std::filesystem::path& a_root,
		std::string_view a_sha256,
		const void* a_data,
		std::size_t a_size)
	{
		PublicationResult result;
		const auto relative = BlobRelativePath(a_sha256);
		if (relative.empty()) {
			result.error = "invalid blob digest";
			return result;
		}
		auto staged = StageImmutable(
			a_root, std::filesystem::path(relative),
			a_data, a_size, a_sha256);
		if (!staged.result.success)
			return staged.result;
		return PublishStagedState(staged);
	}

	PublicationResult PublishManifest(
		const std::filesystem::path& a_root,
		std::string_view a_generatedRunId,
		std::string_view a_json)
	{
		auto staged = StageManifest(
			a_root, a_generatedRunId, a_json);
		if (!staged.result.success)
			return staged.result;
		return PublishStagedManifest(staged);
	}

	StagedManifestPublication StageManifest(
		const std::filesystem::path& a_root,
		std::string_view a_generatedRunId,
		std::string_view a_json)
	{
		StagedManifestPublication staged;
		if (!ValidIdentifier(std::optional<std::string>(a_generatedRunId))) {
			staged.result.error = "invalid generated run identifier";
			return staged;
		}
		if (a_json.empty() || a_json.back() != '\n'
			|| a_json.size() > kMaxShaderBytecodeBytes
			|| !IsValidUtf8(a_json)) {
			staged.result.error =
				"manifest is invalid UTF-8, unterminated, or too large";
			return staged;
		}
		ContentDigest digest{};
		if (!ComputeDigests(a_json.data(), a_json.size(), digest)) {
			staged.result.error = "unable to hash manifest";
			return staged;
		}
		const auto sha256 = HexLower(digest.sha256.data(), digest.sha256.size());
		const auto relative = std::filesystem::path("runs")
			/ std::string(a_generatedRunId) / "manifest.v1.json";
		return StageImmutable(
			a_root, relative, a_json.data(), a_json.size(), sha256);
	}

	PublicationResult PublishStagedManifest(
		StagedManifestPublication& a_staged)
	{
		return PublishStagedState(a_staged);
	}

	void DiscardStagedManifest(
		StagedManifestPublication& a_staged) noexcept
	{
		a_staged.state.reset();
		a_staged.result.success = false;
	}

	PinnedPublishedFile PinPublishedFile(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_relative,
		std::size_t a_expectedSize,
		std::string_view a_expectedSha256)
	{
		PinnedPublishedFile pinned;
		if (a_relative.empty() || a_relative.has_root_path()
			|| a_expectedSize == 0
			|| a_expectedSize > kMaxShaderBytecodeBytes
			|| !IsLowerHexDigest(a_expectedSha256, 64)) {
			pinned.error = "published file verification input is invalid";
			return pinned;
		}
		std::shared_ptr<PublicationState> state;
		std::filesystem::path directory;
		if (!OpenPinnedDirectories(
				a_root, a_relative.parent_path(), state,
				directory, pinned.error))
			return pinned;
		state->expectedSize = a_expectedSize;
		state->expectedSha256 = a_expectedSha256;
		state->target = CreateFileW(
			LongPath(directory / a_relative.filename()).c_str(),
			GENERIC_READ,
			FILE_SHARE_READ,
			nullptr, OPEN_EXISTING,
			FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
			nullptr);
		if (state->target == INVALID_HANDLE_VALUE) {
			pinned.error = "unable to open published file";
			return pinned;
		}
		pinned.success = VerifyFileHandle(
			state->target, *state, pinned.error);
		if (pinned.success)
			pinned.state = std::move(state);
		return pinned;
	}

	bool VerifyPublishedFile(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_relative,
		std::size_t a_expectedSize,
		std::string_view a_expectedSha256,
		std::string& a_error)
	{
		auto pinned = PinPublishedFile(
			a_root, a_relative, a_expectedSize, a_expectedSha256);
		a_error = std::move(pinned.error);
		return pinned.success;
	}

	bool FingerprintPublicationRoot(
		const std::filesystem::path& a_root,
		std::string& a_fingerprint,
		std::string& a_error) noexcept
	{
		try {
			std::shared_ptr<PublicationState> state;
			std::filesystem::path directory;
			if (!OpenPinnedDirectories(
					a_root, {}, state, directory, a_error))
				return false;
			std::wstring folded(state->rootFinalPath.size(), L'\0');
			if (LCMapStringEx(
					LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
					state->rootFinalPath.data(),
					static_cast<int>(state->rootFinalPath.size()),
					folded.data(), static_cast<int>(folded.size()),
					nullptr, nullptr, 0)
				!= static_cast<int>(folded.size())) {
				a_error = "unable to normalize publication root";
				return false;
			}
			ContentDigest digest{};
			if (!ComputeDigests(
					folded.data(), folded.size() * sizeof(folded.front()),
					digest)) {
				a_error = "unable to hash publication root";
				return false;
			}
			a_fingerprint = HexLower(
				digest.sha256.data(), digest.sha256.size());
			return true;
		} catch (const std::bad_alloc&) {
			a_error = "unable to allocate publication root identity";
			return false;
		}
	}

#ifdef FO4CS_SHADER_CATALOG_TESTING
	void SetBeforeDirectoryCreateForTesting(
		BeforeDirectoryCreateForTesting a_callback) noexcept
	{
		g_beforeDirectoryCreate.store(
			a_callback, std::memory_order_release);
	}

	void HoldNextPublishedWinnerForTesting() noexcept
	{
		g_releasePublishedWinner.store(false, std::memory_order_release);
		g_publishedWinnerHeld.store(false, std::memory_order_release);
		g_publicationCollisionRetried.store(
			false, std::memory_order_release);
		g_holdNextPublishedWinner.store(true, std::memory_order_release);
	}

	bool PublishedWinnerHeldForTesting() noexcept
	{
		return g_publishedWinnerHeld.load(std::memory_order_acquire);
	}

	bool PublicationCollisionRetriedForTesting() noexcept
	{
		return g_publicationCollisionRetried.load(
			std::memory_order_acquire);
	}

	void ReleasePublishedWinnerForTesting() noexcept
	{
		g_releasePublishedWinner.store(true, std::memory_order_release);
		g_releasePublishedWinner.notify_all();
	}
#endif

	namespace
	{
		std::string_view RouteLineageStatusName(
			RouteLineageStatus a_status) noexcept
		{
			switch (a_status) {
			case RouteLineageStatus::kNotCreated:
				return "not-created";
			case RouteLineageStatus::kPendingBind:
				return "pending-bind";
			case RouteLineageStatus::kLinked:
				return "linked";
			case RouteLineageStatus::kAmbiguous:
				return "ambiguous";
			case RouteLineageStatus::kDuplicate:
				return "duplicate";
			case RouteLineageStatus::kRouteMismatch:
				return "route-mismatch";
			}
			return "not-created";
		}

		void AppendRouteStringArray(
			std::ostringstream& a_json,
			const std::vector<std::string>& a_values)
		{
			a_json << '[';
			for (std::size_t index = 0; index < a_values.size(); ++index) {
				if (index != 0)
					a_json << ',';
				JsonString(a_json, a_values[index]);
			}
			a_json << ']';
		}

		void AppendRouteCodeRange(
			std::ostringstream& a_json,
			const RouteCodeRange& a_range)
		{
			a_json << "{\"byte_length\":" << a_range.byteLength
				<< ",\"start_rva\":" << a_range.startRva << '}';
		}

		void AppendRouteResolverCodeRange(
			std::ostringstream& a_json,
			const RouteResolverCodeRange& a_range)
		{
			a_json << "{\"code_range\":";
			AppendRouteCodeRange(a_json, a_range.codeRange);
			a_json << ",\"code_sha256\":";
			JsonString(a_json, a_range.codeSha256);
			a_json << ",\"roles\":";
			AppendRouteStringArray(a_json, a_range.roles);
			a_json << ",\"rva\":" << a_range.rva << ",\"symbol\":";
			JsonString(a_json, a_range.symbol);
			a_json << '}';
		}

		void AppendRouteResolver(
			std::ostringstream& a_json,
			const RoutePluginRuntimeResolverIdentity& a_resolver)
		{
			a_json << "{\"code_ranges\":[";
			for (std::size_t index = 0;
				 index < a_resolver.codeRanges.size();
				 ++index) {
				if (index != 0)
					a_json << ',';
				AppendRouteResolverCodeRange(
					a_json, a_resolver.codeRanges[index]);
			}
			a_json << "],\"formula_id\":";
			JsonString(a_json, a_resolver.formulaId);
			a_json << ",\"implementation_sha256\":";
			JsonString(a_json, a_resolver.implementationSha256);
			a_json << ",\"name\":";
			JsonString(a_json, a_resolver.name);
			a_json << ",\"version\":";
			JsonString(a_json, a_resolver.version);
			a_json << '}';
		}

		void AppendRouteProducer(
			std::ostringstream& a_json,
			const RouteProducerIdentity& a_producer)
		{
			a_json << "{\"binary_sha256\":";
			JsonString(a_json, a_producer.binarySha256);
			a_json << ",\"name\":";
			JsonString(a_json, a_producer.name);
			a_json << ",\"version\":";
			JsonString(a_json, a_producer.version);
			a_json << '}';
		}

		void AppendRouteRuntime(
			std::ostringstream& a_json,
			const RouteRuntimeIdentity& a_runtime)
		{
			a_json << "{\"executable_sha256\":";
			JsonString(a_json, a_runtime.executableSha256);
			a_json << ",\"name\":";
			JsonString(a_json, a_runtime.name);
			a_json << ",\"version\":";
			JsonString(a_json, a_runtime.version);
			a_json << '}';
		}

		void AppendRouteRun(
			std::ostringstream& a_json,
			const RouteRunIdentity& a_run)
		{
			a_json << "{\"run_id\":";
			JsonString(a_json, a_run.runId);
			a_json << ",\"scenario_id\":";
			JsonString(a_json, a_run.scenarioId);
			a_json << ",\"stock_only\":";
			JsonBool(a_json, a_run.stockOnly);
			a_json << '}';
		}

		void AppendRouteSnapshot(
			std::ostringstream& a_json,
			const RouteSnapshot& a_route)
		{
			a_json << "{\"observed_lookup_psid\":";
			if (a_route.observedLookupPsid)
				a_json << *a_route.observedLookupPsid;
			else
				a_json << "null";
			a_json << ",\"plugin_resolved_psid\":";
			if (a_route.pluginResolvedPsid)
				a_json << *a_route.pluginResolvedPsid;
			else
				a_json << "null";
			a_json << ",\"plugin_runtime_resolver\":";
			if (a_route.pluginRuntimeResolver)
				AppendRouteResolver(
					a_json, *a_route.pluginRuntimeResolver);
			else
				a_json << "null";
			a_json << ",\"raw_technique\":" << a_route.rawTechnique
				<< ",\"stage\":";
			JsonString(a_json, a_route.stage);
			a_json << ",\"subclass\":";
			JsonString(a_json, a_route.subclass);
			a_json << ",\"tiled_lighting\":";
			if (a_route.tiledLighting)
				JsonBool(a_json, *a_route.tiledLighting);
			else
				a_json << "null";
			a_json << '}';
		}

		void AppendRouteBindSnapshot(
			std::ostringstream& a_json,
			const RouteBindSnapshot& a_route)
		{
			a_json << "{\"raw_technique\":" << a_route.rawTechnique
				<< ",\"stage\":";
			JsonString(a_json, a_route.stage);
			a_json << ",\"subclass\":";
			JsonString(a_json, a_route.subclass);
			a_json << ",\"tiled_lighting\":";
			if (a_route.tiledLighting)
				JsonBool(a_json, *a_route.tiledLighting);
			else
				a_json << "null";
			a_json << '}';
		}

		bool RouteAscii(std::string_view a_value) noexcept
		{
			if (a_value.empty())
				return false;
			return std::ranges::all_of(
				a_value,
				[](unsigned char a_character) {
					return a_character >= 0x20 && a_character <= 0x7e;
				});
		}

		bool RouteIdentityValid(
			const RouteProducerIdentity& a_producer,
			const RouteRuntimeIdentity& a_runtime,
			const RouteRunIdentity& a_run) noexcept
		{
			return RouteAscii(a_producer.name)
				&& RouteAscii(a_producer.version)
				&& IsLowerHexDigest(a_producer.binarySha256, 64)
				&& RouteAscii(a_runtime.name)
				&& RouteAscii(a_runtime.version)
				&& IsLowerHexDigest(a_runtime.executableSha256, 64)
				&& ValidIdentifier(
					std::optional<std::string>(a_run.runId))
				&& a_run.scenarioId
					== "stock-pixel-shader-routes-v1"
				&& a_run.stockOnly;
		}

		std::vector<std::string> SortUniqueRouteReasons(
			std::vector<std::string> a_reasons)
		{
			std::sort(a_reasons.begin(), a_reasons.end());
			a_reasons.erase(
				std::unique(a_reasons.begin(), a_reasons.end()),
				a_reasons.end());
			return a_reasons;
		}

		std::vector<std::string> DeriveRouteObservationReasons(
			const StockRuntimeRouteObservation& a_observation,
			bool a_globalStockOnlyViolation)
		{
			std::vector<std::string> reasons;
			if (!a_observation.facts.creationSucceeded)
				reasons.emplace_back("create-failed");
			if (!a_observation.facts.creationOutputNonNull)
				reasons.emplace_back("create-output-null");
			const bool usableStockObject =
				a_observation.facts.creationSucceeded
				&& a_observation.facts.creationOutputNonNull
				&& a_observation.stockAuthority.finalObjectStock
				&& *a_observation.stockAuthority.finalObjectStock;
			if (usableStockObject && !a_observation.bind)
				reasons.emplace_back("bind-missing");
			if (a_observation.lineage.status
				== RouteLineageStatus::kAmbiguous)
				reasons.emplace_back("lineage-ambiguous");
			if (a_observation.lineage.status
				== RouteLineageStatus::kDuplicate)
				reasons.emplace_back("lineage-duplicate");
			if (a_observation.bind
				&& a_observation.lineage.bindRouteContextMatch
				&& !*a_observation.lineage.bindRouteContextMatch)
				reasons.emplace_back("bind-route-mismatch");
			if (!a_observation.stockAuthority.originalInputUnchanged)
				reasons.emplace_back("original-input-changed");
			if (a_observation.stockAuthority.resolverInvoked)
				reasons.emplace_back("resolver-invoked");
			if (a_observation.facts.creationSucceeded
				&& a_observation.facts.creationOutputNonNull
				&& a_observation.stockAuthority.finalObjectStock
				&& !*a_observation.stockAuthority.finalObjectStock)
				reasons.emplace_back("final-object-non-stock");
			if (a_globalStockOnlyViolation)
				reasons.emplace_back("run-stock-only-violation");
			return SortUniqueRouteReasons(std::move(reasons));
		}

		bool RouteObservationAuthorityConditionsHold(
			const StockRuntimeRouteObservation& a_observation) noexcept
		{
			const auto projected = RouteBindSnapshot{
				a_observation.route.subclass,
				a_observation.route.stage,
				a_observation.route.rawTechnique,
				a_observation.route.tiledLighting
			};
			const bool routeMatches =
				a_observation.bind
				&& a_observation.bind->routeSnapshot
				&& *a_observation.bind->routeSnapshot == projected;
			return a_observation.run.stockOnly
				&& a_observation.facts.routeContextObserved
				&& a_observation.facts.creationObserved
				&& a_observation.facts.creationSucceeded
				&& a_observation.facts.creationOutputNonNull
				&& a_observation.facts.bindObserved
				&& !a_observation.facts.gpuExecutionObserved
				&& a_observation.facts.engineLookupObserved
					== a_observation.route.observedLookupPsid.has_value()
				&& a_observation.lineage.queueEnqueueSucceeded
				&& a_observation.lineage.queueEnqueueSequence
				&& a_observation.stockCreate.sequence
					< *a_observation.lineage.queueEnqueueSequence
				&& a_observation.bind
				&& *a_observation.lineage.queueEnqueueSequence
					< a_observation.bind->sequence
				&& a_observation.lineage.status
					== RouteLineageStatus::kLinked
				&& a_observation.lineage.shaderObjectId
				&& routeMatches
				&& a_observation.stockAuthority.originalInputUnchanged
				&& a_observation.stockAuthority.finalObjectStock
				&& *a_observation.stockAuthority.finalObjectStock
				&& !a_observation.stockAuthority.resolverInvoked
				&& (a_observation.route.pluginResolvedPsid.has_value()
					== a_observation.route.pluginRuntimeResolver.has_value());
		}

		bool ReadRouteFile(
			const std::filesystem::path& a_path,
			std::vector<std::byte>& a_bytes,
			std::string& a_error)
		{
			std::ifstream input(a_path, std::ios::binary | std::ios::ate);
			if (!input.is_open()) {
				a_error = "unable to open identity file";
				return false;
			}
			const auto end = input.tellg();
			if (end <= 0
				|| static_cast<std::uint64_t>(end)
					> static_cast<std::uint64_t>(
						std::numeric_limits<std::size_t>::max())) {
				a_error = "identity file size is invalid";
				return false;
			}
			a_bytes.resize(static_cast<std::size_t>(end));
			input.seekg(0, std::ios::beg);
			input.read(
				reinterpret_cast<char*>(a_bytes.data()),
				static_cast<std::streamsize>(a_bytes.size()));
			if (!input || input.gcount()
					!= static_cast<std::streamsize>(a_bytes.size())) {
				a_error = "unable to read identity file";
				a_bytes.clear();
				return false;
			}
			return true;
		}

		template <class T>
		bool ReadRouteStruct(
			std::span<const std::byte> a_bytes,
			std::size_t a_offset,
			T& a_value) noexcept
		{
			if (a_offset > a_bytes.size()
				|| sizeof(T) > a_bytes.size() - a_offset)
				return false;
			std::memcpy(
				&a_value, a_bytes.data() + a_offset, sizeof(T));
			return true;
		}

		struct RoutePeView
		{
			IMAGE_FILE_HEADER fileHeader{};
			IMAGE_OPTIONAL_HEADER64 optionalHeader{};
			std::vector<IMAGE_SECTION_HEADER> sections;
		};

		bool ParseRoutePe(
			std::span<const std::byte> a_bytes,
			RoutePeView& a_view,
			std::string& a_error)
		{
			IMAGE_DOS_HEADER dos{};
			if (!ReadRouteStruct(a_bytes, 0, dos)
				|| dos.e_magic != IMAGE_DOS_SIGNATURE
				|| dos.e_lfanew < 0) {
				a_error = "plugin PE DOS header is invalid";
				return false;
			}
			const auto ntOffset = static_cast<std::size_t>(dos.e_lfanew);
			DWORD signature = 0;
			if (!ReadRouteStruct(a_bytes, ntOffset, signature)
				|| signature != IMAGE_NT_SIGNATURE
				|| !ReadRouteStruct(
					a_bytes,
					ntOffset + sizeof(signature),
					a_view.fileHeader)
				|| a_view.fileHeader.Machine
					!= IMAGE_FILE_MACHINE_AMD64
				|| !ReadRouteStruct(
					a_bytes,
					ntOffset + sizeof(signature)
						+ sizeof(IMAGE_FILE_HEADER),
					a_view.optionalHeader)
				|| a_view.optionalHeader.Magic
					!= IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
				a_error = "plugin PE headers are invalid";
				return false;
			}
			const auto sectionOffset =
				ntOffset + sizeof(signature)
				+ sizeof(IMAGE_FILE_HEADER)
				+ a_view.fileHeader.SizeOfOptionalHeader;
			try {
				a_view.sections.resize(
					a_view.fileHeader.NumberOfSections);
			} catch (...) {
				a_error = "unable to allocate plugin PE sections";
				return false;
			}
			for (std::size_t index = 0;
				 index < a_view.sections.size();
				 ++index) {
				if (!ReadRouteStruct(
						a_bytes,
						sectionOffset
							+ index * sizeof(IMAGE_SECTION_HEADER),
						a_view.sections[index])) {
					a_error = "plugin PE section table is truncated";
					return false;
				}
			}
			return true;
		}

		std::optional<std::size_t> RouteRvaToFileOffset(
			std::span<const std::byte> a_bytes,
			const RoutePeView& a_view,
			std::uint32_t a_rva,
			std::string& a_error)
		{
			std::optional<std::size_t> result;
			for (const auto& section : a_view.sections) {
				const std::uint64_t start = section.VirtualAddress;
				const std::uint64_t end =
					start + section.SizeOfRawData;
				if (a_rva < start || a_rva >= end)
					continue;
				const std::uint64_t raw =
					static_cast<std::uint64_t>(
						section.PointerToRawData)
					+ (a_rva - start);
				if (raw >= a_bytes.size()) {
					a_error = "plugin PE RVA maps outside the file";
					return std::nullopt;
				}
				if (result) {
					a_error = "plugin PE RVA has overlapping mappings";
					return std::nullopt;
				}
				result = static_cast<std::size_t>(raw);
			}
			if (!result)
				a_error = "plugin PE RVA is not file-backed";
			return result;
		}

		bool ResolveRouteCodeIdentityFromBytes(
			std::span<const std::byte> a_bytes,
			std::uint32_t a_functionRva,
			std::string_view a_symbol,
			RouteCodeIdentity& a_identity,
			std::string& a_error)
		{
			RoutePeView view;
			if (!ParseRoutePe(a_bytes, view, a_error))
				return false;
			const auto& exceptionDirectory =
				view.optionalHeader.DataDirectory[
					IMAGE_DIRECTORY_ENTRY_EXCEPTION];
			if (exceptionDirectory.VirtualAddress == 0
				|| exceptionDirectory.Size
					< sizeof(RUNTIME_FUNCTION)
				|| exceptionDirectory.Size
					% sizeof(RUNTIME_FUNCTION) != 0) {
				a_error = "plugin PE exception directory is invalid";
				return false;
			}
			const auto exceptionOffset = RouteRvaToFileOffset(
				a_bytes,
				view,
				exceptionDirectory.VirtualAddress,
				a_error);
			if (!exceptionOffset
				|| exceptionDirectory.Size
					> a_bytes.size() - *exceptionOffset) {
				a_error = "plugin PE exception directory is truncated";
				return false;
			}
			std::optional<RUNTIME_FUNCTION> found;
			const auto count =
				exceptionDirectory.Size / sizeof(RUNTIME_FUNCTION);
			for (std::size_t index = 0; index < count; ++index) {
				RUNTIME_FUNCTION entry{};
				if (!ReadRouteStruct(
						a_bytes,
						*exceptionOffset
							+ index * sizeof(RUNTIME_FUNCTION),
						entry)) {
					a_error =
						"plugin PE runtime function is truncated";
					return false;
				}
				if (entry.BeginAddress != a_functionRva)
					continue;
				if (found) {
					a_error =
						"plugin PE runtime function is duplicated";
					return false;
				}
				found = entry;
			}
			if (!found || found->BeginAddress >= found->EndAddress) {
				a_error =
					"plugin PE runtime function range is unavailable";
				return false;
			}
			const auto length = static_cast<std::uint64_t>(
				found->EndAddress - found->BeginAddress);
			std::vector<std::byte> code;
			try {
				code.reserve(static_cast<std::size_t>(length));
			} catch (...) {
				a_error = "unable to allocate plugin code identity";
				return false;
			}
			for (std::uint64_t offset = 0; offset < length; ++offset) {
				const auto raw = RouteRvaToFileOffset(
					a_bytes,
					view,
					found->BeginAddress
						+ static_cast<std::uint32_t>(offset),
					a_error);
				if (!raw)
					return false;
				code.push_back(a_bytes[*raw]);
			}
			ContentDigest digest{};
			if (!ComputeDigests(code.data(), code.size(), digest)) {
				a_error = "unable to hash plugin code identity";
				return false;
			}
			a_identity = {
				.module = "FO4CommunityShaders.dll",
				.symbol = std::string(a_symbol),
				.rva = found->BeginAddress,
				.codeRange = {
					found->BeginAddress,
					length
				},
				.codeSha256 = HexLower(
					digest.sha256.data(), digest.sha256.size())
			};
			return true;
		}

		std::string BuildRouteResolverDescriptor(
			const RoutePluginRuntimeResolverIdentity& a_identity,
			std::string_view a_moduleSha256)
		{
			std::ostringstream json;
			json.imbue(std::locale::classic());
			json << "{\"code_ranges\":[";
			for (std::size_t index = 0;
				 index < a_identity.codeRanges.size();
				 ++index) {
				if (index != 0)
					json << ',';
				AppendRouteResolverCodeRange(
					json, a_identity.codeRanges[index]);
			}
			json << "],\"formula_id\":";
			JsonString(json, a_identity.formulaId);
			json << ",\"module\":\"FO4CommunityShaders.dll\","
				"\"module_sha256\":";
			JsonString(json, a_moduleSha256);
			json << ",\"name\":";
			JsonString(json, a_identity.name);
			json << ",\"schema\":"
				"\"fo4cs.plugin-runtime-resolver-implementation\","
				"\"schema_version\":1,\"version\":";
			JsonString(json, a_identity.version);
			json << "}\n";
			return json.str();
		}
	}

	std::string BuildCanonicalRouteObservation(
		const StockRuntimeRouteObservation& a_observation)
	{
		std::ostringstream json;
		json.imbue(std::locale::classic());
		json << "{\"authority_reasons\":";
		AppendRouteStringArray(json, a_observation.authorityReasons);
		json << ",\"bind\":";
		if (a_observation.bind) {
			json << "{\"code_range\":";
			AppendRouteCodeRange(
				json, a_observation.bind->hook.codeRange);
			json << ",\"code_sha256\":";
			JsonString(
				json, a_observation.bind->hook.codeSha256);
			json << ",\"event_id\":";
			JsonString(json, a_observation.bind->eventId);
			json << ",\"hook_module\":";
			JsonString(json, a_observation.bind->hook.module);
			json << ",\"hook_rva\":"
				<< a_observation.bind->hook.rva
				<< ",\"hook_symbol\":";
			JsonString(json, a_observation.bind->hook.symbol);
			json << ",\"route_snapshot\":";
			if (a_observation.bind->routeSnapshot)
				AppendRouteBindSnapshot(
					json, *a_observation.bind->routeSnapshot);
			else
				json << "null";
			json << ",\"sequence\":"
				<< a_observation.bind->sequence
				<< ",\"thread_id\":"
				<< a_observation.bind->threadId << '}';
		} else {
			json << "null";
		}
		json << ",\"bytecode\":{\"byte_length\":"
			<< a_observation.byteLength << ",\"sha1\":";
		JsonString(json, a_observation.sha1);
		json << ",\"sha256\":";
		JsonString(json, a_observation.sha256);
		json << "},\"capture_authoritative\":";
		JsonBool(json, a_observation.captureAuthoritative);
		json << ",\"facts\":{\"bind_observed\":";
		JsonBool(json, a_observation.facts.bindObserved);
		json << ",\"creation_observed\":";
		JsonBool(json, a_observation.facts.creationObserved);
		json << ",\"creation_output_nonnull\":";
		JsonBool(json, a_observation.facts.creationOutputNonNull);
		json << ",\"creation_succeeded\":";
		JsonBool(json, a_observation.facts.creationSucceeded);
		json << ",\"engine_lookup_observed\":";
		JsonBool(json, a_observation.facts.engineLookupObserved);
		json << ",\"gpu_execution_observed\":";
		JsonBool(json, a_observation.facts.gpuExecutionObserved);
		json << ",\"route_context_observed\":";
		JsonBool(json, a_observation.facts.routeContextObserved);
		json << "},\"lineage\":{\"bind_route_context_match\":";
		if (a_observation.lineage.bindRouteContextMatch)
			JsonBool(
				json,
				*a_observation.lineage.bindRouteContextMatch);
		else
			json << "null";
		json << ",\"pointer_lineage_event_id\":";
		JsonOptionalString(
			json, a_observation.lineage.pointerLineageEventId);
		json << ",\"queue_enqueue_sequence\":";
		if (a_observation.lineage.queueEnqueueSequence)
			json << *a_observation.lineage.queueEnqueueSequence;
		else
			json << "null";
		json << ",\"queue_enqueue_succeeded\":";
		JsonBool(
			json,
			a_observation.lineage.queueEnqueueSucceeded);
		json << ",\"shader_object_id\":";
		JsonOptionalString(
			json, a_observation.lineage.shaderObjectId);
		json << ",\"status\":";
		JsonString(
			json,
			RouteLineageStatusName(a_observation.lineage.status));
		json << "},\"producer\":";
		AppendRouteProducer(json, a_observation.producer);
		json << ",\"record\":{\"record_id\":";
		JsonString(json, a_observation.recordId);
		json << ",\"sequence\":"
			<< a_observation.recordSequence
			<< "},\"route\":";
		AppendRouteSnapshot(json, a_observation.route);
		json << ",\"run\":";
		AppendRouteRun(json, a_observation.run);
		json << ",\"runtime\":";
		AppendRouteRuntime(json, a_observation.runtime);
		json << ",\"schema\":"
			"\"fo4cs.stock-runtime-route-observation\","
			"\"schema_version\":1,\"stock_authority\":"
			"{\"final_object_stock\":";
		if (a_observation.stockAuthority.finalObjectStock)
			JsonBool(
				json,
				*a_observation.stockAuthority.finalObjectStock);
		else
			json << "null";
		json << ",\"original_input_unchanged\":";
		JsonBool(
			json,
			a_observation.stockAuthority.originalInputUnchanged);
		json << ",\"resolver_invoked\":";
		JsonBool(json, a_observation.stockAuthority.resolverInvoked);
		json << "},\"stock_create\":{\"class_linkage_state\":";
		JsonString(
			json, a_observation.stockCreate.classLinkageState);
		json << ",\"code_range\":";
		AppendRouteCodeRange(
			json, a_observation.stockCreate.hook.codeRange);
		json << ",\"code_sha256\":";
		JsonString(
			json, a_observation.stockCreate.hook.codeSha256);
		json << ",\"event_id\":";
		JsonString(json, a_observation.stockCreate.eventId);
		json << ",\"hook_module\":";
		JsonString(json, a_observation.stockCreate.hook.module);
		json << ",\"hook_rva\":"
			<< a_observation.stockCreate.hook.rva
			<< ",\"hook_symbol\":";
		JsonString(json, a_observation.stockCreate.hook.symbol);
		json << ",\"sequence\":"
			<< a_observation.stockCreate.sequence
			<< ",\"thread_id\":"
			<< a_observation.stockCreate.threadId << "}}\n";
		return json.str();
	}

	bool ComputeRouteFileSha256(
		const std::filesystem::path& a_path,
		std::string& a_sha256,
		std::uint64_t& a_length,
		std::string& a_error) noexcept
	{
		try {
			std::vector<std::byte> bytes;
			if (!ReadRouteFile(a_path, bytes, a_error))
				return false;
			ContentDigest digest{};
			if (!ComputeDigests(bytes.data(), bytes.size(), digest)) {
				a_error = "unable to hash identity file";
				return false;
			}
			a_length = bytes.size();
			a_sha256 = HexLower(
				digest.sha256.data(), digest.sha256.size());
			return true;
		} catch (...) {
			a_error = "unable to compute identity file digest";
			return false;
		}
	}

	bool ResolveRouteCodeIdentity(
		const std::filesystem::path& a_modulePath,
		std::uintptr_t a_loadedModuleBase,
		std::uintptr_t a_functionAddress,
		std::string_view a_symbol,
		RouteCodeIdentity& a_identity,
		std::string& a_error) noexcept
	{
		try {
			if (a_loadedModuleBase == 0
				|| a_functionAddress < a_loadedModuleBase
				|| a_functionAddress - a_loadedModuleBase
					> std::numeric_limits<std::uint32_t>::max()
				|| !RouteAscii(a_symbol)) {
				a_error = "plugin function identity input is invalid";
				return false;
			}
			std::vector<std::byte> bytes;
			if (!ReadRouteFile(a_modulePath, bytes, a_error))
				return false;
			return ResolveRouteCodeIdentityFromBytes(
				bytes,
				static_cast<std::uint32_t>(
					a_functionAddress - a_loadedModuleBase),
				a_symbol,
				a_identity,
				a_error);
		} catch (...) {
			a_error = "unable to resolve plugin function identity";
			return false;
		}
	}

	bool ResolveRoutePluginRuntimeResolverIdentity(
		const std::filesystem::path& a_modulePath,
		std::uintptr_t a_loadedModuleBase,
		std::string_view a_name,
		std::string_view a_version,
		std::string_view a_formulaId,
		std::span<const RouteResolverCodeRangeRequest> a_ranges,
		RoutePluginRuntimeResolverIdentity& a_identity,
		std::string& a_error) noexcept
	{
		try {
			if (!RouteAscii(a_name)
				|| !RouteAscii(a_version)
				|| !RouteAscii(a_formulaId)
				|| a_ranges.empty()) {
				a_error = "plugin resolver identity input is invalid";
				return false;
			}
			RoutePluginRuntimeResolverIdentity identity;
			identity.name = a_name;
			identity.version = a_version;
			identity.formulaId = a_formulaId;
			for (const auto& request : a_ranges) {
				if (request.roles.empty()) {
					a_error = "plugin resolver role set is empty";
					return false;
				}
				RouteCodeIdentity code;
				if (!ResolveRouteCodeIdentity(
						a_modulePath,
						a_loadedModuleBase,
						request.functionAddress,
						request.symbol,
						code,
						a_error)) {
					return false;
				}
				auto roles = request.roles;
				std::sort(roles.begin(), roles.end());
				if (std::adjacent_find(
						roles.begin(), roles.end())
					!= roles.end()) {
					a_error = "plugin resolver roles are duplicated";
					return false;
				}
				identity.codeRanges.push_back({
					std::move(roles),
					code.symbol,
					code.rva,
					code.codeRange,
					code.codeSha256
				});
			}
			std::sort(
				identity.codeRanges.begin(),
				identity.codeRanges.end(),
				[](const auto& a_left, const auto& a_right) {
					return std::tie(
						a_left.rva,
						a_left.symbol,
						a_left.roles)
						< std::tie(
							a_right.rva,
							a_right.symbol,
							a_right.roles);
				});
			std::vector<std::string> allRoles;
			for (const auto& range : identity.codeRanges)
				allRoles.insert(
					allRoles.end(),
					range.roles.begin(),
					range.roles.end());
			allRoles = SortUniqueRouteReasons(std::move(allRoles));
			const std::vector<std::string> expected{
				"formula",
				"runtime-gate",
				"tiled-state"
			};
			if (allRoles != expected) {
				a_error = "plugin resolver role coverage is incomplete";
				return false;
			}
			std::string moduleSha256;
			std::uint64_t moduleLength = 0;
			if (!ComputeRouteFileSha256(
					a_modulePath,
					moduleSha256,
					moduleLength,
					a_error)) {
				return false;
			}
			const auto descriptor =
				BuildRouteResolverDescriptor(
					identity, moduleSha256);
			ContentDigest digest{};
			if (!ComputeDigests(
					descriptor.data(), descriptor.size(), digest)) {
				a_error = "unable to hash plugin resolver identity";
				return false;
			}
			identity.implementationSha256 = HexLower(
				digest.sha256.data(), digest.sha256.size());
			a_identity = std::move(identity);
			return true;
		} catch (...) {
			a_error = "unable to resolve plugin resolver identity";
			return false;
		}
	}

	namespace
	{
		constexpr std::uint64_t kRouteAdmissionClosed = 1ull << 63;
		constexpr std::uint64_t kRouteAdmissionCountMask =
			kRouteAdmissionClosed - 1;
		constexpr std::size_t kRouteQueueCapacity = 8192;

		bool RouteCodeIdentityValid(
			const RouteCodeIdentity& a_identity) noexcept
		{
			return a_identity.module == "FO4CommunityShaders.dll"
				&& RouteAscii(a_identity.symbol)
				&& a_identity.rva == a_identity.codeRange.startRva
				&& a_identity.codeRange.byteLength != 0
				&& IsLowerHexDigest(a_identity.codeSha256, 64);
		}

		bool RouteResolverIdentityValid(
			const RoutePluginRuntimeResolverIdentity& a_identity) noexcept
		{
			if (!RouteAscii(a_identity.name)
				|| !RouteAscii(a_identity.version)
				|| !RouteAscii(a_identity.formulaId)
				|| !IsLowerHexDigest(
					a_identity.implementationSha256, 64)
				|| a_identity.codeRanges.empty())
				return false;
			if (!std::is_sorted(
					a_identity.codeRanges.begin(),
					a_identity.codeRanges.end(),
					[](const auto& a_left, const auto& a_right) {
						return std::tie(
							a_left.rva,
							a_left.symbol,
							a_left.roles)
							< std::tie(
								a_right.rva,
								a_right.symbol,
								a_right.roles);
					}))
				return false;
			std::vector<std::string> roles;
			for (const auto& range : a_identity.codeRanges) {
				if (!RouteAscii(range.symbol)
					|| range.rva != range.codeRange.startRva
					|| range.codeRange.byteLength == 0
					|| !IsLowerHexDigest(range.codeSha256, 64)
					|| range.roles.empty())
					return false;
				if (!std::is_sorted(
						range.roles.begin(), range.roles.end())
					|| std::adjacent_find(
						range.roles.begin(), range.roles.end())
						!= range.roles.end())
					return false;
				roles.insert(
					roles.end(),
					range.roles.begin(),
					range.roles.end());
			}
			roles = SortUniqueRouteReasons(std::move(roles));
			return roles == std::vector<std::string>{
				"formula", "runtime-gate", "tiled-state"
			};
		}

		std::string BuildRouteScopeDescriptor(
			const RouteRunIdentity& a_run,
			const RouteCaptureScope& a_scope)
		{
			std::ostringstream json;
			json.imbue(std::locale::classic());
			json << "{\"eligibility_rules\":";
			AppendRouteStringArray(json, a_scope.eligibilityRules);
			json << ",\"enabled\":";
			JsonBool(json, a_scope.enabled);
			json << ",\"included_stages\":";
			AppendRouteStringArray(json, a_scope.includedStages);
			json << ",\"included_subclasses\":";
			AppendRouteStringArray(json, a_scope.includedSubclasses);
			json << ",\"scenario_id\":";
			JsonString(json, a_run.scenarioId);
			json << ",\"schema\":"
				"\"fo4cs.stock-runtime-route-capture-scope\","
				"\"schema_version\":1}\n";
			return json.str();
		}

		bool RouteScopeValid(
			const RouteRunIdentity& a_run,
			const RouteCaptureScope& a_scope,
			std::string& a_digest)
		{
			const std::vector<std::string> expectedRules{
				"scoped-setup-technique",
				"stock-create-pixel-shader-callback",
				"stock-only-run"
			};
			if (!a_scope.enabled
				|| a_scope.includedSubclasses
					!= std::vector<std::string>{ "*" }
				|| a_scope.includedStages
					!= std::vector<std::string>{ "ps" }
				|| a_scope.eligibilityRules != expectedRules)
				return false;
			const auto descriptor =
				BuildRouteScopeDescriptor(a_run, a_scope);
			ContentDigest digest{};
			if (!ComputeDigests(
					descriptor.data(), descriptor.size(), digest))
				return false;
			a_digest = HexLower(
				digest.sha256.data(), digest.sha256.size());
			return a_scope.configurationSha256.empty()
				|| a_scope.configurationSha256 == a_digest;
		}

		RoutePublicationError MapRoutePublicationError(
			const PublicationResult& a_result) noexcept
		{
			if (a_result.error.find("collision") != std::string::npos
				|| (a_result.alreadyExisted && !a_result.success))
				return RoutePublicationError::kCollision;
			if (a_result.error.find("reparse") != std::string::npos
				|| a_result.error.find("escaped") != std::string::npos
				|| a_result.error.find("traversal") != std::string::npos)
				return RoutePublicationError::kUnsafePath;
			if (a_result.error.find("flush") != std::string::npos)
				return RoutePublicationError::kFlushFailed;
			return RoutePublicationError::kIoFailed;
		}
	}

	struct StockRuntimeRoutePublisher::Impl
	{
		struct ManifestRow
		{
			std::string recordId;
			std::uint64_t recordSequence = 0;
			std::string observationSha256;
			std::uint64_t observationByteLength = 0;
			std::string stockCreateEventId;
			std::uint64_t stockCreateSequence = 0;
			std::optional<std::string> bindEventId;
			std::optional<std::uint64_t> bindSequence;
			std::uint64_t enqueueSequence = 0;
			bool captureAuthoritative = false;
			StockRuntimeRouteObservation observation;
		};

		std::filesystem::path root;
		RouteProducerIdentity producer;
		RouteRuntimeIdentity runtime;
		RouteRunIdentity run;
		RouteCaptureScope scope;
		RouteResolverRegistrySnapshot registryClose;

		std::atomic<std::uint64_t> captureAdmission{ 0 };
		std::atomic<std::uint64_t> eventSequence{ 0 };
		std::atomic<std::uint64_t> recordSequence{ 0 };
		std::atomic<std::uint64_t> objectSequence{ 0 };

		std::atomic<std::uint64_t> observerCallbacks{ 0 };
		std::atomic<std::uint64_t> includedCallbacks{ 0 };
		std::atomic<std::uint64_t> excludedCallbacks{ 0 };
		std::atomic<std::uint64_t> excludedMissingRoute{ 0 };
		std::atomic<std::uint64_t> excludedAmbiguousRoute{ 0 };
		std::atomic<std::uint64_t> excludedSubclass{ 0 };
		std::atomic<std::uint64_t> excludedStage{ 0 };
		std::atomic<std::uint64_t> createAttempts{ 0 };
		std::atomic<std::uint64_t> queueAttempts{ 0 };
		std::atomic<std::uint64_t> queueSuccesses{ 0 };
		std::atomic<std::uint64_t> queueFailures{ 0 };
		std::atomic<std::uint64_t> persistenceAttempts{ 0 };
		std::atomic<std::uint64_t> persistenceSuccesses{ 0 };
		std::atomic<std::uint64_t> persistenceFailures{ 0 };
		std::atomic<std::uint64_t> excludedInputChanges{ 0 };
		std::atomic<std::uint64_t> excludedResolverInvocations{ 0 };
		std::atomic<std::uint64_t> excludedNonStockFinalObjects{ 0 };
		std::atomic<std::uint64_t> excludedCompletionLosses{ 0 };

		std::mutex recordsMutex;
		std::vector<std::shared_ptr<RouteCaptureRecordState>> records;

		std::mutex publicationMutex;
		bool captureFrozen = false;
		bool publisherAdmissionOpen = false;
		bool finalized = false;
		bool globalStockOnlyViolation = false;
		std::unordered_map<std::string, std::string> frozenDocuments;
		std::unordered_set<std::string> publishedRecords;
		std::vector<ManifestRow> rows;

		bool TryAcquireCapture() noexcept
		{
			auto admission =
				captureAdmission.load(std::memory_order_acquire);
			while ((admission & kRouteAdmissionClosed) == 0) {
				if ((admission & kRouteAdmissionCountMask)
					== kRouteAdmissionCountMask)
					return false;
				if (captureAdmission.compare_exchange_weak(
						admission,
						admission + 1,
						std::memory_order_acq_rel,
						std::memory_order_acquire))
					return true;
			}
			return false;
		}

		void ReleaseCapture() noexcept
		{
			const auto previous =
				captureAdmission.fetch_sub(
					1, std::memory_order_acq_rel);
			if ((previous & kRouteAdmissionCountMask) == 1)
				captureAdmission.notify_all();
		}

		void CloseCapture() noexcept
		{
			auto admission = captureAdmission.fetch_or(
				kRouteAdmissionClosed, std::memory_order_acq_rel);
			admission |= kRouteAdmissionClosed;
			while ((admission & kRouteAdmissionCountMask) != 0) {
				captureAdmission.wait(
					admission, std::memory_order_acquire);
				admission =
					captureAdmission.load(std::memory_order_acquire);
			}
		}

		std::uint64_t NextEventSequence() noexcept
		{
			return eventSequence.fetch_add(
				1, std::memory_order_relaxed) + 1;
		}

		RoutePublicationResult PublishDocument(
			std::string_view a_folder,
			std::string_view a_json)
		{
			RoutePublicationResult result;
			ContentDigest digest{};
			if (a_json.empty()
				|| a_json.back() != '\n'
				|| !ComputeDigests(
					a_json.data(), a_json.size(), digest)) {
				result.error =
					RoutePublicationError::kCanonicalizationFailed;
				return result;
			}
			const auto sha256 = HexLower(
				digest.sha256.data(), digest.sha256.size());
			auto staged = StageImmutable(
				root,
				std::filesystem::path(a_folder)
					/ (sha256 + ".json"),
				a_json.data(),
				a_json.size(),
				sha256);
			if (!staged.result.success) {
				result.error =
					MapRoutePublicationError(staged.result);
				return result;
			}
			const auto published = PublishStagedState(staged);
			if (!published.success) {
				result.error =
					MapRoutePublicationError(published);
				return result;
			}
			result.success = true;
			result.document.sha256 = digest.sha256;
			result.document.byteLength = a_json.size();
			result.document.path =
				std::filesystem::absolute(root)
				/ published.relativePath;
			return result;
		}
	};

	RouteCaptureAdmission::~RouteCaptureAdmission()
	{
		if (_owner)
			_owner->AbandonCreate(*this);
	}

	RouteCaptureAdmission::RouteCaptureAdmission(
		RouteCaptureAdmission&& a_other) noexcept :
		_owner(std::exchange(a_other._owner, nullptr)),
		_createSequence(a_other._createSequence),
		_threadId(a_other._threadId),
		_classLinkagePresent(a_other._classLinkagePresent),
		_included(a_other._included),
		_route(std::move(a_other._route))
	{}

	RouteCaptureAdmission& RouteCaptureAdmission::operator=(
		RouteCaptureAdmission&& a_other) noexcept
	{
		if (this != &a_other) {
			if (_owner)
				_owner->AbandonCreate(*this);
			_owner = std::exchange(a_other._owner, nullptr);
			_createSequence = a_other._createSequence;
			_threadId = a_other._threadId;
			_classLinkagePresent =
				a_other._classLinkagePresent;
			_included = a_other._included;
			_route = std::move(a_other._route);
		}
		return *this;
	}

	RouteBindAdmission::~RouteBindAdmission()
	{
		if (_owner)
			_owner->ReleaseBind(*this);
	}

	RouteBindAdmission::RouteBindAdmission(
		RouteBindAdmission&& a_other) noexcept :
		_owner(std::exchange(a_other._owner, nullptr))
	{}

	RouteBindAdmission& RouteBindAdmission::operator=(
		RouteBindAdmission&& a_other) noexcept
	{
		if (this != &a_other) {
			if (_owner)
				_owner->ReleaseBind(*this);
			_owner = std::exchange(a_other._owner, nullptr);
		}
		return *this;
	}

	StockRuntimeRoutePublisher::StockRuntimeRoutePublisher(
		std::unique_ptr<Impl> a_impl) :
		_impl(std::move(a_impl))
	{}

	StockRuntimeRoutePublisher::~StockRuntimeRoutePublisher() = default;

	std::unique_ptr<StockRuntimeRoutePublisher>
		StockRuntimeRoutePublisher::Open(
			const std::filesystem::path& a_publicationRoot,
			const RouteProducerIdentity& a_producer,
			const RouteRuntimeIdentity& a_runtime,
			const RouteRunIdentity& a_run,
			const RouteCaptureScope& a_scope,
			RoutePublicationError& a_error) noexcept
	{
		a_error = RoutePublicationError::kNone;
		try {
			if (!RouteIdentityValid(
					a_producer, a_runtime, a_run)) {
				a_error =
					RoutePublicationError::kInvalidIdentity;
				return nullptr;
			}
			std::string scopeDigest;
			if (!RouteScopeValid(a_run, a_scope, scopeDigest)
				|| !RouteCodeIdentityValid(a_scope.createHook)
				|| !RouteCodeIdentityValid(a_scope.bindHook)
				|| !RouteResolverIdentityValid(
					a_scope.pluginRuntimeResolver)) {
				a_error = RoutePublicationError::kInvalidScope;
				return nullptr;
			}
			if (!a_scope.resolverRegistryOpen.valid
				|| !a_scope.resolverRegistryOpen.empty
				|| !IsLowerHexDigest(
					a_scope.resolverRegistryOpen.sha256, 64)
				|| !a_scope.resolverRegistrySnapshot
				|| !a_scope.hookCoverageReady) {
				a_error =
					RoutePublicationError::kStockOnlyViolation;
				return nullptr;
			}
			constexpr std::string_view emptyRegistryDescriptor =
				"{\"resolvers\":[],\"schema\":"
				"\"fo4cs.broker-resolver-registry\","
				"\"schema_version\":1}\n";
			ContentDigest emptyRegistryDigest{};
			if (!ComputeDigests(
					emptyRegistryDescriptor.data(),
					emptyRegistryDescriptor.size(),
					emptyRegistryDigest)
				|| a_scope.resolverRegistryOpen.sha256
					!= HexLower(
						emptyRegistryDigest.sha256.data(),
						emptyRegistryDigest.sha256.size())) {
				a_error =
					RoutePublicationError::kStockOnlyViolation;
				return nullptr;
			}
			std::string rootError;
			if (!a_publicationRoot.is_absolute()
				|| !ValidatePublicationRoot(
					a_publicationRoot, rootError)) {
				a_error =
					rootError.find("reparse") != std::string::npos
					? RoutePublicationError::kUnsafePath
					: RoutePublicationError::kInvalidRoot;
				return nullptr;
			}
			auto impl = std::make_unique<Impl>();
			impl->root = a_publicationRoot;
			impl->producer = a_producer;
			impl->runtime = a_runtime;
			impl->run = a_run;
			impl->scope = a_scope;
			impl->scope.configurationSha256 =
				std::move(scopeDigest);
			impl->records.reserve(kRouteQueueCapacity);
			return std::unique_ptr<StockRuntimeRoutePublisher>(
				new StockRuntimeRoutePublisher(std::move(impl)));
		} catch (...) {
			a_error = RoutePublicationError::kInvalidScope;
			return nullptr;
		}
	}

	RouteCaptureAdmission StockRuntimeRoutePublisher::BeginCreate(
		const RouteCreateInput& a_input) noexcept
	{
		RouteCaptureAdmission admission;
		if (!_impl || !_impl->TryAcquireCapture())
			return admission;
		_impl->observerCallbacks.fetch_add(
			1, std::memory_order_relaxed);
		if (!a_input.routePresent) {
			_impl->excludedCallbacks.fetch_add(
				1, std::memory_order_relaxed);
			_impl->excludedMissingRoute.fetch_add(
				1, std::memory_order_relaxed);
			admission._owner = this;
			admission._included = false;
			return admission;
		}
		if (a_input.routeAmbiguous) {
			_impl->excludedCallbacks.fetch_add(
				1, std::memory_order_relaxed);
			_impl->excludedAmbiguousRoute.fetch_add(
				1, std::memory_order_relaxed);
			admission._owner = this;
			admission._included = false;
			return admission;
		}
		if (a_input.stage != "ps") {
			_impl->excludedCallbacks.fetch_add(
				1, std::memory_order_relaxed);
			_impl->excludedStage.fetch_add(
				1, std::memory_order_relaxed);
			admission._owner = this;
			admission._included = false;
			return admission;
		}
		if (a_input.subclass.empty()) {
			_impl->excludedCallbacks.fetch_add(
				1, std::memory_order_relaxed);
			_impl->excludedSubclass.fetch_add(
				1, std::memory_order_relaxed);
			admission._owner = this;
			admission._included = false;
			return admission;
		}
		_impl->includedCallbacks.fetch_add(
			1, std::memory_order_relaxed);
		_impl->createAttempts.fetch_add(
			1, std::memory_order_relaxed);
		_impl->queueAttempts.fetch_add(
			1, std::memory_order_relaxed);
		admission._owner = this;
		admission._included = true;
		admission._createSequence =
			_impl->NextEventSequence();
		admission._threadId = GetCurrentThreadId();
		admission._classLinkagePresent =
			a_input.classLinkagePresent;
		try {
			admission._route.subclass = a_input.subclass;
			admission._route.stage = a_input.stage;
			admission._route.rawTechnique =
				a_input.rawTechnique;
			admission._route.pluginResolvedPsid =
				a_input.pluginResolvedPsid;
			admission._route.tiledLighting =
				a_input.tiledLighting;
			if (a_input.pluginResolvedPsid) {
				admission._route.pluginRuntimeResolver =
					_impl->scope.pluginRuntimeResolver;
			}
		} catch (...) {
			AbandonCreate(admission);
		}
		return admission;
	}

	void StockRuntimeRoutePublisher::AbandonCreate(
		RouteCaptureAdmission& a_admission) noexcept
	{
		if (!_impl || a_admission._owner != this)
			return;
		if (a_admission._included) {
			_impl->queueFailures.fetch_add(
				1, std::memory_order_relaxed);
		} else {
			_impl->excludedCompletionLosses.fetch_add(
				1, std::memory_order_relaxed);
		}
		a_admission._owner = nullptr;
		_impl->ReleaseCapture();
	}

	RouteCreateCommitResult
		StockRuntimeRoutePublisher::CompleteCreate(
			RouteCaptureAdmission&& a_admission,
			const RouteCreateOutcome& a_outcome) noexcept
	{
		RouteCreateCommitResult result;
		if (!_impl || a_admission._owner != this)
			return result;
		if (!a_admission._included) {
			_impl->excludedInputChanges.fetch_add(
				!a_outcome.originalInputUnchanged,
				std::memory_order_relaxed);
			_impl->excludedResolverInvocations.fetch_add(
				a_outcome.resolverInvoked,
				std::memory_order_relaxed);
			_impl->excludedNonStockFinalObjects.fetch_add(
				a_outcome.creationSucceeded
					&& a_outcome.outputNonNull
					&& a_outcome.finalObjectStock
					&& !*a_outcome.finalObjectStock,
				std::memory_order_relaxed);
			a_admission._owner = nullptr;
			_impl->ReleaseCapture();
			return result;
		}
		auto fail = [&]() noexcept {
			_impl->queueFailures.fetch_add(
				1, std::memory_order_relaxed);
			a_admission._owner = nullptr;
			_impl->ReleaseCapture();
			return result;
		};
		try {
			if (a_outcome.byteLength == 0
				|| !IsLowerHexDigest(a_outcome.sha1, 40)
				|| !IsLowerHexDigest(a_outcome.sha256, 64))
				return fail();
			const auto recordId = GenerateUuidV4();
			const auto createEventId = GenerateUuidV4();
			if (!recordId || !createEventId)
				return fail();

			StockRuntimeRouteObservation observation;
			observation.producer = _impl->producer;
			observation.runtime = _impl->runtime;
			observation.run = _impl->run;
			observation.recordId = *recordId;
			observation.byteLength = a_outcome.byteLength;
			observation.sha1 = a_outcome.sha1;
			observation.sha256 = a_outcome.sha256;
			observation.route = std::move(a_admission._route);
			observation.stockCreate = {
				.eventId = *createEventId,
				.sequence = a_admission._createSequence,
				.threadId = a_admission._threadId,
				.hook = _impl->scope.createHook,
				.classLinkageState =
					a_admission._classLinkagePresent
						? "present"
						: "absent"
			};
			observation.facts.creationSucceeded =
				a_outcome.creationSucceeded;
			observation.facts.creationOutputNonNull =
				a_outcome.outputNonNull;
			observation.stockAuthority.originalInputUnchanged =
				a_outcome.originalInputUnchanged;
			observation.stockAuthority.finalObjectStock =
				a_outcome.finalObjectStock;
			observation.stockAuthority.resolverInvoked =
				a_outcome.resolverInvoked;
			result.usableStockObject =
				a_outcome.creationSucceeded
				&& a_outcome.outputNonNull
				&& a_outcome.finalObjectStock
				&& *a_outcome.finalObjectStock;
			if (result.usableStockObject) {
				const auto objectId =
					_impl->objectSequence.fetch_add(
						1, std::memory_order_relaxed) + 1;
				observation.lineage.shaderObjectId =
					"shader-object-" + std::to_string(objectId);
				observation.lineage.status =
					RouteLineageStatus::kPendingBind;
			}
			const auto record =
				std::make_shared<RouteCaptureRecordState>();
			record->observation = std::move(observation);
			const auto enqueueSequence =
				_impl->NextEventSequence();
			{
				std::scoped_lock lock(_impl->recordsMutex);
				if (_impl->records.size()
					>= kRouteQueueCapacity)
					return fail();
				record->observation.recordSequence =
					_impl->recordSequence.fetch_add(
						1, std::memory_order_relaxed) + 1;
				record->observation.lineage
					.queueEnqueueSucceeded = true;
				record->observation.lineage
					.queueEnqueueSequence = enqueueSequence;
				_impl->records.push_back(record);
			}
			_impl->queueSuccesses.fetch_add(
				1, std::memory_order_relaxed);
			result.enqueued = true;
			result.record = std::move(record);
			a_admission._owner = nullptr;
			_impl->ReleaseCapture();
			return result;
		} catch (...) {
			return fail();
		}
	}

	RouteBindAdmission StockRuntimeRoutePublisher::BeginBind() noexcept
	{
		RouteBindAdmission admission;
		if (_impl && _impl->TryAcquireCapture())
			admission._owner = this;
		return admission;
	}

	void StockRuntimeRoutePublisher::ReleaseBind(
		RouteBindAdmission& a_admission) noexcept
	{
		if (!_impl || a_admission._owner != this)
			return;
		a_admission._owner = nullptr;
		_impl->ReleaseCapture();
	}

	void StockRuntimeRoutePublisher::ReleaseBindReservation(
		const std::shared_ptr<RouteCaptureRecordState>& a_record) noexcept
	{
		if (!a_record)
			return;
		std::scoped_lock lock(a_record->mutex);
		a_record->bindReserved = false;
	}

	bool StockRuntimeRoutePublisher::RecordBind(
		RouteBindAdmission&& a_admission,
		const std::shared_ptr<RouteCaptureRecordState>& a_record,
		std::optional<RouteBindSnapshot> a_routeSnapshot) noexcept
	{
		if (!_impl
			|| a_admission._owner != this
			|| !a_record) {
			return false;
		}
		try {
			const auto sequence = _impl->NextEventSequence();
			const auto eventId = GenerateUuidV4();
			if (!eventId) {
				ReleaseBindReservation(a_record);
				ReleaseBind(a_admission);
				return false;
			}
			{
				std::scoped_lock lock(a_record->mutex);
				if (!a_record->bindReserved
					|| a_record->observation.lineage.status
						!= RouteLineageStatus::kPendingBind) {
					a_record->bindReserved = false;
					ReleaseBind(a_admission);
					return false;
				}
				RouteBindEvent bind{
					.eventId = *eventId,
					.sequence = sequence,
					.threadId = GetCurrentThreadId(),
					.hook = _impl->scope.bindHook,
					.routeSnapshot = std::move(a_routeSnapshot)
				};
				const RouteBindSnapshot creation{
					a_record->observation.route.subclass,
					a_record->observation.route.stage,
					a_record->observation.route.rawTechnique,
					a_record->observation.route.tiledLighting
				};
				const bool match =
					bind.routeSnapshot
					&& *bind.routeSnapshot == creation;
				a_record->observation.bind = std::move(bind);
				a_record->observation.facts.bindObserved = true;
				a_record->observation.lineage
					.pointerLineageEventId = *eventId;
				a_record->observation.lineage
					.bindRouteContextMatch = match;
				a_record->observation.lineage.status =
					match
						? RouteLineageStatus::kLinked
						: RouteLineageStatus::kRouteMismatch;
				a_record->bindReserved = false;
			}
			ReleaseBind(a_admission);
			return true;
		} catch (...) {
			ReleaseBindReservation(a_record);
			ReleaseBind(a_admission);
			return false;
		}
	}

	bool StockRuntimeRoutePublisher::CloseCaptureAdmissionAndFreeze(
		FrozenRouteSnapshot& a_snapshot,
		RoutePublicationError& a_error) noexcept
	{
		a_error = RoutePublicationError::kNone;
		if (!_impl) {
			a_error = RoutePublicationError::kInvalidRecord;
			return false;
		}
		try {
			_impl->CloseCapture();
			std::scoped_lock publicationLock(
				_impl->publicationMutex);
			if (_impl->captureFrozen) {
				a_error =
					RoutePublicationError::kCaptureAdmissionClosed;
				return false;
			}
			_impl->registryClose =
				_impl->scope.resolverRegistrySnapshot();
			if (!_impl->registryClose.valid
				|| !IsLowerHexDigest(
					_impl->registryClose.sha256, 64)) {
				a_error =
					RoutePublicationError::kStockOnlyViolation;
				return false;
			}
			constexpr std::string_view emptyRegistryDescriptor =
				"{\"resolvers\":[],\"schema\":"
				"\"fo4cs.broker-resolver-registry\","
				"\"schema_version\":1}\n";
			ContentDigest emptyRegistryDigest{};
			if (!ComputeDigests(
					emptyRegistryDescriptor.data(),
					emptyRegistryDescriptor.size(),
					emptyRegistryDigest)
				|| (_impl->registryClose.empty
					&& _impl->registryClose.sha256
					!= HexLower(
						emptyRegistryDigest.sha256.data(),
						emptyRegistryDigest.sha256.size()))) {
				a_error =
					RoutePublicationError::kStockOnlyViolation;
				return false;
			}
			bool originalInputChanges = false;
			bool resolverInvocations = false;
			bool nonStockFinalObjects = false;
			std::vector<std::shared_ptr<RouteCaptureRecordState>>
				records;
			{
				std::scoped_lock recordsLock(
					_impl->recordsMutex);
				records = _impl->records;
			}
			for (const auto& record : records) {
				std::scoped_lock lock(record->mutex);
				const auto& observation = record->observation;
				originalInputChanges =
					originalInputChanges
					|| !observation.stockAuthority
						.originalInputUnchanged;
				resolverInvocations =
					resolverInvocations
					|| observation.stockAuthority
						.resolverInvoked;
				nonStockFinalObjects =
					nonStockFinalObjects
					|| (observation.facts.creationSucceeded
						&& observation.facts
							.creationOutputNonNull
						&& observation.stockAuthority
							.finalObjectStock
						&& !*observation.stockAuthority
							.finalObjectStock);
			}
			_impl->globalStockOnlyViolation =
				!_impl->registryClose.valid
				|| !_impl->registryClose.empty
				|| _impl->registryClose.generation
					!= _impl->scope.resolverRegistryOpen.generation
				|| _impl->registryClose.sha256
					!= _impl->scope.resolverRegistryOpen.sha256
				|| originalInputChanges
				|| resolverInvocations
				|| nonStockFinalObjects
				|| _impl->excludedInputChanges.load(
					std::memory_order_relaxed) != 0
				|| _impl->excludedResolverInvocations.load(
					std::memory_order_relaxed) != 0
				|| _impl->excludedNonStockFinalObjects.load(
					std::memory_order_relaxed) != 0;
			a_snapshot.records.clear();
			for (const auto& record : records) {
				StockRuntimeRouteObservation observation;
				{
					std::scoped_lock lock(record->mutex);
					record->observation.authorityReasons =
						DeriveRouteObservationReasons(
							record->observation,
							_impl->globalStockOnlyViolation);
					record->observation.captureAuthoritative =
						record->observation.authorityReasons.empty()
						&& RouteObservationAuthorityConditionsHold(
							record->observation);
					if (!record->observation.captureAuthoritative
						&& record->observation
							.authorityReasons.empty()) {
						record->observation.authorityReasons = {
							"producer-declined"
						};
					}
					observation = record->observation;
				}
				const auto json =
					BuildCanonicalRouteObservation(observation);
				_impl->frozenDocuments.emplace(
					observation.recordId, json);
				a_snapshot.records.push_back({
					std::move(observation)
				});
			}
			_impl->captureFrozen = true;
			_impl->publisherAdmissionOpen = true;
			return true;
		} catch (...) {
			a_error = RoutePublicationError::kInvalidRecord;
			return false;
		}
	}

	RoutePublicationResult
		StockRuntimeRoutePublisher::PublishObservation(
			const FrozenRouteRecord& a_record) noexcept
	{
		RoutePublicationResult failure;
		if (!_impl) {
			failure.error = RoutePublicationError::kInvalidRecord;
			return failure;
		}
		try {
			std::scoped_lock lock(_impl->publicationMutex);
			if (!_impl->publisherAdmissionOpen) {
				failure.error =
					RoutePublicationError::kPublisherAdmissionClosed;
				return failure;
			}
			const auto expected = _impl->frozenDocuments.find(
				a_record.observation.recordId);
			const auto json =
				BuildCanonicalRouteObservation(
					a_record.observation);
			if (expected == _impl->frozenDocuments.end()
				|| expected->second != json
				|| _impl->publishedRecords.contains(
					a_record.observation.recordId)) {
				failure.error =
					RoutePublicationError::kInvalidRecord;
				return failure;
			}
			_impl->persistenceAttempts.fetch_add(
				1, std::memory_order_relaxed);
			auto result =
				_impl->PublishDocument("observations", json);
			if (!result.success) {
				_impl->persistenceFailures.fetch_add(
					1, std::memory_order_relaxed);
				return result;
			}
			_impl->persistenceSuccesses.fetch_add(
				1, std::memory_order_relaxed);
			_impl->publishedRecords.emplace(
				a_record.observation.recordId);
			_impl->rows.push_back({
				.recordId = a_record.observation.recordId,
				.recordSequence =
					a_record.observation.recordSequence,
				.observationSha256 = HexLower(
					result.document.sha256.data(),
					result.document.sha256.size()),
				.observationByteLength =
					result.document.byteLength,
				.stockCreateEventId =
					a_record.observation.stockCreate.eventId,
				.stockCreateSequence =
					a_record.observation.stockCreate.sequence,
				.bindEventId =
					a_record.observation.bind
						? std::optional<std::string>(
							a_record.observation.bind->eventId)
						: std::nullopt,
				.bindSequence =
					a_record.observation.bind
						? std::optional<std::uint64_t>(
							a_record.observation.bind->sequence)
						: std::nullopt,
				.enqueueSequence =
					*a_record.observation.lineage
						.queueEnqueueSequence,
				.captureAuthoritative =
					a_record.observation.captureAuthoritative,
				.observation = a_record.observation
			});
			return result;
		} catch (...) {
			_impl->persistenceFailures.fetch_add(
				1, std::memory_order_relaxed);
			failure.error = RoutePublicationError::kIoFailed;
			return failure;
		}
	}

	RoutePublicationResult StockRuntimeRoutePublisher::FinalizeRun() noexcept
	{
		RoutePublicationResult failure;
		if (!_impl) {
			failure.error = RoutePublicationError::kInvalidRecord;
			return failure;
		}
		try {
			std::scoped_lock lock(_impl->publicationMutex);
			if (_impl->finalized) {
				failure.error =
					RoutePublicationError::kAlreadyFinalized;
				return failure;
			}
			if (!_impl->captureFrozen) {
				failure.error =
					RoutePublicationError::kCaptureAdmissionClosed;
				return failure;
			}
			_impl->publisherAdmissionOpen = false;
			_impl->finalized = true;
			std::sort(
				_impl->rows.begin(),
				_impl->rows.end(),
				[](const auto& a_left, const auto& a_right) {
					return std::tie(
						a_left.recordSequence,
						a_left.recordId,
						a_left.observationSha256)
						< std::tie(
							a_right.recordSequence,
							a_right.recordId,
							a_right.observationSha256);
				});

			const auto observerCallbacks =
				_impl->observerCallbacks.load(
					std::memory_order_relaxed);
			const auto includedCallbacks =
				_impl->includedCallbacks.load(
					std::memory_order_relaxed);
			const auto excludedCallbacks =
				_impl->excludedCallbacks.load(
					std::memory_order_relaxed);
			const auto excludedMissing =
				_impl->excludedMissingRoute.load(
					std::memory_order_relaxed);
			const auto excludedAmbiguous =
				_impl->excludedAmbiguousRoute.load(
					std::memory_order_relaxed);
			const auto excludedSubclass =
				_impl->excludedSubclass.load(
					std::memory_order_relaxed);
			const auto excludedStage =
				_impl->excludedStage.load(
					std::memory_order_relaxed);
			const auto createAttempts =
				_impl->createAttempts.load(
					std::memory_order_relaxed);
			const auto queueAttempts =
				_impl->queueAttempts.load(
					std::memory_order_relaxed);
			const auto queueSuccesses =
				_impl->queueSuccesses.load(
					std::memory_order_relaxed);
			const auto queueFailures =
				_impl->queueFailures.load(
					std::memory_order_relaxed);
			const auto persistenceAttempts =
				_impl->persistenceAttempts.load(
					std::memory_order_relaxed);
			const auto persistenceSuccesses =
				_impl->persistenceSuccesses.load(
					std::memory_order_relaxed);
			const auto persistenceFailures =
				_impl->persistenceFailures.load(
					std::memory_order_relaxed);
			const auto allocatedSequences =
				_impl->eventSequence.load(
					std::memory_order_relaxed);
			const bool hookCoverageReady =
				_impl->scope.hookCoverageReady();

			std::unordered_set<std::uint64_t> committedSequences;
			std::unordered_set<std::string> recordIds;
			std::unordered_set<std::uint64_t> recordSequences;
			std::unordered_set<std::string> digests;
			std::unordered_set<std::string> eventIds;
			std::unordered_set<std::uint64_t> eventSequences;
			bool identityDuplicate = false;
			std::uint64_t failedCreates = 0;
			std::uint64_t nullOutputs = 0;
			std::uint64_t ambiguous = 0;
			std::uint64_t duplicate = 0;
			std::uint64_t mismatches = 0;
			std::uint64_t inputChanges = 0;
			std::uint64_t resolverInvocations = 0;
			std::uint64_t nonStock = 0;
			std::uint64_t incompleteBinds = 0;
			for (const auto& row : _impl->rows) {
				identityDuplicate =
					identityDuplicate
					|| !recordIds.emplace(row.recordId).second
					|| !recordSequences.emplace(
						row.recordSequence).second
					|| !digests.emplace(
						row.observationSha256).second
					|| !eventIds.emplace(
						row.stockCreateEventId).second
					|| !eventSequences.emplace(
						row.stockCreateSequence).second
					|| !eventSequences.emplace(
						row.enqueueSequence).second;
				if (row.bindEventId) {
					identityDuplicate =
						identityDuplicate
						|| !eventIds.emplace(
							*row.bindEventId).second
						|| !eventSequences.emplace(
							*row.bindSequence).second;
				}
				committedSequences.emplace(
					row.stockCreateSequence);
				committedSequences.emplace(
					row.enqueueSequence);
				if (row.bindSequence)
					committedSequences.emplace(
						*row.bindSequence);
				const auto& observation = row.observation;
				failedCreates +=
					!observation.facts.creationSucceeded;
				nullOutputs +=
					!observation.facts.creationOutputNonNull;
				ambiguous += observation.lineage.status
					== RouteLineageStatus::kAmbiguous;
				duplicate += observation.lineage.status
					== RouteLineageStatus::kDuplicate;
				mismatches += observation.lineage.status
					== RouteLineageStatus::kRouteMismatch;
				inputChanges +=
					!observation.stockAuthority
						.originalInputUnchanged;
				resolverInvocations +=
					observation.stockAuthority.resolverInvoked;
				nonStock +=
					observation.facts.creationSucceeded
					&& observation.facts
						.creationOutputNonNull
					&& observation.stockAuthority
						.finalObjectStock
					&& !*observation.stockAuthority
						.finalObjectStock;
				const bool usable =
					observation.facts.creationSucceeded
					&& observation.facts
						.creationOutputNonNull
					&& observation.stockAuthority
						.finalObjectStock
					&& *observation.stockAuthority
						.finalObjectStock;
				incompleteBinds += usable
					&& observation.lineage.status
						!= RouteLineageStatus::kLinked;
			}
			inputChanges += _impl->excludedInputChanges.load(
				std::memory_order_relaxed);
			resolverInvocations +=
				_impl->excludedResolverInvocations.load(
					std::memory_order_relaxed);
			nonStock +=
				_impl->excludedNonStockFinalObjects.load(
					std::memory_order_relaxed);
			const auto committedCount =
				static_cast<std::uint64_t>(
					committedSequences.size());
			const auto uncommitted =
				allocatedSequences >= committedCount
					? allocatedSequences - committedCount
					: allocatedSequences;
			const bool filterValid =
				observerCallbacks
					== includedCallbacks + excludedCallbacks
				&& excludedCallbacks
					== excludedMissing
						+ excludedAmbiguous
						+ excludedSubclass
						+ excludedStage
				&& createAttempts == includedCallbacks;
			const bool countersValid =
				queueAttempts == createAttempts
				&& queueSuccesses + queueFailures
					== queueAttempts
				&& persistenceAttempts == queueSuccesses
				&& persistenceSuccesses + persistenceFailures
					== persistenceAttempts
				&& persistenceSuccesses == _impl->rows.size();

			std::vector<std::string> reasons;
			if (!_impl->scope.enabled)
				reasons.emplace_back("scope-disabled");
			std::string scopeDigest;
			if (!RouteScopeValid(
					_impl->run, _impl->scope, scopeDigest)
				|| scopeDigest
					!= _impl->scope.configurationSha256)
				reasons.emplace_back(
					"scope-configuration-mismatch");
			if (!filterValid)
				reasons.emplace_back(
					"filter-accounting-mismatch");
			if (_impl->globalStockOnlyViolation)
				reasons.emplace_back("stock-only-violation");
			if (queueFailures != 0)
				reasons.emplace_back("queue-loss");
			if (persistenceFailures != 0)
				reasons.emplace_back("persistence-loss");
			if (uncommitted != 0)
				reasons.emplace_back("sequence-loss");
			if (!countersValid)
				reasons.emplace_back("counter-mismatch");
			if (_impl->excludedCompletionLosses.load(
					std::memory_order_relaxed) != 0)
				reasons.emplace_back("counter-mismatch");
			if (identityDuplicate)
				reasons.emplace_back("identity-duplicate");
			if (!hookCoverageReady && reasons.empty())
				reasons.emplace_back("producer-declined");
			reasons =
				SortUniqueRouteReasons(std::move(reasons));
			const bool authoritative = reasons.empty();

			std::ostringstream json;
			json.imbue(std::locale::classic());
			json << "{\"authority_reasons\":";
			AppendRouteStringArray(json, reasons);
			json << ",\"capture_authoritative\":";
			JsonBool(json, authoritative);
			json << ",\"capture_scope\":{\"configuration_sha256\":";
			JsonString(
				json, _impl->scope.configurationSha256);
			json << ",\"eligibility_rules\":";
			AppendRouteStringArray(
				json, _impl->scope.eligibilityRules);
			json << ",\"enabled\":";
			JsonBool(json, _impl->scope.enabled);
			json << ",\"included_stages\":";
			AppendRouteStringArray(
				json, _impl->scope.includedStages);
			json << ",\"included_subclasses\":";
			AppendRouteStringArray(
				json, _impl->scope.includedSubclasses);
			json << "},\"diagnostics\":{"
				"\"ambiguous_pointer_lineages\":" << ambiguous
				<< ",\"bind_route_context_mismatches\":"
				<< mismatches
				<< ",\"create_attempts\":" << createAttempts
				<< ",\"duplicate_pointer_lineages\":"
				<< duplicate
				<< ",\"failed_creates\":" << failedCreates
				<< ",\"incomplete_binds\":" << incompleteBinds
				<< ",\"non_stock_final_objects\":" << nonStock
				<< ",\"null_create_outputs\":" << nullOutputs
				<< ",\"original_input_changes\":" << inputChanges
				<< ",\"persistence_attempts\":"
				<< persistenceAttempts
				<< ",\"persistence_failures\":"
				<< persistenceFailures
				<< ",\"persistence_successes\":"
				<< persistenceSuccesses
				<< ",\"queue_enqueue_attempts\":"
				<< queueAttempts
				<< ",\"queue_enqueue_failures\":"
				<< queueFailures
				<< ",\"queue_enqueue_successes\":"
				<< queueSuccesses
				<< ",\"resolver_invocations\":"
				<< resolverInvocations
				<< "},\"filter_outcomes\":{"
				"\"excluded_ambiguous_route_context\":"
				<< excludedAmbiguous
				<< ",\"excluded_callbacks\":"
				<< excludedCallbacks
				<< ",\"excluded_missing_route_context\":"
				<< excludedMissing
				<< ",\"excluded_stage\":" << excludedStage
				<< ",\"excluded_subclass\":"
				<< excludedSubclass
				<< ",\"included_callbacks\":"
				<< includedCallbacks
				<< ",\"observer_callbacks\":"
				<< observerCallbacks
				<< "},\"observations\":[";
			for (std::size_t index = 0;
				 index < _impl->rows.size();
				 ++index) {
				if (index != 0)
					json << ',';
				const auto& row = _impl->rows[index];
				json << "{\"bind_event_id\":";
				JsonOptionalString(json, row.bindEventId);
				json << ",\"bind_sequence\":";
				if (row.bindSequence)
					json << *row.bindSequence;
				else
					json << "null";
				json << ",\"capture_authoritative\":";
				JsonBool(json, row.captureAuthoritative);
				json << ",\"observation_byte_length\":"
					<< row.observationByteLength
					<< ",\"observation_sha256\":";
				JsonString(json, row.observationSha256);
				json << ",\"record_id\":";
				JsonString(json, row.recordId);
				json << ",\"record_sequence\":"
					<< row.recordSequence
					<< ",\"stock_create_event_id\":";
				JsonString(json, row.stockCreateEventId);
				json << ",\"stock_create_sequence\":"
					<< row.stockCreateSequence << '}';
			}
			json << "],\"producer\":";
			AppendRouteProducer(json, _impl->producer);
			json << ",\"run\":";
			AppendRouteRun(json, _impl->run);
			json << ",\"runtime\":";
			AppendRouteRuntime(json, _impl->runtime);
			json << ",\"schema\":"
				"\"fo4cs.stock-runtime-route-run-manifest\","
				"\"schema_version\":1,"
				"\"sequence_accounting\":{"
				"\"allocated_sequences\":"
				<< allocatedSequences
				<< ",\"committed_sequences\":"
				<< committedCount
				<< ",\"first_sequence\":";
			if (allocatedSequences != 0)
				json << 1;
			else
				json << "null";
			json << ",\"last_sequence\":";
			if (allocatedSequences != 0)
				json << allocatedSequences;
			else
				json << "null";
			json << ",\"uncommitted_sequences\":"
				<< uncommitted
				<< "},\"stock_only_evidence\":{"
				"\"resolver_registry_empty_close\":";
			JsonBool(json, _impl->registryClose.empty);
			json << ",\"resolver_registry_empty_open\":";
			JsonBool(
				json, _impl->scope.resolverRegistryOpen.empty);
			json << ",\"resolver_registry_generation_close\":"
				<< _impl->registryClose.generation
				<< ",\"resolver_registry_generation_open\":"
				<< _impl->scope.resolverRegistryOpen.generation
				<< ",\"resolver_registry_sha256_close\":";
			JsonString(json, _impl->registryClose.sha256);
			json << ",\"resolver_registry_sha256_open\":";
			JsonString(
				json,
				_impl->scope.resolverRegistryOpen.sha256);
			json << "}}\n";
			return _impl->PublishDocument(
				"manifests", json.str());
		} catch (...) {
			failure.error = RoutePublicationError::kIoFailed;
			return failure;
		}
	}

	std::string BuildCanonicalManifest(ManifestDocument a_document)
	{
		std::sort(a_document.blobs.begin(), a_document.blobs.end(),
			[](const ManifestBlob& a_left, const ManifestBlob& a_right) {
				return a_left.sha256 < a_right.sha256;
			});
		std::sort(a_document.observations.begin(), a_document.observations.end(),
			[](const ManifestObservation& a_left, const ManifestObservation& a_right) {
				return a_left.key < a_right.key;
			});
		for (auto& observation : a_document.observations) {
			std::sort(
				observation.failedHresults.begin(),
				observation.failedHresults.end(),
				[](const ManifestHresult& a_left, const ManifestHresult& a_right) {
					return static_cast<std::uint32_t>(a_left.code)
						< static_cast<std::uint32_t>(a_right.code);
				});
		}
		std::sort(a_document.attributions.begin(), a_document.attributions.end(),
			[](const ManifestAttribution& a_left, const ManifestAttribution& a_right) {
				return std::tie(
					a_left.sha1, a_left.originatingStockSha1,
					a_left.subclassName, a_left.techniqueBits,
					a_left.attributionKind, a_left.objectKind)
					< std::tie(
						a_right.sha1, a_right.originatingStockSha1,
						a_right.subclassName, a_right.techniqueBits,
						a_right.attributionKind, a_right.objectKind);
			});

		std::ostringstream json;
		json.imbue(std::locale::classic());
		json << "{\"schema\":";
		JsonString(json, kManifestSchema);
		json << ",\"schema_version\":" << kManifestSchemaVersion
			 << ",\"producer\":{\"name\":\"FO4CommunityShaders\",\"version\":";
		JsonString(json, a_document.producerVersion);
		json << ",\"build_describe\":";
		JsonString(json, a_document.producerBuildDescribe);
		json << ",\"git_identity\":";
		JsonString(json, a_document.producerGitIdentity);
		json << "},\"catalog_schema_version\":" << kCatalogSchemaVersion
			 << ",\"generated_run_id\":";
		JsonString(json, a_document.generatedRunId);
		json << ",\"external_run_id\":";
		JsonOptionalString(json, a_document.externalRunId);
		json << ",\"scenario_id\":";
		JsonOptionalString(json, a_document.scenarioId);
		json << ",\"identity\":{\"runtime_family\":";
		JsonString(json, a_document.runtimeFamily);
		json << ",\"runtime_version\":";
		JsonOptionalString(json, a_document.runtimeVersion);
		json << ",\"process_id\":" << a_document.processId
			 << ",\"graphics_adapter\":";
		JsonOptionalString(json, a_document.graphicsAdapter);
		json << ",\"graphics_feature_level\":";
		JsonOptionalString(json, a_document.graphicsFeatureLevel);
		json << ",\"resolution_width\":";
		if (a_document.resolutionWidth)
			json << *a_document.resolutionWidth;
		else
			json << "null";
		json << ",\"resolution_height\":";
		if (a_document.resolutionHeight)
			json << *a_document.resolutionHeight;
		else
			json << "null";
		json << "},\"config\":{\"config_id\":";
		JsonOptionalString(json, a_document.configId);
		json << ",\"source_id\":";
		JsonOptionalString(json, a_document.sourceId);
		json << ",\"evidence_mode\":";
		JsonBool(json, a_document.evidenceMode);
		json << ",\"evidence_ids_satisfied\":";
		JsonBool(json, a_document.evidenceIdsSatisfied);
		json << ",\"raw_export_requested\":";
		JsonBool(json, a_document.rawExportRequested);
		json << ",\"writer_flush_interval_ms\":"
			 << a_document.writerFlushIntervalMs
			 << ",\"subclass_attribution_requested\":";
		JsonBool(json, a_document.subclassAttributionRequested);
		json << ",\"subclass_attribution_enabled\":";
		JsonBool(json, a_document.subclassAttributionEnabled);
		json << "},\"lifecycle\":{\"state\":";
		JsonString(json, a_document.lifecycle);
		json << ",\"started_at\":";
		JsonString(json, a_document.startedAt);
		json << ",\"ended_at\":";
		JsonOptionalString(json, a_document.endedAt);
		json << ",\"writer_drained\":";
		JsonBool(json, a_document.writerDrained);
		json << ",\"raw_export_complete\":";
		JsonBool(json, a_document.rawExportComplete);
		json << ",\"manifest_published\":";
		JsonBool(json, a_document.manifestPublished);
		json << ",\"hook_coverage_ready\":";
		JsonBool(json, a_document.hookCoverageReady);
		json << ",\"orderly_finalizer_ready\":";
		JsonBool(json, a_document.orderlyFinalizerReady);
		json << "},\"quality\":{\"authoritative\":";
		JsonBool(json, a_document.authoritative);
		json << ",\"counters\":";
		AppendQuality(json, a_document.quality);
		json << "},\"counters\":{\"attempts\":" << a_document.counters.attempts
			 << ",\"successes\":" << a_document.counters.successes
			 << ",\"failures\":" << a_document.counters.failures
			 << ",\"unique_observations\":" << a_document.counters.uniqueObservations
			 << ",\"unique_contents\":" << a_document.counters.uniqueContents
			 << ",\"attribution_events\":" << a_document.counters.attributionEvents
			 << "},\"blobs\":[";

		for (std::size_t i = 0; i < a_document.blobs.size(); ++i) {
			if (i != 0)
				json << ',';
			const auto& blob = a_document.blobs[i];
			json << "{\"sha256\":";
			JsonString(json, blob.sha256);
			json << ",\"sha1\":";
			JsonString(json, blob.sha1);
			json << ",\"size_bytes\":" << blob.sizeBytes
				 << ",\"relative_path\":";
			JsonOptionalString(json, blob.relativePath);
			json << ",\"shape\":";
			AppendShape(json, blob.shape);
			json << '}';
		}

		json << "],\"observations\":[";
		for (std::size_t i = 0; i < a_document.observations.size(); ++i) {
			if (i != 0)
				json << ',';
			const auto& observation = a_document.observations[i];
			json << "{\"key\":";
			JsonString(json, observation.key);
			json << ",\"stage\":";
			JsonString(json, observation.stage);
			json << ",\"sha256\":";
			JsonOptionalString(json, observation.sha256);
			json << ",\"sha1\":";
			JsonOptionalString(json, observation.sha1);
			json << ",\"bytecode\":{\"state\":";
			JsonString(json, observation.bytecodeState);
			json << ",\"submitted_size\":" << observation.submittedSize
				 << "},\"stream_output\":";
			AppendStreamOutput(json, observation.streamOutput);
			json << ",\"attempts\":" << observation.attempts
				 << ",\"successes\":" << observation.successes
				 << ",\"failures\":" << observation.failures
				 << ",\"output_requests\":" << observation.outputRequests
				 << ",\"null_outputs\":" << observation.nullOutputs
				 << ",\"raw_output_nonnull\":"
				 << observation.rawOutputNonNull
				 << ",\"resolver\":{\"invocations\":" << observation.resolverInvocations
				 << ",\"reported_replacements\":"
				 << observation.resolverReportedReplacements
				 << ",\"final_stock\":" << observation.finalStock
				 << ",\"final_replacement\":" << observation.finalReplacement
				 << ",\"final_null\":" << observation.finalNull
				 << ",\"replacement_sha256\":";
			JsonOptionalString(json, observation.replacementSha256);
			json << "},\"first_sequence\":" << observation.firstSequence
				 << ",\"last_sequence\":" << observation.lastSequence
				 << ",\"first_qpc\":" << observation.firstQpc
				 << ",\"last_qpc\":" << observation.lastQpc
				 << ",\"first_thread_id\":" << observation.firstThreadId
				 << ",\"first_module\":";
			JsonOptionalString(json, observation.firstModule);
			json << ",\"first_stack\":";
			JsonOptionalString(json, observation.firstStack);
			json << ",\"other_hresult_count\":"
				 << observation.otherHresultCount
				 << ",\"hresult_details_truncated\":";
			JsonBool(json, observation.hresultDetailsTruncated);
			json << ",\"failed_hresult_details\":[";
			for (std::size_t h = 0; h < observation.failedHresults.size(); ++h) {
				if (h != 0)
					json << ',';
				char code[11]{};
				std::snprintf(
					code, sizeof(code), "0x%08x",
					static_cast<std::uint32_t>(observation.failedHresults[h].code));
				json << "{\"hresult\":";
				JsonString(json, code);
				json << ",\"count\":" << observation.failedHresults[h].count << '}';
			}
			json << "]}";
		}

		json << "],\"attributions\":[";
		for (std::size_t i = 0; i < a_document.attributions.size(); ++i) {
			if (i != 0)
				json << ',';
			const auto& attribution = a_document.attributions[i];
			json << "{\"sha1\":";
			JsonOptionalString(json, attribution.sha1);
			json << ",\"originating_stock_sha1\":";
			JsonOptionalString(json, attribution.originatingStockSha1);
			json << ",\"sha256\":";
			JsonOptionalString(json, attribution.sha256);
			json << ",\"subclass\":";
			JsonString(json, attribution.subclassName);
			json << ",\"technique_bits\":";
			if (attribution.techniqueBits)
				json << *attribution.techniqueBits;
			else
				json << "null";
			json << ",\"attribution_kind\":";
			JsonString(json, attribution.attributionKind);
			json << ",\"object_kind\":";
			JsonString(json, attribution.objectKind);
			json << ",\"count\":" << attribution.count << '}';
		}
		json << "]}\n";
		return json.str();
	}
}
