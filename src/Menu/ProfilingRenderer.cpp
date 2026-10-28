#include "ProfilingRenderer.h"

#include "Render/FrameProfiler.h"
#include "Shared/PerfUtils.h"

#include <DearModdingUI/UI/Colors.h>
#include <DearModdingUI/UI/DrawList.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace cs
{
	namespace
	{
		using dmui::ui::Color32;
		using dmui::ui::TableColumnFlags;
		using dmui::ui::Vec2;
		using dmui::ui::Vec4;

		// FO4: DmUI packs colors as RRGGBBAA rather than IM_COL32's ABGR.
		Color32 HslToColor32(float h, float s, float l)
		{
			auto hue2rgb = [](float p, float q, float t) -> float {
				if (t < 0.0f)
					t += 1.0f;
				if (t > 1.0f)
					t -= 1.0f;
				if (t < 1.0f / 6.0f)
					return p + (q - p) * 6.0f * t;
				if (t < 0.5f)
					return q;
				if (t < 2.0f / 3.0f)
					return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
				return p;
			};

			float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
			float p = 2.0f * l - q;
			float r = hue2rgb(p, q, h + 1.0f / 3.0f);
			float g = hue2rgb(p, q, h);
			float b = hue2rgb(p, q, h - 1.0f / 3.0f);

			return dmui::ui::ColorConvertFloat4ToU32(Vec4{ r, g, b, 1.0f });
		}

		constexpr float kGoldenRatio = 0.618033988749895f;
		constexpr float kGraphHeadroomScale = 1.2f;
		constexpr float kMainGraphLegendWidth = 260.0f;
		constexpr float kFeatureGraphLegendWidth = 200.0f;
		constexpr float kMinGraphWidth = 100.0f;
		constexpr float kMainGraphHeight = 180.0f;
		constexpr float kFeatureGraphHeight = 100.0f;
		constexpr float kMainGraphMinFrameTimeSec = 0.0001f;
		constexpr float kFeatureGraphMinFrameTimeSec = 0.00001f;
		constexpr float kTimingTableMetricColumnWidth = 55.0f;
		constexpr float kTimingTablePercentColumnWidth = 45.0f;
		constexpr double kStatsRefreshSeconds = 1.0;

		Vec4 DefaultTextColor()
		{
			return dmui::ui::GetStyleColor(dmui::ui::Color::kText);
		}

		// Comparators for a timing table's columns: Pass by name (case-insensitive), then
		// Avg / P95 / P99 by value, and % by the average's share, which orders like the
		// average itself. Rows are pointers into the cached entries.
		template <class Row, class NameOf, class MetricsOf>
		std::vector<Util::TableRowSortFunc<const Row*>> TimingRowSorts(NameOf a_nameOf, MetricsOf a_metricsOf)
		{
			auto byName = [a_nameOf](const Row* a_lhs, const Row* a_rhs, bool a_ascending) {
				auto less = [](const std::string& a_l, const std::string& a_r) {
					return std::ranges::lexicographical_compare(a_l, a_r, [](char a_x, char a_y) {
						return std::tolower(static_cast<unsigned char>(a_x)) < std::tolower(static_cast<unsigned char>(a_y));
					});
				};
				return a_ascending ? less(a_nameOf(*a_lhs), a_nameOf(*a_rhs)) : less(a_nameOf(*a_rhs), a_nameOf(*a_lhs));
			};
			auto byMetric = [a_metricsOf](size_t a_index) {
				return [a_metricsOf, a_index](const Row* a_lhs, const Row* a_rhs, bool a_ascending) {
					const float l = a_metricsOf(*a_lhs)[a_index];
					const float r = a_metricsOf(*a_rhs)[a_index];
					return a_ascending ? l < r : r < l;
				};
			};
			return { byName, byMetric(0), byMetric(1), byMetric(2), byMetric(0) };
		}

		struct GraphLayout
		{
			float graphWidth;
			float legendWidth;
			float height;
			float uiScale;
		};

		GraphLayout GetGraphLayout(float availableWidth, float baseLegendWidth, float baseHeight)
		{
			const float uiScale = Util::GetUIScale();
			const float contentWidth = std::max(0.0f, availableWidth);
			const float minGraphWidth = kMinGraphWidth * uiScale;
			const float desiredLegendWidth = baseLegendWidth * uiScale;
			const float legendWidth = contentWidth > minGraphWidth ?
			                              std::min(desiredLegendWidth, contentWidth - minGraphWidth) :
			                              0.0f;

			return {
				contentWidth - legendWidth,
				legendWidth,
				baseHeight * uiScale,
				uiScale
			};
		}

		// FO4: DmUI has no TreeNodeEx; a selectable with an arrow toggles the group.
		bool GroupNode(const std::string& a_name, std::unordered_set<std::string>& a_expanded)
		{
			const bool open = a_expanded.contains(a_name);
			const auto label = std::string(open ? "v " : "> ") + a_name + "##group";
			if (dmui::ui::Selectable(label.c_str())) {
				if (open)
					a_expanded.erase(a_name);
				else
					a_expanded.insert(a_name);
				return !open;
			}
			return open;
		}
	}

	Color32 ProfilingRenderer::GetGroupColor(const std::string& groupName)
	{
		auto it = groupColorMap.find(groupName);
		if (it != groupColorMap.end())
			return it->second;

		float hue = std::fmod(nextColorIndex * kGoldenRatio, 1.0f);
		Color32 color = HslToColor32(hue, 0.7f, 0.55f);
		groupColorMap[groupName] = color;
		nextColorIndex++;
		return color;
	}

	Vec4 ProfilingRenderer::HeatColor(float value, float maxValue)
	{
		if (maxValue <= 0.0f)
			return DefaultTextColor();

		float x = std::clamp(value / maxValue, 0.0f, 1.0f);

		float x2 = x * x;
		float x3 = x2 * x;
		float x4 = x2 * x2;
		float x5 = x3 * x2;

		float r = 0.13572138f + 4.61539260f * x - 42.66032258f * x2 + 132.13108234f * x3 - 152.94239396f * x4 + 59.28637943f * x5;
		float g = 0.09140261f + 2.19418839f * x + 4.84296658f * x2 - 14.18503333f * x3 + 4.27729857f * x4 + 2.82956604f * x5;
		float b = 0.10667330f + 12.64194608f * x - 60.58204836f * x2 + 110.36276771f * x3 - 89.90310912f * x4 + 27.34824973f * x5;

		float alpha = dmui::ui::GetStyleColor(dmui::ui::Color::kWindowBg).w;

		return Vec4{ std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f), alpha };
	}

	void ProfilingRenderer::TextHeat(const char* fmt, float value, float maxValue)
	{
		const Vec4 bg = HeatColor(value, maxValue);
		dmui::ui::TableSetBgColor(dmui::ui::TableBgTarget::kCell, dmui::ui::GetColorU32(bg));
		dmui::ui::TextColored(DefaultTextColor(), fmt, value);
	}

	void ProfilingRenderer::RenderTimingModeToggle()
	{
		// FO4: DmUI has no RadioButton; a two-button toggle picks the mode.
		dmui::ui::PushID("ProfilingTimingMode");
		const auto option = [](const char* a_label, TimingMode a_mode) {
			const bool selected = timingMode == a_mode;
			const auto fill = selected ? dmui::ui::Color::kButtonActive : dmui::ui::Color::kFrameBg;
			dmui::ui::PushStyleColor(dmui::ui::Color::kButton, dmui::ui::GetStyleColor(fill));
			if (dmui::ui::Button(a_label) && !selected) {
				timingMode = a_mode;
				timeSinceLastUpdate = kStatsRefreshSeconds;
			}
			dmui::ui::PopStyleColor();
		};
		option("GPU", TimingMode::GPU);
		dmui::ui::SameLine();
		option("CPU", TimingMode::CPU);
		dmui::ui::PopID();
	}

	void ProfilingRenderer::SetupTimingTableColumns(bool includePercentColumn)
	{
		const float scale = Util::GetUIScale();
		// Timings sort slowest first on the first click, names A to Z.
		constexpr auto metricFlags = TableColumnFlags::kWidthFixed | TableColumnFlags::kPreferSortDescending;
		dmui::ui::TableSetupColumn("Pass", TableColumnFlags::kWidthStretch, 3.0f);
		dmui::ui::TableSetupColumn("Avg", metricFlags, kTimingTableMetricColumnWidth * scale);
		dmui::ui::TableSetupColumn("P95", metricFlags, kTimingTableMetricColumnWidth * scale);
		dmui::ui::TableSetupColumn("P99", metricFlags, kTimingTableMetricColumnWidth * scale);
		if (includePercentColumn)
			dmui::ui::TableSetupColumn("%", metricFlags, kTimingTablePercentColumnWidth * scale);
	}

	void ProfilingRenderer::RenderGraph()
	{
		auto& profiler = render::profiling::GetProfiler();
		const auto& results = profiler.GetResults();
		bool cpuMode = (timingMode == TimingMode::CPU);

		if (results.empty())
			return;

		std::vector<legit::ProfilerTask> tasks;

		double accumulated = 0.0;
		for (const auto& result : results) {
			if (!result.valid)
				continue;

			float timeMs = cpuMode ? result.cpuTimeMs : result.gpuTimeMs;

			std::string groupName;
			auto pos = result.name.find("::");
			if (pos != std::string::npos)
				groupName = result.name.substr(0, pos);
			else
				groupName = result.name;

			legit::ProfilerTask task;
			task.startTime = accumulated / 1000.0;
			task.endTime = (accumulated + timeMs) / 1000.0;
			task.name = result.name;
			task.color = GetGroupColor(groupName);
			tasks.push_back(task);
			accumulated += timeMs;
		}

		if (!tasks.empty())
			gpuGraph.Render(tasks, kMainGraphLegendWidth, kMainGraphHeight, kMainGraphMinFrameTimeSec);
	}

	void ProfilingRenderer::GraphState::Render(const std::vector<legit::ProfilerTask>& tasks, float baseLegendWidth, float baseHeight, float minFrameTimeSec)
	{
		if (gate.ShouldSample())
			graph.LoadFrameData(tasks.data(), tasks.size());

		const float maxFrameTimeSec = std::max(graph.GetPeakFrameTime() * kGraphHeadroomScale, minFrameTimeSec);
		const auto layout = GetGraphLayout(dmui::ui::GetContentRegionAvail().x, baseLegendWidth, baseHeight);

		graph.RenderTimings(layout.graphWidth, layout.legendWidth, layout.height, 0, maxFrameTimeSec, layout.uiScale);

		dmui::ui::Spacing();
	}

	void ProfilingRenderer::RenderStatistics(bool showTable, bool showModeToggle)
	{
		auto& profiler = render::profiling::GetProfiler();

		bool cpuMode = (timingMode == TimingMode::CPU);
		if (showModeToggle) {
			RenderTimingModeToggle();
			cpuMode = (timingMode == TimingMode::CPU);
			dmui::ui::Separator();
		}

		const double currentTime = Util::GetNowSecs();
		const double deltaTime = currentTime - lastFrameTime;
		lastFrameTime = currentTime;
		timeSinceLastUpdate += deltaTime;

		if (timeSinceLastUpdate >= kStatsRefreshSeconds) {
			timeSinceLastUpdate = 0.0;

			cachedGroups.clear();
			cachedTotalAvgMs = 0.0f;
			cachedTotalP95Ms = 0.0f;
			cachedTotalP99Ms = 0.0f;
			cachedMaxAvgMs = 0.0f;
			cachedMaxP95Ms = 0.0f;
			cachedMaxP99Ms = 0.0f;
			std::unordered_map<std::string, size_t> groupIndex;

			for (const auto& result : profiler.GetStatistics()) {
				if (!result.valid)
					continue;

				float avg = cpuMode ? result.cpuAvgMs : result.avgMs;
				float p95 = cpuMode ? result.cpuP95Ms : result.p95Ms;
				float p99 = cpuMode ? result.cpuP99Ms : result.p99Ms;

				cachedTotalAvgMs += avg;
				cachedTotalP95Ms += p95;
				cachedTotalP99Ms += p99;

				auto pos = result.name.find("::");
				if (pos != std::string::npos) {
					std::string groupName = result.name.substr(0, pos);
					std::string passLabel = result.name.substr(pos + 2);

					auto it = groupIndex.find(groupName);
					if (it == groupIndex.end()) {
						groupIndex[groupName] = cachedGroups.size();
						cachedGroups.push_back({ groupName, 0, 0, 0 });
					}

					auto& group = cachedGroups[groupIndex[groupName]];
					group.totalAvgMs += avg;
					group.totalP95Ms += p95;
					group.totalP99Ms += p99;
					group.passes.push_back({ passLabel, avg, p95, p99 });
				} else {
					groupIndex[result.name] = cachedGroups.size();
					cachedGroups.push_back({ result.name, avg, p95, p99 });
				}
			}

			for (const auto& group : cachedGroups) {
				cachedMaxAvgMs = std::max(cachedMaxAvgMs, group.totalAvgMs);
				cachedMaxP95Ms = std::max(cachedMaxP95Ms, group.totalP95Ms);
				cachedMaxP99Ms = std::max(cachedMaxP99Ms, group.totalP99Ms);
			}
		}

		if (cachedGroups.empty()) {
			dmui::ui::TextDisabled("%s", "No timing data available (enter game world)");
			return;
		}

		RenderGraph();

		if (showTable) {
			float availHeight = dmui::ui::GetContentRegionAvail().y - dmui::ui::GetFrameHeight();

			if (dmui::ui::BeginTable("##Profiler", 5,
					dmui::ui::TableFlags::kBordersInnerV | dmui::ui::TableFlags::kSizingStretchProp | dmui::ui::TableFlags::kPadOuterX | dmui::ui::TableFlags::kScrollY |
						dmui::ui::TableFlags::kSortable | dmui::ui::TableFlags::kSortTristate,
					Vec2{ 0.0f, availHeight })) {
				dmui::ui::TableSetupScrollFreeze(0, 1);
				SetupTimingTableColumns(true);
				dmui::ui::TableHeadersRow();

				// Groups sort among themselves and each group's passes within it. No
				// active sort (the tristate's third click) keeps the order the passes ran in.
				const Util::TableSortSpec sort = Util::ReadTableSortSpec();
				const auto groupSorts = TimingRowSorts<GroupEntry>([](const GroupEntry& a_group) -> const std::string& { return a_group.name; },
					[](const GroupEntry& a_group) { return std::array{ a_group.totalAvgMs, a_group.totalP95Ms, a_group.totalP99Ms }; });
				const auto passSorts = TimingRowSorts<PassEntry>([](const PassEntry& a_pass) -> const std::string& { return a_pass.label; },
					[](const PassEntry& a_pass) { return std::array{ a_pass.avgMs, a_pass.p95Ms, a_pass.p99Ms }; });
				std::vector<const GroupEntry*> groups;
				groups.reserve(cachedGroups.size());
				for (const auto& group : cachedGroups)
					groups.push_back(&group);
				Util::SortTableRows(groups, sort, groupSorts);

				const auto metrics = [](float a_avg, float a_p95, float a_p99) {
					(void)dmui::ui::TableNextColumn();
					TextHeat("%.3f", a_avg, cachedMaxAvgMs);
					(void)dmui::ui::TableNextColumn();
					TextHeat("%.3f", a_p95, cachedMaxP95Ms);
					(void)dmui::ui::TableNextColumn();
					TextHeat("%.3f", a_p99, cachedMaxP99Ms);
					(void)dmui::ui::TableNextColumn();
					if (cachedTotalAvgMs > 0.0f)
						TextHeat("%5.1f", (a_avg / cachedTotalAvgMs) * 100.0f, 100.0f);
				};

				for (const GroupEntry* groupRow : groups) {
					const auto& group = *groupRow;
					dmui::ui::TableNextRow();
					(void)dmui::ui::TableNextColumn();

					if (group.passes.empty()) {
						dmui::ui::TextUnformatted(group.name.c_str());
						metrics(group.totalAvgMs, group.totalP95Ms, group.totalP99Ms);
					} else {
						bool open = GroupNode(group.name, expandedGroups);
						metrics(group.totalAvgMs, group.totalP95Ms, group.totalP99Ms);
						if (open) {
							std::vector<const PassEntry*> passes;
							passes.reserve(group.passes.size());
							for (const auto& pass : group.passes)
								passes.push_back(&pass);
							Util::SortTableRows(passes, sort, passSorts);
							for (const PassEntry* passRow : passes) {
								const auto& pass = *passRow;
								dmui::ui::TableNextRow();
								(void)dmui::ui::TableNextColumn();
								dmui::ui::Indent();
								dmui::ui::TextUnformatted(pass.label.c_str());
								dmui::ui::Unindent();
								metrics(pass.avgMs, pass.p95Ms, pass.p99Ms);
							}
						}
					}
				}
				dmui::ui::EndTable();
			}
		}
	}

	void ProfilingRenderer::RenderFeatureTimers(const std::string& featurePrefix)
	{
		auto& profiler = render::profiling::GetProfiler();
		const auto& results = profiler.GetResults();

		RenderTimingModeToggle();

		bool cpuMode = (timingMode == TimingMode::CPU);

		struct Entry
		{
			std::string label;
			float timeMs;
			float avgMs;
			float p95Ms;
			float p99Ms;
		};

		std::vector<Entry> entries;
		float totalAvg = 0.0f;
		float totalP95 = 0.0f;
		float totalP99 = 0.0f;
		float maxAvg = 0.0f;
		float maxP95 = 0.0f;
		float maxP99 = 0.0f;

		if (!HasFeatureTimers(featurePrefix)) {
			dmui::ui::TextDisabled("%s", "No timing data");
			return;
		}

		const auto prefix = GetFeatureTimerPrefix(featurePrefix);
		auto& state = featureGraphs[featurePrefix];
		const double currentTime = Util::GetNowSecs();
		if (state.lastStatisticsUpdate < 0.0 || currentTime - state.lastStatisticsUpdate >= kStatsRefreshSeconds) {
			state.statistics.clear();
			for (const auto& result : profiler.GetStatistics(prefix))
				state.statistics.insert_or_assign(result.name, PassStatistics{ result.avgMs, result.p95Ms, result.p99Ms, result.cpuAvgMs, result.cpuP95Ms, result.cpuP99Ms });
			state.lastStatisticsUpdate = currentTime;
		}
		for (const auto& r : results) {
			if (!IsFeatureTimerResult(r, prefix))
				continue;
			std::string label = r.name.substr(prefix.size());
			float timeMs = cpuMode ? r.cpuTimeMs : r.gpuTimeMs;
			const auto cached = state.statistics.find(r.name);
			// Show a new pass's current sample until the next statistics refresh.
			float avg = timeMs;
			float p95 = timeMs;
			float p99 = timeMs;
			if (cached != state.statistics.end()) {
				const auto& statistics = cached->second;
				avg = cpuMode ? statistics.cpuAvgMs : statistics.avgMs;
				p95 = cpuMode ? statistics.cpuP95Ms : statistics.p95Ms;
				p99 = cpuMode ? statistics.cpuP99Ms : statistics.p99Ms;
			}
			entries.push_back({ label, timeMs, avg, p95, p99 });
			totalAvg += avg;
			totalP95 += p95;
			totalP99 += p99;
			maxAvg = std::max(maxAvg, avg);
			maxP95 = std::max(maxP95, p95);
			maxP99 = std::max(maxP99, p99);
		}

		if (entries.empty()) {
			dmui::ui::TextDisabled("%s", "No timing data");
			return;
		}

		std::vector<legit::ProfilerTask> tasks;
		double accumulated = 0.0;
		for (const auto& e : entries) {
			legit::ProfilerTask task;
			task.startTime = accumulated / 1000.0;
			task.endTime = (accumulated + e.timeMs) / 1000.0;
			task.name = e.label;
			task.color = GetGroupColor(featurePrefix + "::" + e.label);
			tasks.push_back(task);
			accumulated += e.timeMs;
		}

		state.Render(tasks, kFeatureGraphLegendWidth, kFeatureGraphHeight, kFeatureGraphMinFrameTimeSec);

		if (dmui::ui::BeginTable("##FeatureTimers", 4, dmui::ui::TableFlags::kBordersInnerV | dmui::ui::TableFlags::kSizingStretchProp | dmui::ui::TableFlags::kPadOuterX | dmui::ui::TableFlags::kSortable | dmui::ui::TableFlags::kSortTristate)) {
			SetupTimingTableColumns(false);
			dmui::ui::TableHeadersRow();

			// The Total row stays last whatever the order.
			std::vector<const Entry*> rows;
			rows.reserve(entries.size());
			for (const auto& e : entries)
				rows.push_back(&e);
			Util::SortTableRows(rows, Util::ReadTableSortSpec(),
				TimingRowSorts<Entry>([](const Entry& a_entry) -> const std::string& { return a_entry.label; },
					[](const Entry& a_entry) { return std::array{ a_entry.avgMs, a_entry.p95Ms, a_entry.p99Ms }; }));

			for (const Entry* row : rows) {
				const auto& e = *row;
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableNextColumn();
				dmui::ui::TextUnformatted(e.label.c_str());
				(void)dmui::ui::TableNextColumn();
				TextHeat("%.3f", e.avgMs, maxAvg);
				(void)dmui::ui::TableNextColumn();
				TextHeat("%.3f", e.p95Ms, maxP95);
				(void)dmui::ui::TableNextColumn();
				TextHeat("%.3f", e.p99Ms, maxP99);
			}

			dmui::ui::TableNextRow();
			(void)dmui::ui::TableNextColumn();
			const auto totalColor = dmui::ui::GetThemeColors().info;
			dmui::ui::TextColored(totalColor, "%s", "Total");
			(void)dmui::ui::TableNextColumn();
			dmui::ui::TextColored(totalColor, "%.3f", totalAvg);
			(void)dmui::ui::TableNextColumn();
			dmui::ui::TextColored(totalColor, "%.3f", totalP95);
			(void)dmui::ui::TableNextColumn();
			dmui::ui::TextColored(totalColor, "%.3f", totalP99);

			dmui::ui::EndTable();
		}
	}

	bool ProfilingRenderer::HasFeatureTimers(const std::string& featurePrefix)
	{
		const auto prefix = GetFeatureTimerPrefix(featurePrefix);
		const auto& results = render::profiling::GetProfiler().GetResults();

		return std::ranges::any_of(results, [&prefix](const auto& result) {
			return IsFeatureTimerResult(result, prefix);
		});
	}

	std::string ProfilingRenderer::GetFeatureTimerPrefix(const std::string& featurePrefix)
	{
		return featurePrefix + "::";
	}

	bool ProfilingRenderer::IsFeatureTimerResult(const Profiler::TimerResult& result, std::string_view prefix)
	{
		return result.valid && result.name.starts_with(prefix);
	}
}
