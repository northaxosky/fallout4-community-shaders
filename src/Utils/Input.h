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

		bool IsValid() const
		{
			return IsValidDevice(GetDevice()) && GetKey() != 0;
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
