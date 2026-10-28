#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Utils/ModuleInfo.h"

namespace cs::util
{
	[[nodiscard]] std::string ModuleFileName(HMODULE a_module);

	// Nullopt when the jmp chain from a_address ends in the game or CS.
	[[nodiscard]] std::optional<std::string> ForeignOwner(std::uintptr_t a_address);
}
