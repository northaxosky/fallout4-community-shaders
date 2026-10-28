#pragma once

#include <DearModdingUI/Client.h>

#include <utility>

namespace cs::ui
{
	template <class Draw>
	bool DrawTable(dmui::Client& a_client, const char* a_id, Draw&& a_draw)
	{
		dmui::SettingsTableScope table{ a_client, a_id };
		if (table.Result() != DMUI_RESULT_OK)
			return false;
		if (table.Visible() && !a_draw())
			return false;
		return table.End();
	}

	template <class Draw>
	bool DrawRow(
		dmui::Client& a_client,
		const char* a_id,
		const char* a_label,
		const char* a_description,
		dmui::RowPresentation::Layout a_layout,
		Draw&& a_draw)
	{
		dmui::SettingsRowScope row{ a_client, a_id, a_label, a_description, a_layout };
		if (row.Result() != DMUI_RESULT_OK)
			return false;
		if (row.Visible() && !a_draw(row))
			return false;
		return row.End().has_value();
	}

	template <class Draw>
	bool DrawRow(
		dmui::Client& a_client,
		const char* a_id,
		const char* a_label,
		const char* a_description,
		Draw&& a_draw)
	{
		return DrawRow(a_client, a_id, a_label, a_description,
			dmui::RowPresentation::Layout::kLabelValue, std::forward<Draw>(a_draw));
	}
}
