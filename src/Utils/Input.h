#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Windows.h>

#include <toml++/toml.hpp>

namespace cs
{
	enum class InputDeviceType
	{
		Primary = 0,
		Secondary = 1,
		Both = 2,
		Keyboard = 3,
		Mouse = 4,
		Gamepad = 5
	};

	constexpr const char* ToString(InputDeviceType a_device)
	{
		switch (a_device) {
		case InputDeviceType::Primary:
			return "Primary";
		case InputDeviceType::Secondary:
			return "Secondary";
		case InputDeviceType::Both:
			return "Both";
		case InputDeviceType::Keyboard:
			return "Keyboard";
		case InputDeviceType::Mouse:
			return "Mouse";
		case InputDeviceType::Gamepad:
			return "Gamepad";
		default:
			return "Unknown";
		}
	}

	constexpr bool IsValidDevice(InputDeviceType a_device)
	{
		return a_device >= InputDeviceType::Primary && a_device <= InputDeviceType::Gamepad;
	}

	// Packed keys keep bare integers valid for keyboard bindings.
	struct InputCombo
	{
	public:
		InputCombo() :
			deviceAndKey(0) {}

		InputCombo(InputDeviceType a_device, std::uint32_t a_key) :
			deviceAndKey((static_cast<std::uint32_t>(a_device) << 16) | (a_key & 0xFFFF))
		{
		}

		static InputCombo Keyboard(std::uint32_t a_key) { return InputCombo(InputDeviceType::Keyboard, a_key); }
		static InputCombo Mouse(std::uint32_t a_key) { return InputCombo(InputDeviceType::Mouse, a_key); }
		static InputCombo Gamepad(std::uint32_t a_key) { return InputCombo(InputDeviceType::Gamepad, a_key); }

		InputDeviceType GetDevice() const { return static_cast<InputDeviceType>(deviceAndKey >> 16); }
		std::uint32_t GetKey() const { return deviceAndKey & 0xFFFF; }
		std::uint32_t GetPacked() const { return deviceAndKey; }

		bool IsValid() const
		{
			return IsValidDevice(GetDevice()) && GetKey() != 0;
		}

		static bool MatchesKeyboardCombo(const std::vector<InputCombo>& a_combo, std::uint32_t a_vkKey)
		{
			if (a_combo.empty() || a_combo.back().GetKey() != a_vkKey || a_combo.back().GetDevice() != InputDeviceType::Keyboard)
				return false;

			bool requiresCtrl = false, requiresShift = false, requiresAlt = false;
			for (std::size_t i = 0; i < a_combo.size() - 1; ++i) {
				if (a_combo[i].GetDevice() != InputDeviceType::Keyboard)
					return false;
				const std::uint32_t modKey = a_combo[i].GetKey();
				if (modKey == VK_CONTROL || modKey == VK_LCONTROL || modKey == VK_RCONTROL)
					requiresCtrl = true;
				else if (modKey == VK_SHIFT || modKey == VK_LSHIFT || modKey == VK_RSHIFT)
					requiresShift = true;
				else if (modKey == VK_MENU || modKey == VK_LMENU || modKey == VK_RMENU)
					requiresAlt = true;
				else
					return false;
			}

			constexpr std::uint16_t kKeyPressed = 0x8000;
			const bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & kKeyPressed) != 0;
			const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & kKeyPressed) != 0;
			const bool altHeld = (GetAsyncKeyState(VK_MENU) & kKeyPressed) != 0;

			return (requiresCtrl == ctrlHeld) && (requiresShift == shiftHeld) && (requiresAlt == altHeld);
		}

		bool operator==(const InputCombo& a_other) const { return deviceAndKey == a_other.deviceAndKey; }
		bool operator<(const InputCombo& a_other) const { return deviceAndKey < a_other.deviceAndKey; }

		// Only single keyboard bindings use a bare integer.
		struct ComboList
		{
			static void Append(toml::table& a_table, std::string_view a_key, const std::vector<InputCombo>& a_combos)
			{
				if (a_combos.empty()) {
					a_table.insert_or_assign(a_key, 0);
					return;
				}

				if (a_combos.size() == 1) {
					if (a_combos[0].GetDevice() == InputDeviceType::Keyboard)
						a_table.insert_or_assign(a_key, static_cast<std::int64_t>(a_combos[0].GetKey()));
					else
						a_table.insert_or_assign(a_key, static_cast<std::int64_t>(a_combos[0].deviceAndKey));
					return;
				}

				toml::array packed;
				for (const auto& combo : a_combos) {
					if (combo.GetDevice() == InputDeviceType::Keyboard)
						packed.push_back(static_cast<std::int64_t>(combo.GetKey()));
					else
						packed.push_back(static_cast<std::int64_t>(combo.deviceAndKey));
				}
				a_table.insert_or_assign(a_key, std::move(packed));
			}

			static bool Read(const toml::table& a_table, std::string_view a_key, std::vector<InputCombo>& a_combos)
			{
				const auto* node = a_table.get(a_key);
				if (!node)
					return false;

				std::vector<InputCombo> parsed;
				if (const auto* array = node->as_array()) {
					for (const auto& element : *array) {
						if (const auto value = element.value<std::int64_t>())
							ParseAndAdd(static_cast<std::uint32_t>(*value), parsed);
					}
				} else if (const auto value = node->value<std::int64_t>()) {
					ParseAndAdd(static_cast<std::uint32_t>(*value), parsed);
				} else {
					return false;
				}

				a_combos = std::move(parsed);
				return true;
			}

		private:
			static void ParseAndAdd(std::uint32_t a_value, std::vector<InputCombo>& a_combos)
			{
				if (a_value == 0)
					return;
				if (a_value < 0x10000) {
					a_combos.push_back(InputCombo::Keyboard(a_value));
				} else {
					InputCombo combo;
					combo.deviceAndKey = a_value;
					a_combos.push_back(combo);
				}
			}
		};

	private:
		std::uint32_t deviceAndKey;
	};
}
