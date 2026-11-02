#pragma once

#include "Log.h"
#include "Menu/SettingField.h"
#include "Settings/SettingsSchema.h"

#include <DearModdingUI/Presentation.h>
#include <DearModdingUI/UI.h>

#include <cmath>
#include <concepts>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace cs::ui
{
	// The one item-tooltip path: the host's hover delay and gamepad focus apply.
	inline void Tooltip(const char* a_text)
	{
		if (dmui::ui::IsItemHovered(dmui::ui::HoveredFlags::kForTooltip))
			dmui::ui::SetTooltip("%s", a_text);
	}

	template <class... Args>
		requires(sizeof...(Args) > 0)
	void Tooltip(const char* a_format, Args... a_args)
	{
		if (dmui::ui::IsItemHovered(dmui::ui::HoveredFlags::kForTooltip))
			dmui::ui::SetTooltip(a_format, a_args...);
	}

	// Builds the text only while the tooltip shows.
	template <std::invocable Make>
	void Tooltip(Make&& a_make)
	{
		if (dmui::ui::IsItemHovered(dmui::ui::HoveredFlags::kForTooltip))
			dmui::ui::SetTooltip("%s", std::string(a_make()).c_str());
	}

	namespace detail
	{
		template <class T>
		inline constexpr bool kIsFloatArray = std::same_as<T, settings::Float3> || std::same_as<T, settings::Color4>;
	}

	// The control alone, for custom field bodies; the field supplies the label.
	template <class T>
	EditResult EditControl(const char* a_label, const FieldSpec& a_spec, T& a_value, const settings::Range<T>& a_edit)
	{
		EditResult result;
		if constexpr (std::same_as<T, settings::StringList>) {
			// One removable row per entry, then a text box that appends on Add.
			static std::unordered_map<std::string, std::string> pending;
			const std::string id(a_spec.id);
			for (std::size_t i = 0; i < a_value.size(); ++i) {
				const std::string remove = "X##" + id + std::to_string(i);
				dmui::ui::Text("%s", a_value[i].c_str());
				dmui::ui::SameLine();
				if (dmui::ui::Button(remove.c_str())) {
					a_value.erase(a_value.begin() + static_cast<std::ptrdiff_t>(i));
					result.changed = result.completed = true;
					break;
				}
			}
			auto& entry = pending[id];
			char buffer[128]{};
			entry.copy(buffer, sizeof(buffer) - 1);
			if (dmui::ui::InputText(("##add" + id).c_str(), buffer, sizeof(buffer)))
				entry = buffer;
			dmui::ui::SameLine();
			if (dmui::ui::Button(("Add##add" + id).c_str()) && !entry.empty()) {
				a_value.push_back(entry);
				entry.clear();
				result.changed = result.completed = true;
			}
		} else if constexpr (detail::kIsFloatArray<T>) {
			// The forwarding ABI exposes scalar inputs but no vector or color editor.
			constexpr const char* components[]{ "R", "G", "B", "A" };
			constexpr const char* axes[]{ "X", "Y", "Z" };
			for (std::size_t i = 0; i < a_value.size(); ++i) {
				const char* name = std::same_as<T, settings::Color4> ? components[i] : axes[i];
				const std::string component = std::string(name) + "##" + std::string(a_spec.id);
				float candidate = a_value[i];
				if (dmui::ui::InputScalar(component.c_str(), &candidate)) {
					if (std::isfinite(candidate)) {
						a_value[i] = candidate;
						result.changed = true;
					} else {
						log::Get("cs.menu")->warn("Rejected non-finite {} component {}", a_spec.id, name);
					}
				}
				result.completed |= dmui::ui::IsItemDeactivatedAfterEdit();
			}
		} else if constexpr (std::same_as<T, std::string>) {
			char buffer[512]{};
			strncpy_s(buffer, a_value.c_str(), _TRUNCATE);
			if (dmui::ui::InputText(a_label, buffer, sizeof(buffer))) {
				a_value = buffer;
				result.changed = true;
			}
			result.completed = dmui::ui::IsItemDeactivatedAfterEdit();
		} else if constexpr (std::same_as<T, bool>) {
			result.changed = result.completed = dmui::ui::Checkbox(a_label, &a_value);
		} else if constexpr (std::unsigned_integral<T>) {
			if (a_edit.min == 0 && a_edit.max == 1) {
				bool enabled = a_value != 0;
				result.changed = result.completed = dmui::ui::Checkbox(a_label, &enabled);
				if (result.changed)
					a_value = enabled ? 1 : 0;
			} else {
				result.changed = dmui::ui::SliderScalar(a_label, &a_value, &a_edit.min, &a_edit.max, a_spec.format ? a_spec.format : "%u", a_spec.flags);
				result.completed = dmui::ui::IsItemDeactivatedAfterEdit();
			}
		} else if constexpr (std::floating_point<T>) {
			const T scale = static_cast<T>(a_spec.scale);
			T shown = a_value * scale;
			const T minimum = a_edit.min * scale, maximum = a_edit.max * scale;
			result.changed = dmui::ui::SliderScalar(a_label, &shown, &minimum, &maximum, a_spec.format, a_spec.flags);
			if (result.changed)
				a_value = shown / scale;
			result.completed = dmui::ui::IsItemDeactivatedAfterEdit();
		} else {
			result.changed = dmui::ui::SliderScalar(a_label, &a_value, &a_edit.min, &a_edit.max, a_spec.format, a_spec.flags);
			result.completed = dmui::ui::IsItemDeactivatedAfterEdit();
		}
		return result;
	}

	// One value as a field row with a Reset to a_default; a_edit bounds sliders.
	template <class T>
	EditResult EditSetting(const FieldSpec& a_spec, T& a_value, const T& a_default, const settings::Range<T>& a_edit = {})
	{
		return DrawField(
			a_spec, a_value != a_default,
			[&](const char* a_label) { return EditControl(a_label, a_spec, a_value, a_edit); },
			[&] { a_value = a_default; });
	}

	// The dropdown control for a field body; the value picked this frame, if any.
	template <class T>
	std::optional<T> DrawPick(std::string_view a_id, const T& a_current,
		std::span<const dmui::ChoiceOption<std::type_identity_t<T>>> a_options)
	{
		const auto pick = dmui::DrawChoice<T>(a_id, a_current, a_options);
		return pick.changed ? pick.selected : std::nullopt;
	}

	// A dropdown field row; a_options may disable entries.
	template <class T>
	EditResult EditChoice(const FieldSpec& a_spec, T& a_value, const T& a_default,
		std::span<const dmui::ChoiceOption<std::type_identity_t<T>>> a_options)
	{
		return DrawField(
			a_spec, a_value != a_default,
			[&](const char*) {
				EditResult result;
				if (const auto pick = DrawPick<T>(a_spec.id, a_value, a_options)) {
					a_value = *pick;
					result.changed = result.completed = true;
				}
				return result;
			},
			[&] { a_value = a_default; });
	}

	// A dropdown field row without a Reset; the value picked this frame, if any.
	template <class T>
	std::optional<T> PickChoice(const FieldSpec& a_spec, const T& a_current,
		std::span<const dmui::ChoiceOption<std::type_identity_t<T>>> a_options)
	{
		std::optional<T> picked;
		DrawActionField(a_spec, [&](const char*) {
			picked = DrawPick<T>(a_spec.id, a_current, a_options);
			return EditResult{};
		});
		return picked;
	}

	// Call-site overrides of what a schema field supplies.
	struct FieldOptions
	{
		// The schema description when empty.
		std::string_view label;
		// The schema help when empty; a renamed field falls back to its description.
		std::string_view help;
		const char* format = nullptr;
		dmui::ui::SliderFlags flags = dmui::ui::SliderFlags::kNone;
		float scale = 1.0f;
	};

	template <class Field>
	FieldSpec SpecFor(const Field& a_field, const FieldOptions& a_options)
	{
		const auto help = !a_field.help.empty() || a_options.label.empty() ? a_field.help : a_field.description;
		return {
			.id = a_field.key,
			.label = a_options.label.empty() ? a_field.description : a_options.label,
			.help = a_options.help.empty() ? help : a_options.help,
			.restart = a_field.description,
			.format = a_options.format,
			.flags = a_options.flags,
			.scale = a_options.scale
		};
	}

	// A schema value as a field row with a Reset to its default.
	template <class Field, class Settings>
		requires requires(const Field& a_field) { a_field.member; a_field.help; }
	EditResult EditSetting(const Field& a_field, Settings& a_settings, const FieldOptions& a_options = {})
	{
		using Value = typename Field::ValueType;
		settings::Range<Value> range{};
		if constexpr (requires { a_field.edit; })
			range = a_field.edit;
		return EditSetting(SpecFor(a_field, a_options), a_settings.*a_field.member, Settings{}.*a_field.member, range);
	}

	// A schema value as a dropdown field row with a Reset to its default.
	template <class Field, class Settings>
		requires requires(const Field& a_field) { a_field.member; a_field.help; }
	EditResult EditChoice(const Field& a_field, Settings& a_settings,
		std::span<const dmui::ChoiceOption<typename Field::ValueType>> a_options, const FieldOptions& a_dropdown = {})
	{
		return EditChoice(SpecFor(a_field, a_dropdown), a_settings.*a_field.member, Settings{}.*a_field.member, a_options);
	}
}
