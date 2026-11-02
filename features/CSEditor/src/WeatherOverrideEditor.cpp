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

		// The variable's control; returns the edit and the possibly changed value.
		ui::EditResult EditValue(const settings::FieldView& a_field, settings::Value& a_value)
		{
			const ui::FieldSpec spec{ .id = a_field.key };
			const std::string hidden = "##" + a_field.key;
			return std::visit([&](auto& a_v) -> ui::EditResult {
				using T = std::remove_cvref_t<decltype(a_v)>;
				if constexpr (std::same_as<T, std::string> || std::same_as<T, settings::Float2> || std::same_as<T, settings::StringList>) {
					dmui::ui::TextDisabled("Not editable here");
					return {};
				} else if constexpr (std::same_as<T, settings::Float3> || std::same_as<T, settings::Color4>) {
					return ui::EditControl(hidden.c_str(), spec, a_v, settings::Range<T>{});
				} else {
					return ui::EditControl(hidden.c_str(), spec, a_v, EditRange<T>(a_field));
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
			bool changed = false, completed = false;
			bool enabled = profile[weather::kEnabledKey].value_or(false);
			if (ui::EditSetting({ .id = id + "-enabled", .label = "Enable overrides for this weather" }, enabled, false).changed) {
				profile.insert_or_assign(weather::kEnabledKey, enabled);
				changed = completed = true;
			}
			bool clear = false;
			{
				const bool exists = profiles->Get(key).has_value();
				const dmui::DisabledScope disabled{ !exists };
				clear = ui::DrawActionField(
					{ .id = id + "-clear", .label = "Clear overrides", .help = "Removes every override for this weather." },
					[](const char* a_label) {
						const bool pressed = dmui::ui::Button(std::format("Clear{}", a_label).c_str());
						return ui::EditResult{ pressed, pressed };
					})
				            .changed;
			}

			const auto& live = feature->GetLiveSettingsAccess();
			const auto base = live.snapshot ? live.snapshot() : toml::table{};
			for (const auto& variable : variables) {
				bool active = profile.contains(variable.key);
				auto value = BaseValue(variable, base);
				if (active) {
					if (auto parsed = variable.read(*profile.get(variable.key)))
						value = *parsed;
				}
				bool toggled = false;
				// Reset drops the override.
				const auto edit = ui::DrawField(
					{ .id = id + "-" + variable.key, .label = variable.description.empty() ? variable.key : variable.description, .help = variable.help },
					active,
					[&](const char*) {
						toggled = dmui::ui::Checkbox(("##override-" + variable.key).c_str(), &active);
						ui::Tooltip("Override %s for this weather", variable.key.c_str());
						dmui::ui::SameLine();
						const dmui::DisabledScope disabled{ !active };
						return EditValue(variable, value);
					},
					[&] { active = false; });
				if (!active && profile.contains(variable.key)) {
					profile.erase(variable.key);
					changed = completed = true;
				}
				if (toggled)
					changed = completed = true;
				if (active && (edit.changed || !profile.contains(variable.key))) {
					settings::WriteValue(profile, variable.key, value);
					changed = true;
				}
				completed |= edit.completed;
			}

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
