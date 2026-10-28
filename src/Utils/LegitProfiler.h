// Based on LegitProfiler by Raikiri (https://github.com/Raikiri/LegitProfiler)
// MIT License - glm dependency removed; FO4 uses dmui::ui::Vec2.
#pragma once

#include <DearModdingUI/UI.h>
#include <DearModdingUI/UI/DrawList.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace legit
{
	namespace Colors
	{
		// FO4: DmUI packs colors as RRGGBBAA, so the ImGui byte swap is gone.
		const static uint32_t imguiText = 0xF2F5FAFFu;
	}

	/** @brief A single profiling task with timing, name, and display colour. */
	struct ProfilerTask
	{
		double startTime;
		double endTime;
		std::string name;
		uint32_t color;

		/** @brief Get the duration of this task in seconds. */
		double GetLength() { return endTime - startTime; }
	};
}

namespace ImGuiUtils
{
	using Vec2 = dmui::ui::Vec2;

	/**
	 * @brief Widget that renders a scrolling profiler graph with per-frame task bars and a legend.
	 *
	 * Maintains a ring buffer of frame data and renders a stacked bar chart showing per-task
	 * timing alongside a sorted legend of the most significant tasks.
	 */
	class ProfilerGraph
	{
	public:
		int frameWidth;
		int frameSpacing;
		bool useColoredLegendText;

		/**
		 * @brief Construct a profiler graph with a fixed ring buffer size.
		 * @param framesCount Number of frames to keep in the ring buffer.
		 */
		ProfilerGraph(size_t framesCount)
		{
			frames.resize(framesCount);
			for (auto& frame : frames)
				frame.tasks.reserve(100);
			frameWidth = 3;
			frameSpacing = 1;
			useColoredLegendText = false;
		}

		/**
		 * @brief Load task data for the current frame into the ring buffer.
		 *
		 * Adjacent tasks with the same name and colour are merged. Statistics
		 * are rebuilt after insertion.
		 *
		 * @param tasks Array of profiler tasks for this frame.
		 * @param count Number of tasks in the array.
		 */
		void LoadFrameData(const legit::ProfilerTask* tasks, size_t count)
		{
			auto& currFrame = frames[currFrameIndex];
			currFrame.tasks.resize(0);
			currFrame.totalTime = 0.0f;
			for (size_t taskIndex = 0; taskIndex < count; taskIndex++) {
				if (taskIndex == 0)
					currFrame.tasks.push_back(tasks[taskIndex]);
				else {
					if (tasks[taskIndex - 1].color != tasks[taskIndex].color || tasks[taskIndex - 1].name != tasks[taskIndex].name)
						currFrame.tasks.push_back(tasks[taskIndex]);
					else
						currFrame.tasks.back().endTime = tasks[taskIndex].endTime;
				}
				currFrame.totalTime += float(tasks[taskIndex].endTime - tasks[taskIndex].startTime);
			}
			currFrame.taskStatsIndex.resize(currFrame.tasks.size());

			for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++) {
				auto& task = currFrame.tasks[taskIndex];
				auto it = taskNameToStatsIndex.find(task.name);
				if (it == taskNameToStatsIndex.end()) {
					taskNameToStatsIndex[task.name] = taskStats.size();
					TaskStats taskStat;
					taskStat.maxTime = -1.0;
					taskStats.push_back(taskStat);
				}
				currFrame.taskStatsIndex[taskIndex] = taskNameToStatsIndex[task.name];
			}
			{
				float recentMax = 0.0f;
				size_t lookback = std::min(frames.size(), size_t(120));
				for (size_t i = 0; i < lookback; i++) {
					size_t idx = (currFrameIndex + frames.size() - 1 - i) % frames.size();
					recentMax = std::max(recentMax, frames[idx].totalTime);
				}
				if (peakFrameTime <= 0.0f)
					peakFrameTime = recentMax;
				else {
					float rate = (recentMax < peakFrameTime) ? 0.02f : 0.01f;
					peakFrameTime += (recentMax - peakFrameTime) * rate;
				}
			}
			currFrameIndex = (currFrameIndex + 1) % frames.size();
			RebuildTaskStats(currFrameIndex, 300);
		}

		/**
		 * @brief Get the total task time for a frame at the given offset from the current frame.
		 * @param frameIndexOffset Offset backwards from the current frame index.
		 * @return Total accumulated task time for that frame.
		 */
		float GetTotalTaskTime(int frameIndexOffset)
		{
			return frames[GetCurrFrameIndex(frameIndexOffset)].totalTime;
		}

		/** @brief Get the smoothed peak frame time across recent frames. */
		float GetPeakFrameTime() const { return peakFrameTime; }

		/**
		 * @brief Render the profiler graph and optional legend into the current window.
		 * @param graphWidth Width of the graph area in pixels.
		 * @param legendWidth Width of the legend area (0 to hide legend).
		 * @param height Height of the entire widget in pixels.
		 * @param frameIndexOffset Offset backwards from the current frame to display.
		 * @param maxFrameTime Time value corresponding to the full graph height.
		 * @param uiScale DPI scale factor for rendering.
		 */
		void RenderTimings(float graphWidth, float legendWidth, float height, int frameIndexOffset, float maxFrameTime, float uiScale = 1.0f)
		{
			const auto drawList = dmui::ui::WindowDrawList();
			const Vec2 widgetPos = dmui::ui::GetCursorScreenPos();
			RenderGraph(drawList, widgetPos, Vec2{ graphWidth, height }, frameIndexOffset, maxFrameTime, uiScale);
			if (legendWidth > 0.0f)
				RenderLegend(drawList, Vec2{ widgetPos.x + graphWidth, widgetPos.y }, Vec2{ legendWidth, height }, frameIndexOffset, maxFrameTime, uiScale);
			dmui::ui::Dummy(Vec2{ graphWidth + legendWidth, height });
		}

	private:
		size_t GetCurrFrameIndex(size_t frameIndexOffset)
		{
			return (currFrameIndex - frameIndexOffset - 1 + 2 * frames.size()) % frames.size();
		}

		void RebuildTaskStats(size_t endFrame, size_t framesCount)
		{
			for (auto& taskStat : taskStats) {
				taskStat.maxTime = -1.0f;
				taskStat.priorityOrder = size_t(-1);
				taskStat.onScreenIndex = size_t(-1);
			}

			for (size_t frameNumber = 0; frameNumber < framesCount; frameNumber++) {
				size_t frameIndex = (endFrame - 1 - frameNumber + frames.size()) % frames.size();
				auto& frame = frames[frameIndex];
				for (size_t taskIndex = 0; taskIndex < frame.tasks.size(); taskIndex++) {
					auto& task = frame.tasks[taskIndex];
					auto& stats = taskStats[frame.taskStatsIndex[taskIndex]];
					stats.maxTime = std::max(stats.maxTime, task.endTime - task.startTime);
				}
			}
			std::vector<size_t> statPriorities;
			statPriorities.resize(taskStats.size());
			for (size_t statIndex = 0; statIndex < taskStats.size(); statIndex++)
				statPriorities[statIndex] = statIndex;

			std::sort(statPriorities.begin(), statPriorities.end(), [this](size_t left, size_t right) { return taskStats[left].maxTime > taskStats[right].maxTime; });
			for (size_t statNumber = 0; statNumber < taskStats.size(); statNumber++) {
				size_t statIndex = statPriorities[statNumber];
				taskStats[statIndex].priorityOrder = statNumber;
			}
		}

		void RenderGraph(const dmui::ui::DrawList& drawList, Vec2 graphPos, Vec2 graphSize, size_t frameIndexOffset, float maxFrameTime, float uiScale)
		{
			Rect(drawList, graphPos, Vec2{ graphPos.x + graphSize.x, graphPos.y + graphSize.y }, 0xffffffff, false);
			const float scaledFrameWidth = std::max(1.0f, static_cast<float>(frameWidth) * uiScale);
			const float scaledFrameSpacing = std::max(1.0f, static_cast<float>(frameSpacing) * uiScale);
			const float heightThreshold = uiScale;

			for (size_t frameNumber = 0; frameNumber < frames.size(); frameNumber++) {
				size_t frameIndex = GetCurrFrameIndex(frameIndexOffset + frameNumber);

				Vec2 framePos{ graphPos.x + graphSize.x - uiScale - scaledFrameWidth - (scaledFrameWidth + scaledFrameSpacing) * float(frameNumber), graphPos.y + graphSize.y - uiScale };
				if (framePos.x < graphPos.x + uiScale)
					break;
				Vec2 taskPos = framePos;
				auto& frame = frames[frameIndex];
				for (const auto& task : frame.tasks) {
					float taskStartHeight = (float(task.startTime) / maxFrameTime) * graphSize.y;
					float taskEndHeight = (float(task.endTime) / maxFrameTime) * graphSize.y;
					if (std::abs(taskEndHeight - taskStartHeight) > heightThreshold)
						Rect(drawList, Vec2{ taskPos.x, taskPos.y - taskStartHeight }, Vec2{ taskPos.x + scaledFrameWidth, taskPos.y - taskEndHeight }, task.color, true);
				}
			}
		}

		void RenderLegend(const dmui::ui::DrawList& drawList, Vec2 legendPos, Vec2 legendSize, size_t frameIndexOffset, float maxFrameTime, float uiScale)
		{
			float markerLeftRectMargin = 3.0f * uiScale;
			float markerLeftRectWidth = 5.0f * uiScale;
			float markerMidWidth = 30.0f * uiScale;
			float markerRightRectWidth = 10.0f * uiScale;
			float markerRigthRectMargin = 3.0f * uiScale;
			float markerRightRectHeight = 10.0f * uiScale;
			float markerRightRectSpacing = 4.0f * uiScale;
			// FO4: DmUI's font is far taller than ImGui's 13px; rows space by it.
			const float fontSize = dmui::ui::GetFontSize();
			const float rowPitch = std::max(markerRightRectHeight, fontSize) + markerRightRectSpacing;
			float textMarginX = 5.0f * uiScale;

			auto& currFrame = frames[GetCurrFrameIndex(frameIndexOffset)];
			size_t maxTasksCount = size_t(legendSize.y / rowPitch);

			for (auto& taskStat : taskStats)
				taskStat.onScreenIndex = size_t(-1);

			size_t tasksToShow = std::min<size_t>(taskStats.size(), maxTasksCount);
			size_t tasksShownCount = 0;
			for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++) {
				auto& task = currFrame.tasks[taskIndex];
				auto& stat = taskStats[currFrame.taskStatsIndex[taskIndex]];

				if (stat.priorityOrder >= tasksToShow)
					continue;

				if (stat.onScreenIndex == size_t(-1))
					stat.onScreenIndex = tasksShownCount++;
				else
					continue;

				float taskStartHeight = (float(task.startTime) / maxFrameTime) * legendSize.y;
				float taskEndHeight = (float(task.endTime) / maxFrameTime) * legendSize.y;

				Vec2 markerLeftRectMin{ legendPos.x + markerLeftRectMargin, legendPos.y + legendSize.y };
				Vec2 markerLeftRectMax{ markerLeftRectMin.x + markerLeftRectWidth, markerLeftRectMin.y };
				markerLeftRectMin.y -= taskStartHeight;
				markerLeftRectMax.y -= taskEndHeight;

				Vec2 markerRightRectMin{ legendPos.x + markerLeftRectMargin + markerLeftRectWidth + markerMidWidth, legendPos.y + legendSize.y - markerRigthRectMargin - rowPitch * float(stat.onScreenIndex) };
				Vec2 markerRightRectMax{ markerRightRectMin.x + markerRightRectWidth, markerRightRectMin.y - markerRightRectHeight };
				RenderTaskMarker(drawList, markerLeftRectMin, markerLeftRectMax, markerRightRectMin, markerRightRectMax, task.color);

				uint32_t textColor = useColoredLegendText ? task.color : legit::Colors::imguiText;

				float taskTimeMs = float(task.endTime - task.startTime);
				// FO4: one string; a fixed name offset overlaps DmUI's wider digits.
				const float textY = (markerRightRectMin.y + markerRightRectMax.y - fontSize) * 0.5f;
				Text(drawList, Vec2{ markerRightRectMax.x + textMarginX, textY }, textColor, std::format("[{:.2f}ms] {}", taskTimeMs * 1000.0f, task.name));
			}
		}

		static void Rect(const dmui::ui::DrawList& drawList, Vec2 minPoint, Vec2 maxPoint, uint32_t col, bool filled = true)
		{
			// FO4: DmUI rejects the inverted rects ImGui accepts; bars draw bottom-up.
			const Vec2 lo{ std::min(minPoint.x, maxPoint.x), std::min(minPoint.y, maxPoint.y) };
			maxPoint = Vec2{ std::max(minPoint.x, maxPoint.x), std::max(minPoint.y, maxPoint.y) };
			minPoint = lo;
			if (filled)
				drawList.AddRectFilled(minPoint, maxPoint, col);
			else
				drawList.AddRect(minPoint, maxPoint, col);
		}

		static void Text(const dmui::ui::DrawList& drawList, Vec2 point, uint32_t col, const std::string& text)
		{
			drawList.AddText(point, col, text);
		}

		static void RenderTaskMarker(const dmui::ui::DrawList& drawList, Vec2 leftMinPoint, Vec2 leftMaxPoint, Vec2 rightMinPoint, Vec2 rightMaxPoint, uint32_t col)
		{
			Rect(drawList, leftMinPoint, leftMaxPoint, col, true);
			Rect(drawList, rightMinPoint, rightMaxPoint, col, true);
			std::array<Vec2, 4> points = {
				Vec2{ leftMaxPoint.x, leftMinPoint.y },
				Vec2{ leftMaxPoint.x, leftMaxPoint.y },
				Vec2{ rightMinPoint.x, rightMaxPoint.y },
				Vec2{ rightMinPoint.x, rightMinPoint.y }
			};
			drawList.AddPolygonFilled(points, col);
		}

		struct FrameData
		{
			std::vector<legit::ProfilerTask> tasks;
			std::vector<size_t> taskStatsIndex;
			float totalTime;
		};

		struct TaskStats
		{
			double maxTime;
			size_t priorityOrder;
			size_t onScreenIndex;
		};

		std::vector<TaskStats> taskStats;
		std::map<std::string, size_t> taskNameToStatsIndex;
		std::vector<FrameData> frames;
		size_t currFrameIndex = 0;
		float peakFrameTime = 0.0f;
	};
}
