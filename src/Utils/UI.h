#pragma once

#include <DearModdingUI/FontMetrics.h>
#include <DearModdingUI/UI.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace Util
{
	// FO4: DmUI has no UI scale query; font size over its baseline is the scale.
	inline float GetUIScale()
	{
		return dmui::ui::GetFontSize() / DearModdingUI::Theme::kBaselineFontSize;
	}

	/** @brief Row comparator for one column of a sortable table: (a, b, ascending) -> a before b. */
	template <typename T>
	using TableRowSortFunc = std::function<bool(const T&, const T&, bool)>;

	/** @brief The column a sortable table is sorted by and the direction. column -1 = leave the rows as they are. */
	struct TableSortSpec
	{
		int column = -1;
		bool ascending = true;
	};

	/**
	 * @brief Reads the current table's sort specs (between BeginTable and EndTable).
	 * Falls back to the defaults when the table has no active sort: a table that is not
	 * sortable, or a tristate one the user has un-sorted. Pass -1 to leave the rows as they are then.
	 */
	inline TableSortSpec ReadTableSortSpec(int defaultColumn = -1, bool defaultAscending = true)
	{
		TableSortSpec spec{ defaultColumn, defaultAscending };
		if (const auto sortSpecs = dmui::ui::TableGetSortSpecs(); !sortSpecs.empty()) {
			spec.column = sortSpecs.front().column;
			spec.ascending = sortSpecs.front().direction == DMUI_UI_SORT_DIRECTION_ASCENDING;
		}
		return spec;
	}

	/** @brief Stable-sorts rows with one comparator, unless the spec says to leave them as they are. */
	template <typename T>
	void SortTableRowsWith(std::vector<T>& rows, const TableSortSpec& spec, const TableRowSortFunc<T>& comparator)
	{
		if (spec.column < 0 || !comparator)
			return;
		const bool ascending = spec.ascending;
		std::stable_sort(rows.begin(), rows.end(), [&comparator, ascending](const T& a, const T& b) {
			return comparator(a, b, ascending);
		});
	}

	/** @brief Stable-sorts rows by the spec's column with that column's comparator; a column without one leaves the rows as they are. */
	template <typename T>
	void SortTableRows(std::vector<T>& rows, const TableSortSpec& spec, const std::vector<TableRowSortFunc<T>>& customSorts)
	{
		if (spec.column < 0 || static_cast<size_t>(spec.column) >= customSorts.size())
			return;
		SortTableRowsWith(rows, spec, customSorts[spec.column]);
	}
}
