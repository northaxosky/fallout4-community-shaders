#pragma once

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace cs::util
{
	// Does not bump the module refcount.
	[[nodiscard]] inline HMODULE ModuleFromAddress(const void* a_address) noexcept
	{
		HMODULE module = nullptr;
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCWSTR>(a_address), &module);
		return module;
	}

	// A null module names the executable.
	[[nodiscard]] inline std::optional<std::filesystem::path> ModulePath(HMODULE a_module)
	{
		constexpr std::size_t kMaxPathChars = 32768;
		std::wstring buffer(MAX_PATH, L'\0');
		for (;;) {
			const DWORD written = GetModuleFileNameW(a_module, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (written == 0)
				return std::nullopt;
			if (written < buffer.size()) {
				buffer.resize(written);
				return std::filesystem::path(std::move(buffer));
			}
			if (buffer.size() >= kMaxPathChars)
				return std::nullopt;
			buffer.resize(buffer.size() * 2);
		}
	}
}
