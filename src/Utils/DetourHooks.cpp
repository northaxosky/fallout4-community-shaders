#include "Utils/DetourHooks.h"

#include "Log.h"

#include <detours.h>

#include <cstring>
#include <string_view>

namespace
{
	auto* L = cs::log::Get("cs.hooks");
}

namespace cs::hooks
{
	LONG Attach(void** a_func, void* a_thunk, const char* a_typeName)
	{
		LONG result = DetourTransactionBegin();
		if (result == NO_ERROR) {
			// A failed thread update must abort too, or the transaction commits half-initialized.
			result = DetourUpdateThread(GetCurrentThread());
			if (result == NO_ERROR)
				result = DetourAttach(a_func, a_thunk);
			if (result == NO_ERROR)
				result = DetourTransactionCommit();
			else
				DetourTransactionAbort();
		}
		if (result != NO_ERROR)
			L->error("Detours failed to hook {} (error {})", std::string_view{ a_typeName }, result);
		return result;
	}

	std::uintptr_t IATHook(
		std::uintptr_t a_module,
		const char* a_importModule,
		const char* a_api,
		std::uintptr_t a_detour)
	{
		struct Search
		{
			const char* importModule;
			const char* api;
			std::uintptr_t detour;
			bool inModule = false;
			std::uintptr_t previous = 0;
		} search{ a_importModule, a_api, a_detour };

		DetourEnumerateImportsEx(
			reinterpret_cast<HMODULE>(a_module), &search,
			[](PVOID a_context, HMODULE, PCSTR a_file) -> BOOL {
				auto& state = *static_cast<Search*>(a_context);
				state.inModule = a_file && _stricmp(a_file, state.importModule) == 0;
				return TRUE;
			},
			[](PVOID a_context, DWORD, PCSTR a_name, PVOID* a_slot) -> BOOL {
				auto& state = *static_cast<Search*>(a_context);
				if (!state.inModule || !a_name || std::strcmp(a_name, state.api) != 0)
					return TRUE;
				state.previous = reinterpret_cast<std::uintptr_t>(*a_slot);
				REL::Relocation<std::uintptr_t>{ reinterpret_cast<std::uintptr_t>(a_slot) }.write(state.detour);
				return FALSE;
			});
		return search.previous;
	}
}
