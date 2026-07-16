#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Windows.h>

namespace cs::input
{
	// Parsed keyboard chord: one key plus optional Shift/Ctrl/Alt. vk == 0 means unbound.
	struct Hotkey
	{
		std::uint32_t vk    = 0;
		bool          shift = false;
		bool          ctrl  = false;
		bool          alt   = false;

		// Parses "Shift+F11", "F10", "Ctrl+F9"; "none"/"" -> unbound. a_ok (if given) reports spec
		// validity: true for a real chord or the deliberate unbind, false for a malformed spec.
		static Hotkey Parse(std::string_view a_spec, bool* a_ok = nullptr);

		bool IsBound() const noexcept { return vk != 0; }

		// Exact modifier match, ignores auto-repeat. F10 is a system key (WM_SYSKEYDOWN), hence both downs.
		bool MatchesDown(UINT a_msg, WPARAM a_wparam, LPARAM a_lparam) const noexcept;

		// Pairs the key-up with a consumed press; matches vk only since modifiers may already be released.
		bool MatchesUp(UINT a_msg, WPARAM a_wparam) const noexcept;

		// Canonical form ("Shift+F11", "None") for logging and round-trip.
		std::string ToString() const;
	};
}
