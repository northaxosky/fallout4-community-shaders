#pragma once

#include "Log.h"
#include "Settings/SettingsSchema.h"

#include <DearModdingUI/UI.h>

#include <cmath>
#include <concepts>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cs::ui
{
	inline void Tooltip(const char* a_text)
	{
		if (dmui::ui::IsItemHovered())
			dmui::ui::SetTooltip("%s", a_text);
	}

	struct EditResult
	{
		bool changed = false;
		// True once the edit is finished: a toggle, or a released slider or input.
		bool completed = false;
	};

	// A schema field's widget label: its description, with the key as the id.
	template <class Field>
	std::string FieldLabel(const Field& a_field)
	{
		return std::string(a_field.description) + "##" + std::string(a_field.key);
	}

	// One schema value's widget; a_edit bounds sliders, not vectors.
	template <class T>
	EditResult EditSetting(const char* a_label, std::string_view a_id, T& a_value, const settings::Range<T>& a_edit,
		const char* a_format = nullptr, dmui::ui::SliderFlags a_flags = dmui::ui::SliderFlags::kNone)
	{
		EditResult result;
		if constexpr (std::same_as<T, settings::StringList>) {
			// One removable row per entry, then a text box that appends on Add.
			static std::unordered_map<std::string, std::string> pending;
			dmui::ui::Text("%s", a_label);
			for (std::size_t i = 0; i < a_value.size(); ++i) {
				const std::string remove = "X##" + std::string(a_id) + std::to_string(i);
				dmui::ui::Text("%s", a_value[i].c_str());
				dmui::ui::SameLine();
				if (dmui::ui::Button(remove.c_str())) {
					a_value.erase(a_value.begin() + static_cast<std::ptrdiff_t>(i));
					result.changed = result.completed = true;
					break;
				}
			}
			auto& entry = pending[std::string(a_id)];
			char buffer[128]{};
			entry.copy(buffer, sizeof(buffer) - 1);
			if (dmui::ui::InputText(("##add" + std::string(a_id)).c_str(), buffer, sizeof(buffer)))
				entry = buffer;
			dmui::ui::SameLine();
			if (dmui::ui::Button(("Add##add" + std::string(a_id)).c_str()) && !entry.empty()) {
				a_value.push_back(entry);
				entry.clear();
				result.changed = result.completed = true;
			}
		} else if constexpr (std::same_as<T, settings::Float3> || std::same_as<T, settings::Color4>) {
			// The forwarding ABI exposes scalar inputs but no vector or color editor.
			constexpr const char* components[]{ "R", "G", "B", "A" };
			constexpr const char* axes[]{ "X", "Y", "Z" };
			dmui::ui::Text("%s", a_label);
			for (std::size_t i = 0; i < a_value.size(); ++i) {
				const char* name = std::same_as<T, settings::Float3> ? axes[i] : components[i];
				const std::string component = std::string(name) + "##" + std::string(a_id);
				float candidate = a_value[i];
				if (dmui::ui::InputScalar(component.c_str(), &candidate)) {
					if (std::isfinite(candidate)) {
						a_value[i] = candidate;
						result.changed = true;
					} else {
						log::Get("cs.menu")->warn("Rejected non-finite {} component {}", a_id, name);
					}
				}
				result.completed |= dmui::ui::IsItemDeactivatedAfterEdit();
			}
		} else if constexpr (std::same_as<T, bool>) {
			result.changed = result.completed = dmui::ui::Checkbox(a_label, &a_value);
		} else if constexpr (std::unsigned_integral<T>) {
			if (a_edit.min == 0 && a_edit.max == 1) {
				bool enabled = a_value != 0;
				result.changed = result.completed = dmui::ui::Checkbox(a_label, &enabled);
				if (result.changed)
					a_value = enabled ? 1 : 0;
			} else {
				result.changed = dmui::ui::SliderScalar(a_label, &a_value, &a_edit.min, &a_edit.max, a_format ? a_format : "%u", a_flags);
				result.completed = dmui::ui::IsItemDeactivatedAfterEdit();
			}
		} else {
			result.changed = dmui::ui::SliderScalar(a_label, &a_value, &a_edit.min, &a_edit.max, a_format, a_flags);
			result.completed = dmui::ui::IsItemDeactivatedAfterEdit();
		}
		return result;
	}
}
