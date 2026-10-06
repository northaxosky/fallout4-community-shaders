#pragma once

#include <Windows.h>

#include <cstdint>

namespace cs::hooks
{
	// Redirects *a_func (the target on entry, the original-call trampoline on success) to a_thunk.
	// Logs and returns the Detours error on failure; *a_func is then left as the target.
	[[nodiscard]] LONG Attach(void** a_func, void* a_thunk, const char* a_typeName);

	// Swaps a_module's import of a_api from a_importModule for a_detour; returns the previous
	// target, or 0 when the import is absent.
	[[nodiscard]] std::uintptr_t IATHook(
		std::uintptr_t a_module,
		const char* a_importModule,
		const char* a_api,
		std::uintptr_t a_detour);
}
