#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Utils/ModuleInfo.h"

namespace cs::util
{
	[[nodiscard]] std::string ModuleFileName(HMODULE a_module);

	// Nullopt outside any module or inside the game or CS.
	[[nodiscard]] std::optional<std::string> ForeignModuleName(std::uintptr_t a_address);

	// Nullopt when the jmp chain from a_address ends in the game or CS.
	[[nodiscard]] std::optional<std::string> ForeignOwner(std::uintptr_t a_address);

	// Warns when a_code is another module's hook, which a_hook then chains onto.
	bool WarnIfForeignHook(std::string_view a_hook, std::uintptr_t a_code);
}
