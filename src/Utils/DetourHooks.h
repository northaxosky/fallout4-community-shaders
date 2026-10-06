#pragma once

#include <Windows.h>

#include <cstdint>

namespace cs::hooks
{
	// On success *a_func is the original-call trampoline; on failure it is untouched.
	[[nodiscard]] LONG Attach(void** a_func, void* a_thunk, const char* a_typeName);

}
