#pragma once

#include "Feature.h"
#include "Menu/SettingWidgets.h"

#include <DearModdingUI/UI.h>

#include <span>
#include <string>

namespace cs::settings
{
	template <class Schema, class Settings>
	class SchemaEdit;

	// Records edits on the feature; HostClient persists them on completion or page exit.
	class SettingsEdit
	{
	public:
		explicit SettingsEdit(Feature& a_feature) noexcept : _feature(a_feature) {}

		bool Apply(ui::EditResult a_result) noexcept
		{
			_feature._settingsSavePending |= a_result.changed;
			_feature._settingsEditCompleted |= a_result.completed;
			return a_result.changed;
		}

		// Edits through a schema's members, each as a field row with a Reset.
		template <class Schema, class Settings>
		SchemaEdit<Schema, Settings> Bind(const Schema& a_schema, Settings& a_settings) noexcept
		{
			return { *this, a_schema, a_settings };
		}

	private:
		Feature& _feature;
	};

	// Field rows for one feature's schema members; see SettingsEdit::Bind.
	template <class Schema, class Settings>
	class SchemaEdit
	{
	public:
		SchemaEdit(SettingsEdit& a_edit, const Schema& a_schema, Settings& a_settings) noexcept :
			_edit(a_edit), _schema(a_schema), _settings(a_settings) {}

		// Draws the field and returns whether it changed.
		template <class T>
		bool operator()(T Settings::* a_member, const ui::FieldOptions& a_options = {}) const
		{
			return Edit(a_member, a_options).changed;
		}

		// As operator(), keeping whether the edit also finished.
		template <class T>
		ui::EditResult Edit(T Settings::* a_member, const ui::FieldOptions& a_options = {}) const
		{
			const auto result = ui::EditSetting(_schema.Find(a_member), _settings, a_options);
			_edit.Apply(result);
			return result;
		}

		template <class T>
		bool Choice(T Settings::* a_member, std::span<const dmui::ChoiceOption<std::type_identity_t<T>>> a_choices,
			const ui::FieldOptions& a_options = {}) const
		{
			return _edit.Apply(ui::EditChoice(_schema.Find(a_member), _settings, a_choices, a_options));
		}

	private:
		SettingsEdit& _edit;
		const Schema& _schema;
		Settings& _settings;
	};
}
