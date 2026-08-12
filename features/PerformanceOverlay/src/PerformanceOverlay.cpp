#include "PerformanceOverlay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>
#include <Windows.h>
#include <dxgi1_4.h>

#include "Env.h"
#include "FrameGeneration.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.performanceoverlay"); }

	constexpr std::array<float, 3> kFrameTimeReferenceFps{ 30.0f, 60.0f, 120.0f };
	constexpr const char* kPostFgFrameTimeTooltip =
		"Backend-reported when available (DLSS-G slDLSSGGetState, XeSS-FG xefgSwapChainGetLastPresentStatus accumulate into a per-tick counter). FSR3 falls back to engine frame time divided by the configured multiplier.";

	namespace
	{
		std::string SettingError(std::string_view a_key, std::string_view a_reason)
		{
			return "settings." + std::string(a_key) + ": " + std::string(a_reason);
		}

		bool AcceptSetting(
			feature_config::ScalarReadStatus a_status,
			std::string_view a_key,
			std::string_view a_expected,
			std::string_view a_range,
			std::string& a_error)
		{
			switch (a_status) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = SettingError(a_key, "expected " + std::string(a_expected));
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = SettingError(a_key, "value must be finite");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = SettingError(a_key, a_range);
				break;
			}
			return false;
		}

		bool ReadBoolSetting(
			const toml::table& a_table,
			std::string_view a_key,
			bool& a_value,
			std::string& a_error)
		{
			return AcceptSetting(
				feature_config::ReadBool(a_table, a_key, a_value),
				a_key, "boolean", "boolean value is out of range", a_error);
		}

		bool ReadIntegerSetting(
			const toml::table& a_table,
			std::string_view a_key,
			std::int64_t a_min,
			std::int64_t a_max,
			std::string_view a_range,
			int& a_value,
			std::string& a_error)
		{
			auto value = static_cast<std::int64_t>(a_value);
			const auto status = feature_config::ReadSignedInteger(a_table, a_key, value, a_min, a_max);
			if (!AcceptSetting(status, a_key, "integer", a_range, a_error)) {
				return false;
			}
			if (status == feature_config::ScalarReadStatus::kValid) {
				a_value = static_cast<int>(value);
			}
			return true;
		}

		bool ReadFloatSetting(
			const toml::table& a_table,
			std::string_view a_key,
			float a_min,
			float a_max,
			std::string_view a_range,
			float& a_value,
			std::string& a_error)
		{
			return AcceptSetting(
				feature_config::ReadFloat(a_table, a_key, a_value, a_min, a_max),
				a_key, "number", a_range, a_error);
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			PerformanceOverlay::Settings& a_candidate,
			int a_historyCapacity,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode) {
				return true;
			}

			const auto* settingsTable = settingsNode->as_table();
			if (!settingsTable) {
				a_error = "settings: expected table";
				return false;
			}

			const auto floatLowest = std::numeric_limits<float>::lowest();
			const auto floatMax = std::numeric_limits<float>::max();
			return ReadBoolSetting(*settingsTable, "enabled", a_candidate.enabled, a_error)
				&& ReadIntegerSetting(*settingsTable, "preset", 0, 3, "value must be in range 0..3", a_candidate.preset, a_error)
				&& ReadBoolSetting(*settingsTable, "show_fps", a_candidate.showFps, a_error)
				&& ReadBoolSetting(*settingsTable, "show_frame_time", a_candidate.showFrameTime, a_error)
				&& ReadBoolSetting(*settingsTable, "show_graph", a_candidate.showGraph, a_error)
				&& ReadBoolSetting(*settingsTable, "show_estimated_post_fg_frame_time", a_candidate.showEstimatedPostFGFrameTime, a_error)
				&& ReadBoolSetting(*settingsTable, "show_vram", a_candidate.showVram, a_error)
				&& ReadBoolSetting(*settingsTable, "show_stats", a_candidate.showStats, a_error)
				&& ReadIntegerSetting(*settingsTable, "corner", 0, 3, "value must be in range 0..3", a_candidate.corner, a_error)
				&& ReadBoolSetting(*settingsTable, "free_drag", a_candidate.freeDrag, a_error)
				&& ReadFloatSetting(*settingsTable, "drag_pos_x", floatLowest, floatMax, "value must be representable as float", a_candidate.dragPosX, a_error)
				&& ReadFloatSetting(*settingsTable, "drag_pos_y", floatLowest, floatMax, "value must be representable as float", a_candidate.dragPosY, a_error)
				&& ReadFloatSetting(*settingsTable, "opacity", 0.0f, 1.0f, "value must be in range 0..1", a_candidate.opacity, a_error)
				&& ReadBoolSetting(*settingsTable, "show_border", a_candidate.showBorder, a_error)
				&& ReadFloatSetting(*settingsTable, "font_scale", 0.5f, 3.0f, "value must be in range 0.5..3", a_candidate.fontScale, a_error)
				&& ReadBoolSetting(*settingsTable, "high_contrast", a_candidate.highContrast, a_error)
				&& ReadBoolSetting(*settingsTable, "auto_thresholds", a_candidate.autoThresholds, a_error)
				&& ReadFloatSetting(*settingsTable, "fps_good", 1.0f, 1000.0f, "value must be in range 1..1000", a_candidate.fpsGood, a_error)
				&& ReadFloatSetting(*settingsTable, "fps_warn", 1.0f, 1000.0f, "value must be in range 1..1000", a_candidate.fpsWarn, a_error)
				&& ReadFloatSetting(*settingsTable, "update_interval", 0.05f, 5.0f, "value must be in range 0.05..5", a_candidate.updateInterval, a_error)
				&& ReadIntegerSetting(*settingsTable, "history_size", 30, a_historyCapacity, "value must be in range 30..600", a_candidate.historySize, a_error)
				&& ReadFloatSetting(*settingsTable, "graph_height_px", 40.0f, 160.0f, "value must be in range 40..160", a_candidate.graphHeightPx, a_error)
				&& AcceptSetting(
					feature_config::ReadString(*settingsTable, "toggle_hotkey", a_candidate.toggleHotkey),
					"toggle_hotkey", "string", "string value is out of range", a_error);
		}
	}

	PerformanceOverlay* PerformanceOverlay::GetSingleton()
	{
		static PerformanceOverlay instance;
		return &instance;
	}

	bool PerformanceOverlay::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = settings;
		if (!ParseSettingsTable(a_config, candidate, kHistoryCapacity, a_error)) {
			return false;
		}

		settings = candidate;
		RefreshToggleHotkey();
		return true;
	}

	void PerformanceOverlay::Load()
	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		_qpcFreq = static_cast<double>(freq.QuadPart);

		cs::Menu::Get().RegisterWndProcCallback(*this, &PerformanceOverlay::HandleWndProc);

		L->info("Loaded: enabled={} preset={} corner={} toggle_hotkey={}",
			settings.enabled, settings.preset, settings.corner, _toggleHotkey.ToString());
	}

	void PerformanceOverlay::RefreshToggleHotkey()
	{
		bool ok = false;
		_toggleHotkey = cs::input::Hotkey::Parse(settings.toggleHotkey, &ok);
		if (!ok)
			L->warn("Invalid toggle_hotkey '{}', overlay toggle disabled", settings.toggleHotkey);
	}

	bool PerformanceOverlay::HandleWndProc(HWND, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
	{
		auto* self = GetSingleton();

		// Consume key-up early to prevent stuck input and F10 beeps.
		if (self->_toggleReleaseVk != 0 && (a_msg == WM_KEYUP || a_msg == WM_SYSKEYUP)
			&& a_wparam == self->_toggleReleaseVk) {
			self->_toggleReleaseVk = 0;
			return true;
		}

		// Open menus leave keyboard input to ImGui.
		if (cs::Menu::Get().IsOpen())
			return false;
		if (!self->settings.enabled || self->settings.preset == static_cast<int>(Preset::Off))
			return false;

		if (self->_toggleHotkey.MatchesDown(a_msg, a_wparam, a_lparam)) {
			cs::Menu::Get().ToggleOverlay();
			self->_toggleReleaseVk = self->_toggleHotkey.vk;
			return true;
		}
		return false;
	}

	void PerformanceOverlay::SaveSettings()
	{
		toml::table settingsTable;
		settingsTable.insert_or_assign("enabled", settings.enabled);
		settingsTable.insert_or_assign("preset", static_cast<int64_t>(settings.preset));
		settingsTable.insert_or_assign("show_fps", settings.showFps);
		settingsTable.insert_or_assign("show_frame_time", settings.showFrameTime);
		settingsTable.insert_or_assign("show_graph", settings.showGraph);
		settingsTable.insert_or_assign("show_estimated_post_fg_frame_time", settings.showEstimatedPostFGFrameTime);
		settingsTable.insert_or_assign("show_vram", settings.showVram);
		settingsTable.insert_or_assign("show_stats", settings.showStats);
		settingsTable.insert_or_assign("corner", static_cast<int64_t>(settings.corner));
		settingsTable.insert_or_assign("free_drag", settings.freeDrag);
		settingsTable.insert_or_assign("drag_pos_x", static_cast<double>(settings.dragPosX));
		settingsTable.insert_or_assign("drag_pos_y", static_cast<double>(settings.dragPosY));
		settingsTable.insert_or_assign("opacity", static_cast<double>(settings.opacity));
		settingsTable.insert_or_assign("show_border", settings.showBorder);
		settingsTable.insert_or_assign("font_scale", static_cast<double>(settings.fontScale));
		settingsTable.insert_or_assign("high_contrast", settings.highContrast);
		settingsTable.insert_or_assign("auto_thresholds", settings.autoThresholds);
		settingsTable.insert_or_assign("fps_good", static_cast<double>(settings.fpsGood));
		settingsTable.insert_or_assign("fps_warn", static_cast<double>(settings.fpsWarn));
		settingsTable.insert_or_assign("update_interval", static_cast<double>(settings.updateInterval));
		settingsTable.insert_or_assign("history_size", static_cast<int64_t>(settings.historySize));
		settingsTable.insert_or_assign("graph_height_px", static_cast<double>(settings.graphHeightPx));
		settingsTable.insert_or_assign("toggle_hotkey", settings.toggleHotkey);

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settingsTable); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void PerformanceOverlay::ApplyPreset(Preset preset)
	{
		settings.preset = static_cast<int>(preset);
		switch (preset) {
			case Preset::Off:
				settings.showFps = false;
				settings.showFrameTime = false;
				settings.showGraph = false;
				settings.showEstimatedPostFGFrameTime = false;
				settings.showVram = false;
				settings.showStats = false;
				break;
			case Preset::Minimal:
				settings.showFps = true;
				settings.showFrameTime = false;
				settings.showGraph = false;
				settings.showEstimatedPostFGFrameTime = false;
				settings.showVram = false;
				settings.showStats = false;
				break;
			case Preset::Standard:
				settings.showFps = true;
				settings.showFrameTime = true;
				settings.showGraph = true;
				settings.showEstimatedPostFGFrameTime = true;
				settings.showVram = false;
				settings.showStats = false;
				break;
			case Preset::Verbose:
				settings.showFps = true;
				settings.showFrameTime = true;
				settings.showGraph = true;
				settings.showEstimatedPostFGFrameTime = true;
				settings.showVram = true;
				settings.showStats = true;
				break;
		}
	}

	void PerformanceOverlay::EnsureRefreshHz()
	{
		if (_refreshKnown)
			return;
		// Display settings provide an adequate refresh estimate.
		DEVMODEW devMode{};
		devMode.dmSize = sizeof(devMode);
		if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &devMode)) {
			_refreshHz = std::max(30.0f, static_cast<float>(devMode.dmDisplayFrequency));
			_refreshKnown = true;
			if (settings.autoThresholds) {
				settings.fpsGood = _refreshHz * 0.95f;
				settings.fpsWarn = _refreshHz * 0.5f;
			}
		}
	}

	void PerformanceOverlay::TickFrame()
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		const double nowSec = static_cast<double>(now.QuadPart) / _qpcFreq;
		const int displayedFrameMultiplier = std::max(1, cs::env::GetDisplayedFrameMultiplier());

		if (_lastFrameQpc > 0.0) {
			const float dtMs = static_cast<float>((nowSec - _lastFrameQpc) * 1000.0);
			// Ignore pause-length samples.
			if (dtMs > 0.0f && dtMs < 1000.0f) {
				_curFrameMs = dtMs;
				_frameTimesMs[_frameTimesHead] = dtMs;
				// Post-FG frame time is estimated, not measured.
				_postFgFrameTimesMs[_frameTimesHead] = dtMs / static_cast<float>(displayedFrameMultiplier);
				_frameTimesHead = (_frameTimesHead + 1) % settings.historySize;
				if (_frameTimesCount < settings.historySize)
					_frameTimesCount++;
			}
		}
		_lastFrameQpc = nowSec;

		// Cadenced updates prevent per-frame flicker.
		if (nowSec - _lastDisplayUpdate >= settings.updateInterval) {
			_displayedFrameMs = _curFrameMs;
			_displayedFps = _curFrameMs > 0.0f ? 1000.0f / _curFrameMs : 0.0f;
			_displayedFrameMultiplier = displayedFrameMultiplier;

			// Prefer backend counts; otherwise estimate from engine FPS.
			const uint64_t totalNow = cs::env::GetDisplayedFrameTotal();
			const double windowSec = nowSec - _lastDisplayedSampleSec;
			if (_lastDisplayedSampleSec > 0.0 && windowSec > 0.0 && totalNow >= _lastDisplayedFrameTotal) {
				const uint64_t delta = totalNow - _lastDisplayedFrameTotal;
				if (delta > 0)
					_measuredDisplayedFps = static_cast<float>(static_cast<double>(delta) / windowSec);
				else
					_measuredDisplayedFps = _displayedFps * static_cast<float>(displayedFrameMultiplier);
			} else {
				_measuredDisplayedFps = _displayedFps * static_cast<float>(displayedFrameMultiplier);
			}
			_lastDisplayedFrameTotal = totalNow;
			_lastDisplayedSampleSec  = nowSec;

			RecomputeStats();
			_lastDisplayUpdate = nowSec;
		}
	}

	void PerformanceOverlay::RecomputeStats()
	{
		if (_frameTimesCount == 0)
			return;
		std::vector<float> sorted;
		sorted.reserve(_frameTimesCount);
		for (int i = 0; i < _frameTimesCount; ++i)
			sorted.push_back(_frameTimesMs[i]);
		std::sort(sorted.begin(), sorted.end());

		double sum = 0.0;
		for (float v : sorted) sum += v;
		_avgMs = static_cast<float>(sum / sorted.size());

		double sqDiff = 0.0;
		for (float v : sorted) {
			const double d = v - _avgMs;
			sqDiff += d * d;
		}
		_stddevMs = static_cast<float>(std::sqrt(sqDiff / sorted.size()));

		// Lows represent the slow-frame tail.
		const auto idx99   = static_cast<size_t>(sorted.size() * 99 / 100);
		const auto idx999  = static_cast<size_t>(sorted.size() * 999 / 1000);
		_onePctLowMs       = sorted[std::min(idx99,  sorted.size() - 1)];
		_pointOnePctLowMs  = sorted[std::min(idx999, sorted.size() - 1)];
	}

	void PerformanceOverlay::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto presetName = [](int p) -> std::string_view {
			switch (static_cast<Preset>(p)) {
			case Preset::Off:      return "off";
			case Preset::Minimal:  return "minimal";
			case Preset::Standard: return "standard";
			case Preset::Verbose:  return "verbose";
			default:               return "unknown";
			}
		};
		a_sink
			.Field("enabled", settings.enabled)
			.Field("preset", presetName(settings.preset))
			.Field("fps", static_cast<double>(_displayedFps))
			.Field("frame_ms", static_cast<double>(_curFrameMs))
			.Field("avg_ms", static_cast<double>(_avgMs))
			.Field("low_1pct_ms", static_cast<double>(_onePctLowMs))
			.Field("refresh_hz", static_cast<double>(_refreshHz))
			.Field("multiplier", static_cast<std::int64_t>(_displayedFrameMultiplier))
			.Field("vram_used_mb", static_cast<std::int64_t>(_vramUsedBytes / (1024 * 1024)))
			.Field("vram_budget_mb", static_cast<std::int64_t>(_vramBudgetBytes / (1024 * 1024)));
	}

	void PerformanceOverlay::DrawOverlay()
	{
		if (!settings.enabled || settings.preset == static_cast<int>(Preset::Off))
			return;

		EnsureRefreshHz();
		TickFrame();

		const bool wantContent = settings.showFps || settings.showFrameTime ||
			settings.showGraph || settings.showVram || settings.showStats;
		if (!wantContent)
			return;

		ImGuiIO& io = ImGui::GetIO();
		const ImVec2 viewport = io.DisplaySize;

		// Snap by default; dragging is optional.
		ImGuiCond posCond = ImGuiCond_Always;
		ImVec2 pos{ 10.0f, 10.0f };
		ImVec2 pivot{ 0.0f, 0.0f };
		const float pad = 10.0f;
		switch (static_cast<Corner>(settings.corner)) {
			case Corner::TopLeft:     pos = { pad, pad };                                pivot = { 0.0f, 0.0f }; break;
			case Corner::TopRight:    pos = { viewport.x - pad, pad };                   pivot = { 1.0f, 0.0f }; break;
			case Corner::BottomLeft:  pos = { pad, viewport.y - pad };                   pivot = { 0.0f, 1.0f }; break;
			case Corner::BottomRight: pos = { viewport.x - pad, viewport.y - pad };      pivot = { 1.0f, 1.0f }; break;
		}
		if (settings.freeDrag) {
			pos = { settings.dragPosX, settings.dragPosY };
			pivot = { 0.0f, 0.0f };
			posCond = ImGuiCond_FirstUseEver;
		}

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings;
		if (!settings.freeDrag)
			flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		if (!settings.showBorder)
			flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground;
		else
			flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

		// Fixed width keeps optional rows aligned.
		const float kContentWidth = 440.0f * settings.fontScale;
		ImGui::SetNextWindowPos(pos, posCond, pivot);
		ImGui::SetNextWindowBgAlpha(settings.opacity);
		ImGui::SetNextWindowSizeConstraints(ImVec2(kContentWidth, 0.0f), ImVec2(kContentWidth, FLT_MAX));
		if (ImGui::Begin("##PerfOverlay", nullptr, flags)) {
			if (settings.freeDrag) {
				const ImVec2 cur = ImGui::GetWindowPos();
				if (cur.x != settings.dragPosX || cur.y != settings.dragPosY) {
					settings.dragPosX = cur.x;
					settings.dragPosY = cur.y;
				}
			}

			ImGui::SetWindowFontScale(settings.fontScale);

				const ImVec4 colGood  = ImVec4(0.20f, 1.00f, 0.20f, 1.00f);
			const ImVec4 colWarn  = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
			const ImVec4 colBad   = ImVec4(1.00f, 0.30f, 0.30f, 1.00f);
			const ImVec4 colHi    = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
			const ImVec4 colPostFg = ImVec4(0.20f, 0.85f, 1.00f, 0.90f);

			auto fpsColor = [&](float fps) -> ImVec4 {
				if (settings.highContrast) return colHi;
				if (fps >= settings.fpsGood) return colGood;
				if (fps >= settings.fpsWarn) return colWarn;
				return colBad;
			};

			auto drawReferenceLinesOverLastPlot = [](float ymin, float ymax, ImU32 color) {
				const ImVec2 itemMin = ImGui::GetItemRectMin();
				const ImVec2 itemMax = ImGui::GetItemRectMax();
				const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
				const ImVec2 plotMin(itemMin.x + framePadding.x, itemMin.y + framePadding.y);
				const ImVec2 plotMax(itemMax.x - framePadding.x, itemMax.y - framePadding.y);
				if (plotMax.x <= plotMin.x || plotMax.y <= plotMin.y)
					return;

				const float range = std::max(ymax - ymin, 0.001f);
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->PushClipRect(plotMin, plotMax, true);
				for (float fps : kFrameTimeReferenceFps) {
					const float ms = 1000.0f / fps;
					if (ms < ymin || ms > ymax)
						continue;
					const float t = std::clamp((ms - ymin) / range, 0.0f, 1.0f);
					const float y = plotMax.y - t * (plotMax.y - plotMin.y);
					drawList->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), color, 1.0f);
				}
				drawList->PopClipRect();
			};

			auto drawLineOverLastPlot = [](const float* values, int count, float ymin, float ymax, ImU32 color) {
				if (count < 2)
					return;

				const ImVec2 itemMin = ImGui::GetItemRectMin();
				const ImVec2 itemMax = ImGui::GetItemRectMax();
				const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
				const ImVec2 plotMin(itemMin.x + framePadding.x, itemMin.y + framePadding.y);
				const ImVec2 plotMax(itemMax.x - framePadding.x, itemMax.y - framePadding.y);
				if (plotMax.x <= plotMin.x || plotMax.y <= plotMin.y)
					return;

				const float range = std::max(ymax - ymin, 0.001f);
				const float width = plotMax.x - plotMin.x;
				const float height = plotMax.y - plotMin.y;
				const float xStep = width / static_cast<float>(count - 1);
				auto pointAt = [&](int idx) {
					const float t = std::clamp((values[idx] - ymin) / range, 0.0f, 1.0f);
					return ImVec2(plotMin.x + xStep * static_cast<float>(idx), plotMax.y - t * height);
				};

				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->PushClipRect(plotMin, plotMax, true);
				ImVec2 prev = pointAt(0);
				for (int i = 1; i < count; ++i) {
					const ImVec2 cur = pointAt(i);
					drawList->AddLine(prev, cur, color, 1.5f);
					prev = cur;
				}
				drawList->PopClipRect();
			};

			if (settings.showFps) {
				const bool fgActive = _displayedFrameMultiplier > 1;
				const auto fgType = FrameGeneration::GetSingleton()->activeFrameGenType;
				const bool fsr3Fallback = fgActive && fgType == FrameGeneration::FrameGenType::kFSR3;
				const float estimateFps = _displayedFps * static_cast<float>(_displayedFrameMultiplier);
				const float outputFps = (fgActive && _measuredDisplayedFps > 0.0f) ? _measuredDisplayedFps : estimateFps;
				ImGui::PushStyleColor(ImGuiCol_Text, fpsColor(outputFps));
				if (fgActive) {
					const char* fgLabel = "FG";
					switch (fgType) {
						case FrameGeneration::FrameGenType::kFSR3:   fgLabel = fsr3Fallback ? "FSR3 est" : "FSR3"; break;
						case FrameGeneration::FrameGenType::kDLSSG:  fgLabel = "DLSS-G"; break;
						case FrameGeneration::FrameGenType::kXeSSFG: fgLabel = "XeSS-FG"; break;
					}
					ImGui::Text("[%s] %.0f FPS  |  [Engine] %.0f FPS", fgLabel, outputFps, _displayedFps);
				} else {
					ImGui::Text("[Engine] %.0f FPS", _displayedFps);
				}
				ImGui::PopStyleColor();
				if (fgActive)
					ImGui::SetItemTooltip("Displayed FPS comes from backend-reported frame counts (DLSS-G slDLSSGGetState, XeSS-FG xefgSwapChainGetLastPresentStatus). FSR3 falls back to engine FPS times multiplier (labeled \"FSR3 est\") since safe per-frame counting would require hijacking presentCallback.");
			}
			if (settings.showFrameTime) {
				if (_displayedFrameMultiplier > 1)
					ImGui::Text("%.2f ms / displayed %.2f ms",
						_displayedFrameMs,
						_displayedFrameMs / static_cast<float>(_displayedFrameMultiplier));
				else
					ImGui::Text("%.2f ms", _displayedFrameMs);
			}

			if (settings.showGraph && _frameTimesCount > 1) {
				// PlotLines requires contiguous samples.
				static std::array<float, kHistoryCapacity> linear{};
				static std::array<float, kHistoryCapacity> linearPostFg{};
				for (int i = 0; i < _frameTimesCount; ++i) {
					int src = (_frameTimesHead - _frameTimesCount + i + settings.historySize) % settings.historySize;
					if (src < 0) src += settings.historySize;
					linear[i] = _frameTimesMs[src];
					linearPostFg[i] = _postFgFrameTimesMs[src];
				}
				const float refreshMs = 1000.0f / std::max(_refreshHz, 30.0f);
				const float maxReferenceMs = 1000.0f / kFrameTimeReferenceFps.front();
				// Limit outlier influence with average plus three sigma.
				const float ymaxTarget = std::max({
					refreshMs * 2.0f,
					_avgMs + 3.0f * _stddevMs,
					maxReferenceMs * 1.05f,
				});
				// Smooth scaling so hitches fade.
				if (_graphYMaxSmoothed <= 0.0f) {
					_graphYMaxSmoothed = ymaxTarget;
				} else {
					_graphYMaxSmoothed += (ymaxTarget - _graphYMaxSmoothed) * 0.25f;
				}
				const float ymax = _graphYMaxSmoothed;
				ImGui::TextUnformatted("Frame Time");
				if (settings.showEstimatedPostFGFrameTime) {
					ImGui::SameLine();
					const float lineH = ImGui::GetTextLineHeight();
					const float chipW = lineH * 0.6f;
					const ImVec2 chipPos = ImGui::GetCursorScreenPos();
					ImGui::GetWindowDrawList()->AddRectFilled(
						ImVec2(chipPos.x, chipPos.y + lineH * 0.25f),
						ImVec2(chipPos.x + chipW, chipPos.y + lineH * 0.75f),
						ImGui::GetColorU32(colPostFg));
					ImGui::Dummy(ImVec2(chipW + 4.0f, lineH));
					ImGui::SameLine();
					ImGui::TextColored(colPostFg, "post-FG");
					ImGui::SetItemTooltip(kPostFgFrameTimeTooltip);
				}
				const float graphHeight = std::clamp(settings.graphHeightPx, 40.0f, 160.0f) * settings.fontScale;
				ImGui::PlotLines("##frametimegraph", linear.data(), _frameTimesCount, 0,
					nullptr, 0.0f, ymax, ImVec2(-FLT_MIN, graphHeight));
				drawReferenceLinesOverLastPlot(0.0f, ymax, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, settings.highContrast ? 0.35f : 0.18f)));
				if (settings.showEstimatedPostFGFrameTime)
					drawLineOverLastPlot(linearPostFg.data(), _frameTimesCount, 0.0f, ymax, ImGui::GetColorU32(colPostFg));
			}

			if (settings.showStats) {
				ImGui::Text("avg     %5.2f ms", _avgMs);
				ImGui::Text("1%% low  %5.2f ms", _onePctLowMs);
				ImGui::Text("0.1%% low %5.2f ms", _pointOnePctLowMs);
			}

			if (settings.showVram) {
				if (!_adapter)
					_adapter = cs::Menu::Get().GetDXGIAdapter3();
				if (_adapter) {
					DXGI_QUERY_VIDEO_MEMORY_INFO info{};
					if (SUCCEEDED(_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
						_vramUsedBytes   = info.CurrentUsage;
						_vramBudgetBytes = info.Budget;
					}
				}
				if (_vramBudgetBytes > 0) {
					const float frac = static_cast<float>(static_cast<double>(_vramUsedBytes) / static_cast<double>(_vramBudgetBytes));
					char label[64];
					std::snprintf(label, sizeof(label), "%.1f / %.1f GB",
						_vramUsedBytes / (1024.0 * 1024.0 * 1024.0),
						_vramBudgetBytes / (1024.0 * 1024.0 * 1024.0));
					ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), label);
				}
			}
		}
		// End must pair with Begin even when hidden.
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
	}

	void PerformanceOverlay::RestoreDefaultSettings()
	{
		settings = Settings{};
		RefreshToggleHotkey();
		SaveSettings();
		cs::Menu::ShowToast("Performance Overlay reset to defaults", 2.5);
	}

	void PerformanceOverlay::DrawSettings()
	{
		ImGui::TextDisabled("%s toggles the overlay in-game.", _toggleHotkey.ToString().c_str());

		if (ImGui::Checkbox("Enabled", &settings.enabled))
			SaveSettings();

		ImGui::Separator();

		// Save sliders only on commit to avoid render-thread writes.
		auto sliderCommit = [] { return ImGui::IsItemDeactivatedAfterEdit(); };

		static const char* presetLabels[] = { "Off", "Minimal", "Standard", "Verbose" };
		int preset = std::clamp(settings.preset, 0, 3);
		if (ImGui::Combo("Preset", &preset, presetLabels, IM_ARRAYSIZE(presetLabels))) {
			ApplyPreset(static_cast<Preset>(preset));
			SaveSettings();
		}

		if (ImGui::CollapsingHeader("Sections")) {
			bool changed = false;
			changed |= ImGui::Checkbox("FPS", &settings.showFps);
			changed |= ImGui::Checkbox("Frame time (ms)", &settings.showFrameTime);
			changed |= ImGui::Checkbox("Frame time graph", &settings.showGraph);
			changed |= ImGui::Checkbox("Estimated post-FG frame-time series", &settings.showEstimatedPostFGFrameTime);
			ImGui::SetItemTooltip(kPostFgFrameTimeTooltip);
			changed |= ImGui::Checkbox("VRAM", &settings.showVram);
			changed |= ImGui::Checkbox("Frame stats (avg / 1%% low / 0.1%% low)", &settings.showStats);
			if (changed) SaveSettings();
		}

		if (ImGui::CollapsingHeader("Position")) {
			static const char* cornerLabels[] = { "Top-left", "Top-right", "Bottom-left", "Bottom-right" };
			int corner = std::clamp(settings.corner, 0, 3);
			if (ImGui::Combo("Corner", &corner, cornerLabels, IM_ARRAYSIZE(cornerLabels))) {
				settings.corner = corner;
				SaveSettings();
			}
			if (ImGui::Checkbox("Free-drag (override corner snap)", &settings.freeDrag))
				SaveSettings();
		}

		if (ImGui::CollapsingHeader("Style")) {
			bool changed = false;
			ImGui::SliderFloat("Background opacity", &settings.opacity, 0.0f, 1.0f, "%.2f");
			if (sliderCommit()) {
				settings.opacity = std::clamp(settings.opacity, 0.0f, 1.0f);
				changed = true;
			}
			if (ImGui::Checkbox("Show border", &settings.showBorder)) changed = true;
			ImGui::SliderFloat("Font scale", &settings.fontScale, 0.5f, 3.0f, "%.2fx");
			if (sliderCommit()) {
				settings.fontScale = std::clamp(settings.fontScale, 0.5f, 3.0f);
				changed = true;
			}
			if (ImGui::Checkbox("High contrast (force white text)", &settings.highContrast)) changed = true;
			if (changed) SaveSettings();
		}

		if (ImGui::CollapsingHeader("Color thresholds")) {
			if (ImGui::Checkbox("Auto-seed from monitor refresh rate", &settings.autoThresholds)) {
				if (settings.autoThresholds) {
					_refreshKnown = false;
					EnsureRefreshHz();
				}
				SaveSettings();
			}
			ImGui::TextDisabled("Detected refresh: %.0f Hz", _refreshHz);
			ImGui::BeginDisabled(settings.autoThresholds);
			bool committed = false;
			ImGui::SliderFloat("Good (>= FPS)", &settings.fpsGood, 30.0f, 360.0f, "%.0f");
			if (sliderCommit()) committed = true;
			ImGui::SliderFloat("Warn (>= FPS)", &settings.fpsWarn, 15.0f, 240.0f, "%.0f");
			if (sliderCommit()) committed = true;
			ImGui::EndDisabled();
			if (committed) SaveSettings();
		}

		if (ImGui::CollapsingHeader("Tracking")) {
			ImGui::SliderFloat("Update interval (s)", &settings.updateInterval, 0.05f, 2.0f, "%.2f");
			ImGui::SetItemTooltip("How often the displayed FPS/frametime number refreshes. The history graph updates every frame.");
			const bool intervalCommitted = sliderCommit();
			ImGui::SliderInt("History size (frames)", &settings.historySize, 30, kHistoryCapacity);
			const bool historyCommitted = sliderCommit();
			ImGui::SliderFloat("Graph height (px)", &settings.graphHeightPx, 40.0f, 160.0f, "%.0f");
			const bool graphHeightCommitted = sliderCommit();
			if (intervalCommitted || historyCommitted || graphHeightCommitted) {
				settings.updateInterval = std::clamp(settings.updateInterval, 0.05f, 5.0f);
				settings.historySize    = std::clamp(settings.historySize, 30, kHistoryCapacity);
				settings.graphHeightPx  = std::clamp(settings.graphHeightPx, 40.0f, 160.0f);
				if (historyCommitted) {
					// History-size changes invalidate existing samples.
					_frameTimesHead = 0;
					_frameTimesCount = 0;
				}
				SaveSettings();
			}
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(PerformanceOverlay::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
