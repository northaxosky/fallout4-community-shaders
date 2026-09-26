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
#include "Settings/SettingsPersistence.h"
#include "Menu/SettingsEdit.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.performanceoverlay"); }

	constexpr std::array<float, 3> kFrameTimeReferenceFps{ 30.0f, 60.0f, 120.0f };


	PerformanceOverlay* PerformanceOverlay::GetSingleton()
	{
		static PerformanceOverlay instance;
		return &instance;
	}

	bool PerformanceOverlay::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = settings;
		if (!settings::Parse(performance_overlay::kSchema, a_config, candidate, a_error)) {
			return false;
		}

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

	bool PerformanceOverlay::SaveSettings()
	{
		return settings::SaveDelta(performance_overlay::kSchema, GetConfigKey(), settings, *L);
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

		const dmui::ui::Vec4 good{ 0.20f, 1.00f, 0.20f, 1.00f };
		const dmui::ui::Vec4 warning{ 1.00f, 0.85f, 0.20f, 1.00f };
		const dmui::ui::Vec4 bad{ 1.00f, 0.30f, 0.30f, 1.00f };
		const dmui::ui::Vec4 white{ 1.00f, 1.00f, 1.00f, 1.00f };
		const auto color = settings.highContrast ?
			white :
			(_displayedFps >= settings.fpsGood ?
				good :
				(_displayedFps >= settings.fpsWarn ? warning : bad));

		if (settings.showFps) {
			dmui::ui::PushStyleColor(dmui::ui::Color::kText, color);
			dmui::ui::Text("[Engine] %.0f FPS", _displayedFps);
			dmui::ui::PopStyleColor();
		}
		if (settings.showFrameTime)
			dmui::ui::Text("%.2f ms", _displayedFrameMs);

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
			dmui::ui::Text("avg     %5.2f ms", _avgMs);
			dmui::ui::Text("1%% low  %5.2f ms", _onePctLowMs);
			dmui::ui::Text("0.1%% low %5.2f ms", _pointOnePctLowMs);
		}
		if (settings.showVram && _vramBudgetBytes > 0) {
			dmui::ui::Text(
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
		settings::SettingsEdit edit{ *this };
		dmui::ui::TextDisabled(
			"The host owns the overlay hotkey. Suggested default: %s.",
			settings.toggleHotkey.c_str());

		edit.Discrete(dmui::ui::Checkbox("Enabled", &settings.enabled));

		dmui::ui::Separator();

		static const std::array presetOptions{
			dmui::ChoiceOption<int>{ 0, "Off", "off" },
			dmui::ChoiceOption<int>{ 1, "Minimal", "minimal" },
			dmui::ChoiceOption<int>{ 2, "Standard", "standard" },
			dmui::ChoiceOption<int>{ 3, "Verbose", "verbose" }
		};
		const auto preset = dmui::DrawChoice<int>(
			"performance-overlay-preset",
			settings.preset,
			std::span<const dmui::ChoiceOption<int>>{ presetOptions },
			"Unavailable",
			"Preset");
		if (edit.Discrete(preset.changed)) {
			ApplyPreset(static_cast<Preset>(*preset.selected));
		}

		if (dmui::ui::CollapsingHeader("Sections")) {
			edit.Discrete(dmui::ui::Checkbox("FPS", &settings.showFps));
			edit.Discrete(dmui::ui::Checkbox("Frame time (ms)", &settings.showFrameTime));
			edit.Discrete(dmui::ui::Checkbox("Frame time graph", &settings.showGraph));
			edit.Discrete(dmui::ui::Checkbox("VRAM", &settings.showVram));
			edit.Discrete(dmui::ui::Checkbox("Frame stats (avg / 1%% low / 0.1%% low)", &settings.showStats));
		}

		if (dmui::ui::CollapsingHeader("Position")) {
			static const std::array cornerOptions{
				dmui::ChoiceOption<int>{ 0, "Top-left", "top-left" },
				dmui::ChoiceOption<int>{ 1, "Top-right", "top-right" },
				dmui::ChoiceOption<int>{ 2, "Bottom-left", "bottom-left" },
				dmui::ChoiceOption<int>{ 3, "Bottom-right", "bottom-right" }
			};
			const auto corner = dmui::DrawChoice<int>(
				"performance-overlay-corner",
				settings.corner,
				std::span<const dmui::ChoiceOption<int>>{ cornerOptions },
				"Unavailable",
				"Corner");
			if (edit.Discrete(corner.changed)) {
				settings.corner = *corner.selected;
			}
			edit.Discrete(dmui::ui::Checkbox("Free-drag (override corner snap)", &settings.freeDrag));
		}

		if (dmui::ui::CollapsingHeader("Style")) {
			const auto opacityRange = performance_overlay::kSchema.EditRange(&Settings::opacity);
			edit.Continuous(dmui::ui::SliderScalar(
				"Background opacity",
				&settings.opacity,
				&opacityRange.min,
				&opacityRange.max,
				"%.2f"));
			if (dmui::ui::IsItemDeactivatedAfterEdit()) {
				settings.opacity = std::clamp(settings.opacity, opacityRange.min, opacityRange.max);
			}
			edit.Discrete(dmui::ui::Checkbox("Show border", &settings.showBorder));
			const auto fontScaleRange = performance_overlay::kSchema.EditRange(&Settings::fontScale);
			edit.Continuous(dmui::ui::SliderScalar(
				"Font scale",
				&settings.fontScale,
				&fontScaleRange.min,
				&fontScaleRange.max,
				"%.2fx"));
			if (dmui::ui::IsItemDeactivatedAfterEdit()) {
				settings.fontScale = std::clamp(settings.fontScale, fontScaleRange.min, fontScaleRange.max);
			}
			edit.Discrete(dmui::ui::Checkbox("High contrast (force white text)", &settings.highContrast));
		}

		if (dmui::ui::CollapsingHeader("Color thresholds")) {
			if (edit.Discrete(dmui::ui::Checkbox("Auto-seed from monitor refresh rate", &settings.autoThresholds))) {
				if (settings.autoThresholds) {
					_refreshKnown = false;
					EnsureRefreshHz();
				}
			}
			dmui::ui::TextDisabled("Detected refresh: %.0f Hz", _refreshHz);
			dmui::ui::BeginDisabled(settings.autoThresholds);
			const auto goodFpsRange = performance_overlay::kSchema.EditRange(&Settings::fpsGood);
			edit.Continuous(dmui::ui::SliderScalar(
				"Good (>= FPS)",
				&settings.fpsGood,
				&goodFpsRange.min,
				&goodFpsRange.max,
				"%.0f"));
			const auto warnFpsRange = performance_overlay::kSchema.EditRange(&Settings::fpsWarn);
			edit.Continuous(dmui::ui::SliderScalar(
				"Warn (>= FPS)",
				&settings.fpsWarn,
				&warnFpsRange.min,
				&warnFpsRange.max,
				"%.0f"));
			dmui::ui::EndDisabled();
		}

		if (dmui::ui::CollapsingHeader("Tracking")) {
			const auto updateIntervalRange = performance_overlay::kSchema.EditRange(&Settings::updateInterval);
			edit.Continuous(dmui::ui::SliderScalar(
				"Update interval (s)",
				&settings.updateInterval,
				&updateIntervalRange.min,
				&updateIntervalRange.max,
				"%.2f"));
			const bool intervalCommitted = dmui::ui::IsItemDeactivatedAfterEdit();
			if (const dmui::TooltipScope tooltip{ dmui::ui::HoveredFlags::kNone };
				tooltip.Visible()) {
				dmui::ui::Text(
					"%s",
					"How often the displayed FPS/frametime number refreshes. "
					"The history graph updates every frame.");
			}
			const auto historySizeRange = performance_overlay::kSchema.EditRange(&Settings::historySize);
			edit.Continuous(dmui::ui::SliderScalar(
				"History size (frames)",
				&settings.historySize,
				&historySizeRange.min,
				&historySizeRange.max));
			const bool historyCommitted = dmui::ui::IsItemDeactivatedAfterEdit();
			const auto graphHeightRange = performance_overlay::kSchema.EditRange(&Settings::graphHeightPx);
			edit.Continuous(dmui::ui::SliderScalar(
				"Graph height (px)",
				&settings.graphHeightPx,
				&graphHeightRange.min,
				&graphHeightRange.max,
				"%.0f"));
			const bool graphHeightCommitted = dmui::ui::IsItemDeactivatedAfterEdit();
			if (intervalCommitted || historyCommitted || graphHeightCommitted) {
				settings.updateInterval = std::clamp(settings.updateInterval, 0.05f, 5.0f);
				settings.historySize    = std::clamp(settings.historySize, 30, kHistoryCapacity);
				settings.graphHeightPx  = std::clamp(settings.graphHeightPx, 40.0f, 160.0f);
				if (historyCommitted) {
					// History-size changes invalidate existing samples.
					_frameTimesHead = 0;
					_frameTimesCount = 0;
				}
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
