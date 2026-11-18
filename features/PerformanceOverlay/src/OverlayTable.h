#pragma once

#include "Utils/UI.h"

#include <DearModdingUI/Client.h>
#include <DearModdingUI/UI.h>

#include <string>
#include <vector>

namespace cs::features::performance_overlay
{
	std::string Milliseconds(float a_value);
	dmui::TextTone ComparisonTone(float a_left, float a_right);
	void DrawMilliseconds(float a_value, dmui::TextTone a_tone = dmui::TextTone::kInherit, bool a_microseconds = false);
	void DrawDelta(float a_baseline, float a_test);

	// Name columns sort A to Z first and metric columns slowest first.
	template <std::size_t N>
	Util::TableSortSpec SortHeaders(const char* const (&a_labels)[N], int a_defaultColumn)
	{
		using dmui::ui::TableColumnFlags;
		for (std::size_t i = 0; i < N; ++i) {
			auto flags = i ? TableColumnFlags::kPreferSortDescending : TableColumnFlags::kNone;
			if (static_cast<int>(i) == a_defaultColumn)
				flags |= TableColumnFlags::kDefaultSort;
			dmui::ui::TableSetupColumn(a_labels[i], flags);
		}
		dmui::ui::TableHeadersRow();
		return Util::ReadTableSortSpec(a_defaultColumn, false);
	}

	template <class Row, class Metric>
	void SortRows(std::vector<Row>& a_rows, const Util::TableSortSpec& a_sort, Metric a_metric)
	{
		const int column = a_sort.column;
		Util::SortTableRowsWith<Row>(a_rows, a_sort, [&](const Row& a, const Row& b, bool a_ascending) {
			if constexpr (requires { a.shaderType; }) {
				if (a.shaderType < 0 || b.shaderType < 0) {
					if (a.shaderType < 0 && b.shaderType < 0)
						return a.shaderType < b.shaderType;
					return a.shaderType >= 0;
				}
			}
			const auto left = a_metric(a), right = a_metric(b);
			const int order = column == 0 ? a.label.compare(b.label) : (left > right) - (left < right);
			return a_ascending ? order < 0 : order > 0;
		});
	}
}
