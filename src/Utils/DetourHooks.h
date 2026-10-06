#pragma once

#include <Windows.h>

#include <cstdint>

namespace cs::hooks
{
	// Redirects *a_func (the target on entry, the original-call trampoline on success) to a_thunk,
	// warning first when another plugin already hijacked the target's prologue.
	// Logs and returns the Detours error on failure; *a_func is then left as the target.
	[[nodiscard]] LONG Attach(void** a_func, void* a_thunk, const char* a_typeName);
}
