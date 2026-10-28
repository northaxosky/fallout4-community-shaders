#pragma once

#include <REX/W32/KERNEL32.h>

#include <expected>
#include <string_view>

namespace cs
{
	// Views an RCDATA resource of the plugin DLL; the error names the failed step.
	[[nodiscard]] inline std::expected<std::string_view, std::string_view> LoadRcData(int a_id)
	{
		const auto module = reinterpret_cast<HMODULE>(REX::W32::GetCurrentModule());
		if (!module)
			return std::unexpected("current module handle is unavailable");
		const auto resource = ::FindResourceW(module, MAKEINTRESOURCEW(a_id), RT_RCDATA);
		if (!resource)
			return std::unexpected("RCDATA resource was not found");
		const auto size = ::SizeofResource(module, resource);
		if (size == 0)
			return std::unexpected("RCDATA resource is empty");
		const auto loaded = ::LoadResource(module, resource);
		if (!loaded)
			return std::unexpected("RCDATA resource could not be loaded");
		const auto* data = static_cast<const char*>(::LockResource(loaded));
		if (!data)
			return std::unexpected("RCDATA resource could not be read");
		return std::string_view(data, size);
	}
}
