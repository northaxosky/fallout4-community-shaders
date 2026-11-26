#include "Utils/DetourHooks.h"

#include "Log.h"
#include "Utils/HookOwner.h"

#include <detours/detours.h>

#include <cstdint>
#include <string_view>

namespace
{
	auto* L = cs::log::Get("cs.hooks");

	std::string_view ShortTypeName(std::string_view a_typeName)
	{
		const auto split = a_typeName.find_last_of(": ");
		return split == std::string_view::npos ? a_typeName : a_typeName.substr(split + 1);
	}
}

namespace cs::hooks
{
	LONG Attach(void** a_func, void* a_thunk, const char* a_typeName)
	{
		const auto hookName = ShortTypeName(a_typeName);
		cs::util::WarnIfForeignHook(hookName, reinterpret_cast<std::uintptr_t>(*a_func));

		LONG result = DetourTransactionBegin();
		if (result == NO_ERROR) {
			// A failed thread update must abort too, or the transaction commits and skips the fallback.
			result = DetourUpdateThread(GetCurrentThread());
			if (result == NO_ERROR)
				result = DetourAttach(a_func, a_thunk);
			if (result == NO_ERROR)
				result = DetourTransactionCommit();
			else
				DetourTransactionAbort();
		} else {
			// Begin can claim the transaction before failing; release it for later hooks.
			DetourTransactionAbort();
		}
		if (result != NO_ERROR)
			L->error("Detours failed to hook {} (error {})", hookName, result);
		return result;
	}
}
