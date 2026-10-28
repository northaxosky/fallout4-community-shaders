#include "WeatherOverrideEditor.h"

#include "Feature.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
#include "Menu/SettingWidgets.h"
#include "World/Weather.h"
#include "World/WeatherProfiles.h"

#include <DearModdingUI/UI.h>
#include <DearModdingUI/UI/Colors.h>

#include <format>
#include <variant>

namespace cs::features::cs_editor
{
	namespace
	{
		template <class T>
		settings::Range<T> EditRange(const settings::FieldView& a_field)
		{
			const auto& min = a_field.editMinimum ? a_field.editMinimum : a_field.minimum;
			const auto& max = a_field.editMaximum ? a_field.editMaximum : a_field.maximum;
			settings::Range<T> range;
			if (min && std::holds_alternative<T>(*min))
				range.min = std::get<T>(*min);
			if (max && std::holds_alternative<T>(*max))
				range.max = std::get<T>(*max);
			return range;
		}

		// The variable's widget; returns the edit and the possibly changed value.
		ui::EditResult EditValue(const settings::FieldView& a_field, settings::Value& a_value)
		{
			const auto& label = a_field.description.empty() ? a_field.key : a_field.description;
			return std::visit([&](auto& a_v) -> ui::EditResult {
				using T = std::remove_cvref_t<decltype(a_v)>;
				if constexpr (std::same_as<T, std::string> || std::same_as<T, settings::Float2> || std::same_as<T, settings::StringList>) {
					dmui::ui::TextDisabled("%s: not editable here", label.c_str());
					return {};
				} else if constexpr (std::same_as<T, settings::Float3> || std::same_as<T, settings::Color4>) {
					return ui::EditSetting(label.c_str(), a_field.key, a_v, settings::Range<T>{});
				} else {
					return ui::EditSetting(label.c_str(), a_field.key, a_v, EditRange<T>(a_field));
				}
			},
				a_value);
		}

		settings::Value BaseValue(const settings::FieldView& a_field, const toml::table& a_live)
		{
			if (const auto* node = a_live.get(a_field.key)) {
				if (auto value = a_field.read(*node))
					return *value;
			}
			return a_field.defaultValue;
		}
	}

	void WeatherOverrideEditor::Draw(const RE::TESWeather* a_weather)
	{
		const ui::Section section{ "cs-editor-overrides", "Weather Overrides" };
		if (!section)
			return;
		const auto key = engine::WeatherKey(a_weather);
		if (key.empty()) {
			dmui::ui::TextDisabled("No weather selected, or the weather has no source plugin.");
			return;
		}
		dmui::ui::Text("Editing: %s", engine::DescribeWeather(a_weather).c_str());
		dmui::ui::TextDisabled("Overrides apply live and blend with the weather transition.");
		if (!_error.empty()) {
			dmui::ui::TextColored(dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusError), "%s", _error.c_str());
		}

		for (auto* feature : FeatureManager::Get().GetAll()) {
			auto* profiles = feature->GetWeatherProfiles();
			if (!profiles)
				continue;
			const auto variables = profiles->Variables();
			auto profile = profiles->Get(key).value_or(toml::table{ { weather::kEnabledKey, false } });
			std::size_t overridden = 0;
			for (const auto& variable : variables)
				overridden += profile.contains(variable.key);

			const auto id = std::format("cs-editor-overrides-{}", feature->GetName());
			const ui::Section featureSection{ id.c_str(), std::string(feature->GetDisplayName()).c_str(),
				ui::Collapsible{ .count = overridden, .framed = false } };
			if (!featureSection)
				continue;
			dmui::ui::PushID(id.c_str());

			bool changed = false, completed = false;
			bool enabled = profile[weather::kEnabledKey].value_or(false);
			if (dmui::ui::Checkbox("Enable overrides for this weather", &enabled)) {
				profile.insert_or_assign(weather::kEnabledKey, enabled);
				changed = completed = true;
			}
			dmui::ui::SameLine();
			const bool exists = profiles->Get(key).has_value();
			dmui::ui::BeginDisabled(!exists);
			const bool clear = dmui::ui::Button("Clear");
			dmui::ui::EndDisabled();

			const auto& live = feature->GetLiveSettingsAccess();
			const auto base = live.snapshot ? live.snapshot() : toml::table{};
			for (const auto& variable : variables) {
				dmui::ui::PushID(variable.key.c_str());
				bool active = profile.contains(variable.key);
				auto value = BaseValue(variable, base);
				if (active) {
					if (auto parsed = variable.read(*profile.get(variable.key)))
						value = *parsed;
				}
				if (dmui::ui::Checkbox("##override", &active)) {
					changed = completed = true;
					if (!active)
						profile.erase(variable.key);
				}
				if (dmui::ui::IsItemHovered())
					dmui::ui::SetTooltip("Override %s for this weather", variable.key.c_str());
				dmui::ui::SameLine();
				dmui::ui::BeginDisabled(!active);
				const auto edit = EditValue(variable, value);
				dmui::ui::EndDisabled();
				if (active && (edit.changed || !profile.contains(variable.key))) {
					settings::WriteValue(profile, variable.key, value);
					changed = true;
				}
				completed |= edit.completed;
				dmui::ui::PopID();
			}
			dmui::ui::PopID();

			const auto name = feature->GetName();
			if (changed || clear) {
				std::string error;
				if (!profiles->Set(key, clear ? nullptr : &profile, error)) {
					_error = std::format("{}: {}", feature->GetDisplayName(), error);
					continue;
				}
				_error.clear();
				_unsaved.emplace(name);
			}
			// A released slider completes on a later frame than its last change.
			if (!(completed || clear) || !_unsaved.contains(name))
				continue;
			if (const auto saved = weather::Save(feature->GetConfigKey(), *profiles); saved)
				_unsaved.erase(_unsaved.find(name));
			else
				Menu::ShowToast(saved.error, 6.0, DMUI_STATUS_SEVERITY_ERROR, std::string(feature->GetDisplayName()));
		}
	}
}
