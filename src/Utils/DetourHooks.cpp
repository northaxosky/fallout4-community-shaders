#include "Utils/DetourHooks.h"

#include "Log.h"
#include "Utils/ModuleInfo.h"

#include <detours.h>

#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace
{
	auto* L = cs::log::Get("cs.hooks");

	constexpr int kMaxForeignHops = 4;
	constexpr std::size_t kMaxJumpBytes = 12;

	std::string_view ShortTypeName(std::string_view a_typeName)
	{
		const auto split = a_typeName.find_last_of(": ");
		return split == std::string_view::npos ? a_typeName : a_typeName.substr(split + 1);
	}

	bool ReadForeign(std::uintptr_t a_address, void* a_out, std::size_t a_size) noexcept
	{
		constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
		                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		for (auto cursor = a_address; cursor < a_address + a_size;) {
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) != sizeof(info) ||
				info.State != MEM_COMMIT || (info.Protect & readable) == 0 || (info.Protect & PAGE_GUARD) != 0)
				return false;
			cursor = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
		}
		std::memcpy(a_out, reinterpret_cast<const void*>(a_address), a_size);
		return true;
	}

	std::optional<std::uintptr_t> JumpDestination(std::uintptr_t a_address) noexcept
	{
		std::uint8_t code[kMaxJumpBytes]{};
		// A tiny function at a region's end may have under 12 readable bytes.
		std::size_t size = kMaxJumpBytes;
		while (size >= 2 && !ReadForeign(a_address, code, size))
			--size;
		if (size < 2)
			return std::nullopt;

		if (code[0] == 0xE9 && size >= 5) {
			std::int32_t rel;
			std::memcpy(&rel, code + 1, sizeof(rel));
			return a_address + 5 + rel;
		}
		if (code[0] == 0xEB)
			return a_address + 2 + static_cast<std::int8_t>(code[1]);
		if (code[0] == 0xFF && code[1] == 0x25 && size >= 6) {
			std::int32_t disp;
			std::memcpy(&disp, code + 2, sizeof(disp));
			std::uintptr_t destination;
			if (ReadForeign(a_address + 6 + disp, &destination, sizeof(destination)))
				return destination;
			return std::nullopt;
		}
		if (code[0] == 0x48 && code[1] == 0xB8 && size >= 12 && code[10] == 0xFF && code[11] == 0xE0) {
			std::uintptr_t destination;
			std::memcpy(&destination, code + 2, sizeof(destination));
			return destination;
		}
		return std::nullopt;
	}

	std::string ModuleFileName(HMODULE a_module)
	{
		const auto path = cs::util::ModulePath(a_module);
		if (!path)
			return "unknown module";
		std::string name;
		REX::UTF16_TO_UTF8(path->filename().native(), name);
		return name;
	}

	std::string DescribeAddress(std::uintptr_t a_address)
	{
		if (const auto module = cs::util::ModuleFromAddress(reinterpret_cast<const void*>(a_address)))
			return std::format("{}+{:X}", ModuleFileName(module), a_address - reinterpret_cast<std::uintptr_t>(module));
		return std::format("{:#x}", a_address);
	}

	// Trampolines often sit in allocated memory outside any module.
	void WarnIfForeignHook(std::uintptr_t a_target, std::string_view a_hookName)
	{
		static const auto game = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
		static const auto self = reinterpret_cast<std::uintptr_t>(cs::util::ModuleFromAddress(reinterpret_cast<const void*>(&ModuleFileName)));

		auto destination = JumpDestination(a_target);
		if (!destination)
			return;
		for (int hop = 0; hop < kMaxForeignHops; ++hop) {
			const auto module = reinterpret_cast<std::uintptr_t>(cs::util::ModuleFromAddress(reinterpret_cast<const void*>(*destination)));
			if (module == game || module == self)
				return;
			if (module != 0) {
				L->warn(
					"{} target {} is already hooked by {}. Chaining anyway; please tell me you know what you are doing.",
					a_hookName, DescribeAddress(a_target), ModuleFileName(reinterpret_cast<HMODULE>(module)));
				return;
			}
			const auto next = JumpDestination(*destination);
			if (!next)
				break;
			destination = next;
		}
		L->warn(
			"{} target {} is already hooked by an unknown owner (code at {:#x}). Chaining anyway; please tell me you know what you are doing.",
			a_hookName, DescribeAddress(a_target), *destination);
	}
}

namespace cs::hooks
{
	LONG Attach(void** a_func, void* a_thunk, const char* a_typeName)
	{
		const auto hookName = ShortTypeName(a_typeName);
		WarnIfForeignHook(reinterpret_cast<std::uintptr_t>(*a_func), hookName);

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
		}
		if (result != NO_ERROR)
			L->error("Detours failed to hook {} (error {})", hookName, result);
		return result;
	}
}
