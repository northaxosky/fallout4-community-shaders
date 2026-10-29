#pragma once

#include <filesystem>

namespace cs::util
{
	// Documents\My Games\<save folder>\F4SE; empty when Documents is unresolved.
	[[nodiscard]] std::filesystem::path F4SEUserDirectory();
}
