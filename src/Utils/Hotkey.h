#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Windows.h>

namespace cs::input
{
	// vk=0 means unbound.
	struct Hotkey
	{
		std::uint32_t vk    = 0;
		bool          shift = false;
		bool          ctrl  = false;
		bool          alt   = false;

		// a_ok distinguishes unbound from malformed input.
		static Hotkey Parse(std::string_view a_spec, bool* a_ok = nullptr);

		bool IsBound() const noexcept { return vk != 0; }

		// Match exact modifiers and both key-down message types.
		bool MatchesDown(UINT a_msg, WPARAM a_wparam, LPARAM a_lparam) const noexcept;

		// Match key-up by key because modifiers may release first.
		bool MatchesUp(UINT a_msg, WPARAM a_wparam) const noexcept;

		// Canonical form supports logging and round trips.
		std::string ToString() const;
	};
}
