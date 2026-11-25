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

	// Records edit completion; HostClient commits the draft.
	class SettingsEdit
	{
	public:
		explicit SettingsEdit(Feature& a_feature) noexcept : _feature(a_feature) {}

		bool Apply(ui::EditResult a_result) noexcept
		{
			if (auto* live = _feature.GetLiveSettings())
				live->editCompleted |= a_result.completed;
			return a_result.changed;
		}

		// Edits the feature's draft; widgets never touch live state.
		template <const auto& kSchema>
		auto Bind(LiveSettings<kSchema>& a_live) noexcept
		{
			using Settings = typename LiveSettings<kSchema>::Settings;
			return SchemaEdit<std::remove_cvref_t<decltype(kSchema)>, Settings>{ *this, kSchema, a_live.Draft(), a_live.DefaultSettings() };
		}

	private:
		Feature& _feature;
	};

	// Field rows for one feature's schema members; see SettingsEdit::Bind.
	template <class Schema, class Settings>
	class SchemaEdit
	{
	public:
		SchemaEdit(SettingsEdit& a_edit, const Schema& a_schema, Settings& a_settings, Settings a_defaults) noexcept :
			_edit(a_edit), _schema(a_schema), _settings(a_settings), _defaults(std::move(a_defaults)) {}

		// The draft, for controls drawn outside the schema rows.
		Settings& Values() const noexcept { return _settings; }

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
			const auto result = ui::EditSetting(_schema.Find(a_member), _settings, _defaults, a_options);
			_edit.Apply(result);
			return result;
		}

		template <class T>
		bool Choice(T Settings::* a_member, std::span<const dmui::ChoiceOption<std::type_identity_t<T>>> a_choices,
			const ui::FieldOptions& a_options = {}) const
		{
			return _edit.Apply(ui::EditChoice(_schema.Find(a_member), _settings, _defaults, a_choices, a_options));
		}

		// Offers the schema field's own choices.
		template <class T>
		bool Choice(T Settings::* a_member, const ui::FieldOptions& a_options = {}) const
		{
			return _edit.Apply(ui::EditChoice(_schema.Find(a_member), _settings, _defaults, a_options));
		}

	private:
		SettingsEdit& _edit;
		const Schema& _schema;
		Settings& _settings;
		Settings _defaults;
	};
}
