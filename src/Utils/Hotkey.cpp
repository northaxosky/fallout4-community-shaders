#include "Utils/Hotkey.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace cs::input
{
	namespace
	{
		constexpr std::string_view kWhitespace = " \t\r\n";

		std::string_view Trim(std::string_view a_text)
		{
			const auto begin = a_text.find_first_not_of(kWhitespace);
			if (begin == std::string_view::npos)
				return {};
			const auto end = a_text.find_last_not_of(kWhitespace);
			return a_text.substr(begin, end - begin + 1);
		}

		std::string ToLower(std::string_view a_text)
		{
			std::string out(a_text);
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char a_c) noexcept { return static_cast<char>(std::tolower(a_c)); });
			return out;
		}

		// Maps supported key names to Win32 virtual keys.
		std::uint32_t KeyTokenToVk(std::string_view a_token)
		{
			if (a_token.empty())
				return 0;
			if (a_token.front() == 'f' && a_token.size() >= 2 && a_token.size() <= 3) {
				int num = 0;
				for (std::size_t i = 1; i < a_token.size(); ++i) {
					if (a_token[i] < '0' || a_token[i] > '9')
						return 0;
					num = num * 10 + (a_token[i] - '0');
				}
				if (num >= 1 && num <= 12)
					return static_cast<std::uint32_t>(VK_F1 + (num - 1));
				return 0;
			}
			if (a_token.size() == 1) {
				const char c = a_token.front();
				if (c >= 'a' && c <= 'z')
					return static_cast<std::uint32_t>('A' + (c - 'a'));
				if (c >= '0' && c <= '9')
					return static_cast<std::uint32_t>(c);
			}
			return 0;
		}

		bool ModifierDown(int a_vk) noexcept
		{
			return (GetKeyState(a_vk) & 0x8000) != 0;
		}
	}

	Hotkey Hotkey::Parse(std::string_view a_spec, bool* a_ok)
	{
		const auto report = [a_ok](bool a_valid) noexcept {
			if (a_ok)
				*a_ok = a_valid;
		};

		const std::string      lowered = ToLower(a_spec);
		const std::string_view spec = Trim(lowered);
		// Empty and "none" deliberately unbind.
		if (spec.empty() || spec == "none") {
			report(true);
			return {};
		}

		Hotkey      result;
		bool        haveKey = false;
		std::size_t start = 0;
		while (true) {
			const auto plus = spec.find('+', start);
			const auto raw = spec.substr(start,
				plus == std::string_view::npos ? std::string_view::npos : plus - start);
			const auto token = Trim(raw);
			if (token.empty()) {
				report(false);
				return {};
			}

			if (token == "shift") {
				result.shift = true;
			} else if (token == "ctrl") {
				result.ctrl = true;
			} else if (token == "alt") {
				result.alt = true;
			} else if (!haveKey) {
				const auto vk = KeyTokenToVk(token);
				if (vk == 0) {
					report(false);
					return {};
				}
				result.vk = vk;
				haveKey = true;
			} else {
				report(false);  // Reject multiple key tokens.
				return {};
			}

			if (plus == std::string_view::npos)
				break;
			start = plus + 1;
		}

		if (!haveKey) {  // Reject chords without a key.
			report(false);
			return {};
		}
		report(true);
		return result;
	}

	bool Hotkey::MatchesDown(UINT a_msg, WPARAM a_wparam, LPARAM a_lparam) const noexcept
	{
		if (a_msg != WM_KEYDOWN && a_msg != WM_SYSKEYDOWN)
			return false;
		if (vk == 0 || a_wparam != vk)
			return false;
		if ((HIWORD(a_lparam) & KF_REPEAT) != 0)
			return false;
		return ModifierDown(VK_SHIFT) == shift
			&& ModifierDown(VK_CONTROL) == ctrl
			&& ModifierDown(VK_MENU) == alt;
	}

	bool Hotkey::MatchesUp(UINT a_msg, WPARAM a_wparam) const noexcept
	{
		if (a_msg != WM_KEYUP && a_msg != WM_SYSKEYUP)
			return false;
		return vk != 0 && a_wparam == vk;
	}

	std::string Hotkey::ToString() const
	{
		if (vk == 0)
			return "None";

		std::string out;
		if (shift)
			out += "Shift+";
		if (ctrl)
			out += "Ctrl+";
		if (alt)
			out += "Alt+";

		if (vk >= VK_F1 && vk <= VK_F12) {
			out += 'F';
			out += std::to_string(static_cast<int>(vk) - VK_F1 + 1);
		} else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
			out += static_cast<char>(vk);
		} else {
			out += "VK" + std::to_string(vk);
		}
		return out;
	}
}
