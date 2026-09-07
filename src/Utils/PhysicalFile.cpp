#include "Utils/PhysicalFile.h"

#include <array>
#include <memory>
#include <string>

#include <Windows.h>
#include <psapi.h>

namespace cs::files
{
	std::expected<std::filesystem::path, std::error_code> PhysicalFilePath(
		const std::filesystem::path& a_path)
	{
		const auto error = [] {
			return std::unexpected(
				std::error_code(static_cast<int>(GetLastError()), std::system_category()));
		};
		const auto file = CreateFileW(
			a_path.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return error();
		const std::unique_ptr<void, decltype(&CloseHandle)> fileOwner(file, CloseHandle);
		const std::unique_ptr<void, decltype(&CloseHandle)> mapping(
			CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr),
			CloseHandle);
		if (!mapping)
			return error();
		const std::unique_ptr<void, decltype(&UnmapViewOfFile)> view(
			MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 1),
			UnmapViewOfFile);
		if (!view)
			return error();

		// USVFS rewrites file-name queries, but mapped-file names identify the real backing object.
		std::wstring native(32768, L'\0');
		const auto length = GetMappedFileNameW(
			GetCurrentProcess(), view.get(),
			native.data(), static_cast<DWORD>(native.size()));
		if (!length)
			return error();
		if (length >= native.size())
			return std::unexpected(std::make_error_code(std::errc::filename_too_long));
		native.resize(length);

		constexpr std::wstring_view networkPrefix = L"\\Device\\Mup\\";
		if (native.starts_with(networkPrefix))
			return std::filesystem::path(L"\\\\" + native.substr(networkPrefix.size()));

		std::array<wchar_t, 512> drives{};
		const auto driveLength = GetLogicalDriveStringsW(
			static_cast<DWORD>(drives.size()), drives.data());
		if (!driveLength)
			return error();
		if (driveLength >= drives.size())
			return std::unexpected(std::make_error_code(std::errc::value_too_large));
		for (const auto* drive = drives.data(); *drive;
			 drive += std::char_traits<wchar_t>::length(drive) + 1) {
			const std::wstring volume(drive, 2);
			std::array<wchar_t, 32768> device{};
			if (!QueryDosDeviceW(volume.c_str(), device.data(), static_cast<DWORD>(device.size())))
				continue;
			const std::wstring_view prefix(device.data());
			if (native.starts_with(prefix) && native.size() > prefix.size() &&
				native[prefix.size()] == L'\\') {
				return std::filesystem::path(volume + native.substr(prefix.size()));
			}
		}
		return std::unexpected(std::make_error_code(std::errc::no_such_device));
	}
}
