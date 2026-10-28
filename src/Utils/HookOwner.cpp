#include "Utils/HookOwner.h"

#include "PCH.h"

#include <cstring>
#include <format>

namespace cs::util
{
	namespace
	{
		constexpr int kMaxForeignHops = 4;
		constexpr std::size_t kMaxJumpBytes = 12;

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
	}

	std::string ModuleFileName(HMODULE a_module)
	{
		const auto path = ModulePath(a_module);
		if (!path)
			return "unknown module";
		std::string name;
		REX::UTF16_TO_UTF8(path->filename().native(), name);
		return name;
	}

	// Trampolines often sit in allocated memory outside any module.
	std::optional<std::string> ForeignOwner(std::uintptr_t a_address)
	{
		static const auto game = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
		static const auto self = reinterpret_cast<std::uintptr_t>(ModuleFromAddress(reinterpret_cast<const void*>(&ForeignOwner)));

		auto destination = JumpDestination(a_address);
		if (!destination)
			return std::nullopt;
		for (int hop = 0; hop < kMaxForeignHops; ++hop) {
			const auto module = reinterpret_cast<std::uintptr_t>(ModuleFromAddress(reinterpret_cast<const void*>(*destination)));
			if (module == game || module == self)
				return std::nullopt;
			if (module != 0)
				return ModuleFileName(reinterpret_cast<HMODULE>(module));
			const auto next = JumpDestination(*destination);
			if (!next)
				break;
			destination = next;
		}
		return std::format("an unknown owner (code at {:#x})", *destination);
	}
}
