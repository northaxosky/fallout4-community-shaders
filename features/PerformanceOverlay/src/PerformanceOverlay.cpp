#include "PerformanceOverlay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <DearModdingUI/Client.h>
#include <toml++/toml.hpp>
#include <Windows.h>
#include <dxgi1_4.h>

#include "Log.h"
#include "Host/HostClient.h"
#include "Menu/Menu.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.performanceoverlay"); }

	constexpr std::array<float, 3> kFrameTimeReferenceFps{ 30.0f, 60.0f, 120.0f };

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

		_toggleHotkeyConfigured = feature_config::HasUserFeatureSetting(
			GetConfigKey(), "toggle_hotkey");
		settings = candidate;
		return true;
	}

	void PerformanceOverlay::Load()
	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		_qpcFreq = static_cast<double>(freq.QuadPart);

		L->info("Loaded: enabled={} preset={} corner={} toggle_hotkey={}",
			settings.enabled, settings.preset, settings.corner, settings.toggleHotkey);
	}

	void PerformanceOverlay::SaveSettings()
	{
		toml::table settingsTable;
		settingsTable.insert_or_assign("enabled", settings.enabled);
		settingsTable.insert_or_assign("preset", static_cast<int64_t>(settings.preset));
		settingsTable.insert_or_assign("show_fps", settings.showFps);
		settingsTable.insert_or_assign("show_frame_time", settings.showFrameTime);
		settingsTable.insert_or_assign("show_graph", settings.showGraph);
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
		if (_toggleHotkeyConfigured)
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
				settings.showVram = false;
				settings.showStats = false;
				break;
			case Preset::Minimal:
				settings.showFps = true;
				settings.showFrameTime = false;
				settings.showGraph = false;
				settings.showVram = false;
				settings.showStats = false;
				break;
			case Preset::Standard:
				settings.showFps = true;
				settings.showFrameTime = true;
				settings.showGraph = true;
				settings.showVram = false;
				settings.showStats = false;
				break;
			case Preset::Verbose:
				settings.showFps = true;
				settings.showFrameTime = true;
				settings.showGraph = true;
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

		if (_lastFrameQpc > 0.0) {
			const float dtMs = static_cast<float>((nowSec - _lastFrameQpc) * 1000.0);
			// Ignore pause-length samples.
			if (dtMs > 0.0f && dtMs < 1000.0f) {
				_curFrameMs = dtMs;
				_frameTimesMs[_frameTimesHead] = dtMs;
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
			.Field("vram_used_mb", static_cast<std::int64_t>(_vramUsedBytes / (1024 * 1024)))
			.Field("vram_budget_mb", static_cast<std::int64_t>(_vramBudgetBytes / (1024 * 1024)));
	}

	void PerformanceOverlay::DrawOverlay()
	{
		if (!settings.enabled || settings.preset == static_cast<int>(Preset::Off))
			return;

		EnsureRefreshHz();

		const bool wantContent = settings.showFps || settings.showFrameTime ||
			settings.showGraph || settings.showVram || settings.showStats;
		if (!wantContent)
			return;

		const ImVec4 good{ 0.20f, 1.00f, 0.20f, 1.00f };
		const ImVec4 warning{ 1.00f, 0.85f, 0.20f, 1.00f };
		const ImVec4 bad{ 1.00f, 0.30f, 0.30f, 1.00f };
		const ImVec4 white{ 1.00f, 1.00f, 1.00f, 1.00f };
		const auto color = settings.highContrast ?
			white :
			(_displayedFps >= settings.fpsGood ?
				good :
				(_displayedFps >= settings.fpsWarn ? warning : bad));

		if (settings.showFps) {
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::Text("[Engine] %.0f FPS", _displayedFps);
			ImGui::PopStyleColor();
		}
		if (settings.showFrameTime)
			ImGui::Text("%.2f ms", _displayedFrameMs);

		if (settings.showGraph && _frameTimesCount > 1) {
			static std::array<float, kHistoryCapacity> linear{};
			for (int i = 0; i < _frameTimesCount; ++i) {
				int source =
					(_frameTimesHead - _frameTimesCount + i + settings.historySize) %
					settings.historySize;
				if (source < 0)
					source += settings.historySize;
				linear[i] = _frameTimesMs[source];
			}
			const float refreshMs = 1000.0f / std::max(_refreshHz, 30.0f);
			const float slowestReferenceMs = 1000.0f / kFrameTimeReferenceFps.front();
			const float target = std::max({
				refreshMs * 2.0f,
				_avgMs + 3.0f * _stddevMs,
				slowestReferenceMs * 1.05f });
			if (_graphYMaxSmoothed <= 0.0f)
				_graphYMaxSmoothed = target;
			else
				_graphYMaxSmoothed += (target - _graphYMaxSmoothed) * 0.25f;

			std::array<DMUI_PlotReferenceLine, 3> references{};
			for (std::size_t index = 0; index < references.size(); ++index) {
				references[index] = {
					1000.0f / kFrameTimeReferenceFps[index],
					{ 1.0f, 1.0f, 1.0f, settings.highContrast ? 0.35f : 0.18f }
				};
			}
			const DMUI_AnnotatedPlotDescriptor plot{
				DMUI_ANNOTATED_PLOT_DESCRIPTOR_0_1_SIZE,
				linear.data(),
				static_cast<std::uint32_t>(_frameTimesCount),
				0u,
				0.0f,
				_graphYMaxSmoothed,
				{ 440.0f, std::clamp(settings.graphHeightPx, 40.0f, 160.0f) },
				"Frame Time",
				references.data(),
				static_cast<std::uint32_t>(references.size())
			};
			(void)host::HostClient::Get().DrawAnnotatedPlot(
				"performance-frame-time",
				plot);
		}
		if (settings.showStats) {
			ImGui::Text("avg     %5.2f ms", _avgMs);
			ImGui::Text("1%% low  %5.2f ms", _onePctLowMs);
			ImGui::Text("0.1%% low %5.2f ms", _pointOnePctLowMs);
		}
		if (settings.showVram && _vramBudgetBytes > 0) {
			ImGui::Text(
				"VRAM %.1f / %.1f GB",
				_vramUsedBytes / (1024.0 * 1024.0 * 1024.0),
				_vramBudgetBytes / (1024.0 * 1024.0 * 1024.0));
		}
	}

	void PerformanceOverlay::TickHostFrame(
		std::uint64_t a_vramUsedBytes,
		std::uint64_t a_vramBudgetBytes)
	{
		EnsureRefreshHz();
		TickFrame();
		_vramUsedBytes = a_vramUsedBytes;
		_vramBudgetBytes = a_vramBudgetBytes;
	}

	DMUI_ManagedOverlayOptions PerformanceOverlay::ManagedOverlayOptions() const noexcept
	{
		const auto anchor = settings.freeDrag ?
			DMUI_OVERLAY_ANCHOR_FREE :
			static_cast<DMUI_OverlayAnchor>(std::clamp(settings.corner, 0, 3));
		const DMUI_Vec2 offset = settings.freeDrag ?
			DMUI_Vec2{ settings.dragPosX, settings.dragPosY } :
			DMUI_Vec2{ 10.0f, 10.0f };
		return {
			DMUI_MANAGED_OVERLAY_OPTIONS_0_1_SIZE,
			anchor,
			offset,
			{ 440.0f, 0.0f },
			{ 440.0f, 10000.0f },
			settings.opacity,
			settings.fontScale,
			settings.showBorder ? 1u : 0u,
			settings.showBorder ? 1u : 0u,
			settings.freeDrag ? 1u : 0u,
			0u
		};
	}

	void PerformanceOverlay::CommitOverlayPlacement(
		const DMUI_ManagedOverlayPlacement& a_placement)
	{
		if (!settings.freeDrag || a_placement.anchor != DMUI_OVERLAY_ANCHOR_FREE)
			return;
		if (settings.dragPosX == a_placement.position.x &&
			settings.dragPosY == a_placement.position.y)
			return;
		settings.dragPosX = a_placement.position.x;
		settings.dragPosY = a_placement.position.y;
		SaveSettings();
	}

	void PerformanceOverlay::RestoreDefaultSettings()
	{
		settings = Settings{};
		SaveSettings();
		cs::Menu::ShowToast("Performance Overlay reset to defaults", 2.5);
	}

	void PerformanceOverlay::DrawSettings()
	{
		ImGui::TextDisabled(
			"The host owns the overlay hotkey. Suggested default: %s.",
			settings.toggleHotkey.c_str());
		const auto drawChoice = [](const char* a_label,
								 int& a_value,
								 const char* const* a_labels,
								 std::size_t a_count) {
			const auto selected =
				a_value >= 0 && static_cast<std::size_t>(a_value) < a_count ?
				a_value :
				0;
			bool choiceChanged = false;
			if (ImGui::BeginCombo(a_label, a_labels[selected])) {
				for (std::size_t index = 0; index < a_count; ++index) {
					if (ImGui::Selectable(
							a_labels[index],
							static_cast<int>(index) == selected)) {
						a_value = static_cast<int>(index);
						choiceChanged = true;
					}
					if (static_cast<int>(index) == selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			return choiceChanged;
		};

		if (ImGui::Checkbox("Enabled", &settings.enabled))
			SaveSettings();

		ImGui::Separator();

		// Save sliders only on commit to avoid render-thread writes.
		auto sliderCommit = [] { return ImGui::IsItemDeactivatedAfterEdit(); };

		static const char* presetLabels[] = { "Off", "Minimal", "Standard", "Verbose" };
		int preset = std::clamp(settings.preset, 0, 3);
		if (drawChoice("Preset", preset, presetLabels, std::size(presetLabels))) {
			ApplyPreset(static_cast<Preset>(preset));
			SaveSettings();
		}

		if (ImGui::CollapsingHeader("Sections")) {
			bool changed = false;
			changed |= ImGui::Checkbox("FPS", &settings.showFps);
			changed |= ImGui::Checkbox("Frame time (ms)", &settings.showFrameTime);
			changed |= ImGui::Checkbox("Frame time graph", &settings.showGraph);
			changed |= ImGui::Checkbox("VRAM", &settings.showVram);
			changed |= ImGui::Checkbox("Frame stats (avg / 1%% low / 0.1%% low)", &settings.showStats);
			if (changed) SaveSettings();
		}

		if (ImGui::CollapsingHeader("Position")) {
			static const char* cornerLabels[] = { "Top-left", "Top-right", "Bottom-left", "Bottom-right" };
			int corner = std::clamp(settings.corner, 0, 3);
			if (drawChoice("Corner", corner, cornerLabels, std::size(cornerLabels))) {
				settings.corner = corner;
				SaveSettings();
			}
			if (ImGui::Checkbox("Free-drag (override corner snap)", &settings.freeDrag))
				SaveSettings();
		}

		if (ImGui::CollapsingHeader("Style")) {
			bool changed = false;
			const float opacityMin = 0.0f;
			const float opacityMax = 1.0f;
			(void)ImGui::SliderScalar(
				"Background opacity",
				ImGuiDataType_Float,
				&settings.opacity,
				&opacityMin,
				&opacityMax,
				"%.2f");
			if (sliderCommit()) {
				settings.opacity = std::clamp(settings.opacity, 0.0f, 1.0f);
				changed = true;
			}
			if (ImGui::Checkbox("Show border", &settings.showBorder)) changed = true;
			const float fontScaleMin = 0.5f;
			const float fontScaleMax = 3.0f;
			(void)ImGui::SliderScalar(
				"Font scale",
				ImGuiDataType_Float,
				&settings.fontScale,
				&fontScaleMin,
				&fontScaleMax,
				"%.2fx");
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
			const float goodFpsMin = 30.0f;
			const float goodFpsMax = 360.0f;
			(void)ImGui::SliderScalar(
				"Good (>= FPS)",
				ImGuiDataType_Float,
				&settings.fpsGood,
				&goodFpsMin,
				&goodFpsMax,
				"%.0f");
			if (sliderCommit()) committed = true;
			const float warnFpsMin = 15.0f;
			const float warnFpsMax = 240.0f;
			(void)ImGui::SliderScalar(
				"Warn (>= FPS)",
				ImGuiDataType_Float,
				&settings.fpsWarn,
				&warnFpsMin,
				&warnFpsMax,
				"%.0f");
			if (sliderCommit()) committed = true;
			ImGui::EndDisabled();
			if (committed) SaveSettings();
		}

		if (ImGui::CollapsingHeader("Tracking")) {
			const float updateIntervalMin = 0.05f;
			const float updateIntervalMax = 2.0f;
			(void)ImGui::SliderScalar(
				"Update interval (s)",
				ImGuiDataType_Float,
				&settings.updateInterval,
				&updateIntervalMin,
				&updateIntervalMax,
				"%.2f");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(
					"%s",
					"How often the displayed FPS/frametime number refreshes. "
					"The history graph updates every frame.");
			}
			const bool intervalCommitted = sliderCommit();
			const int historySizeMin = 30;
			const int historySizeMax = kHistoryCapacity;
			(void)ImGui::SliderScalar(
				"History size (frames)",
				ImGuiDataType_S32,
				&settings.historySize,
				&historySizeMin,
				&historySizeMax);
			const bool historyCommitted = sliderCommit();
			const float graphHeightMin = 40.0f;
			const float graphHeightMax = 160.0f;
			(void)ImGui::SliderScalar(
				"Graph height (px)",
				ImGuiDataType_Float,
				&settings.graphHeightPx,
				&graphHeightMin,
				&graphHeightMax,
				"%.0f");
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
