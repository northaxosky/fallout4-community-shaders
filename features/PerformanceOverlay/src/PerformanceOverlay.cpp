#include "PerformanceOverlay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>
#include <Windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Env.h"
#include "Log.h"
#include "Menu.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.perfoverlay"); }

	struct PerformanceOverlay::AdapterCache
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
	};

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\PerformanceOverlay.toml";

	PerformanceOverlay* PerformanceOverlay::GetSingleton()
	{
		static PerformanceOverlay instance;
		return &instance;
	}

	void PerformanceOverlay::Load()
	{
		LoadSettings();

		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		_qpcFreq = static_cast<double>(freq.QuadPart);

		L->info("Loaded: enabled={} preset={} corner={}",
			settings.enabled, settings.preset, settings.corner);
	}

	void PerformanceOverlay::LoadSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			return;
		}

		settings.enabled        = table["settings"]["enabled"].value_or(settings.enabled);
		settings.preset         = std::clamp(static_cast<int>(table["settings"]["preset"].value_or<int64_t>(settings.preset)), 0, 3);

		settings.showFps        = table["settings"]["show_fps"].value_or(settings.showFps);
		settings.showFrameTime  = table["settings"]["show_frame_time"].value_or(settings.showFrameTime);
		settings.showGraph      = table["settings"]["show_graph"].value_or(settings.showGraph);
		settings.showVram       = table["settings"]["show_vram"].value_or(settings.showVram);
		settings.showStats      = table["settings"]["show_stats"].value_or(settings.showStats);

		settings.corner         = std::clamp(static_cast<int>(table["settings"]["corner"].value_or<int64_t>(settings.corner)), 0, 3);
		settings.freeDrag       = table["settings"]["free_drag"].value_or(settings.freeDrag);
		settings.dragPosX       = static_cast<float>(table["settings"]["drag_pos_x"].value_or(static_cast<double>(settings.dragPosX)));
		settings.dragPosY       = static_cast<float>(table["settings"]["drag_pos_y"].value_or(static_cast<double>(settings.dragPosY)));

		settings.opacity        = std::clamp(static_cast<float>(table["settings"]["opacity"].value_or(static_cast<double>(settings.opacity))), 0.0f, 1.0f);
		settings.showBorder     = table["settings"]["show_border"].value_or(settings.showBorder);
		settings.fontScale      = std::clamp(static_cast<float>(table["settings"]["font_scale"].value_or(static_cast<double>(settings.fontScale))), 0.5f, 3.0f);
		settings.highContrast   = table["settings"]["high_contrast"].value_or(settings.highContrast);

		settings.autoThresholds = table["settings"]["auto_thresholds"].value_or(settings.autoThresholds);
		settings.fpsGood        = static_cast<float>(table["settings"]["fps_good"].value_or(static_cast<double>(settings.fpsGood)));
		settings.fpsWarn        = static_cast<float>(table["settings"]["fps_warn"].value_or(static_cast<double>(settings.fpsWarn)));

		settings.updateInterval = std::clamp(static_cast<float>(table["settings"]["update_interval"].value_or(static_cast<double>(settings.updateInterval))), 0.05f, 5.0f);
		settings.historySize    = std::clamp(static_cast<int>(table["settings"]["history_size"].value_or<int64_t>(settings.historySize)), 30, kHistoryCapacity);
	}

	void PerformanceOverlay::SaveSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		auto& settingsTable = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
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

		std::ofstream out(kConfigPath);
		if (out) {
			out << table;
		}
	}

	void PerformanceOverlay::ApplyPreset(Preset preset)
	{
		settings.preset = static_cast<int>(preset);
		switch (preset) {
			case Preset::Off:
				settings.showFps = settings.showFrameTime = settings.showGraph =
					settings.showVram = settings.showStats = false;
				break;
			case Preset::Minimal:
				settings.showFps = true;
				settings.showFrameTime = settings.showGraph = settings.showVram = settings.showStats = false;
				break;
			case Preset::Standard:
				settings.showFps = settings.showFrameTime = settings.showGraph = true;
				settings.showVram = settings.showStats = false;
				break;
			case Preset::Verbose:
				settings.showFps = settings.showFrameTime = settings.showGraph =
					settings.showVram = settings.showStats = true;
				break;
		}
	}

	void PerformanceOverlay::EnsureRefreshHz()
	{
		if (_refreshKnown)
			return;
		// Pull current monitor's refresh rate via EnumDisplaySettings; cheap and accurate enough.
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
			// Discard implausibly large deltas (overlay was hidden, app paused, etc.) so the
			// stats and graph don't get poisoned by a single multi-second sample.
			if (dtMs > 0.0f && dtMs < 1000.0f) {
				_curFrameMs = dtMs;
				_frameTimesMs[_frameTimesHead] = dtMs;
				_frameTimesHead = (_frameTimesHead + 1) % settings.historySize;
				if (_frameTimesCount < settings.historySize)
					_frameTimesCount++;
			}
		}
		_lastFrameQpc = nowSec;

		// Recompute the displayed FPS / frame time at the user-controlled cadence so the readout
		// doesn't flicker per frame. Stats are recomputed on the same cadence; cheap given history<=600.
		if (nowSec - _lastDisplayUpdate >= settings.updateInterval) {
			_displayedFrameMs = _curFrameMs;
			_displayedFps = _curFrameMs > 0.0f ? 1000.0f / _curFrameMs : 0.0f;
			_displayedFrameMultiplier = std::max(1, cs::env::GetDisplayedFrameMultiplier());
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

		// Lows = high frame-time tail (slow frames).
		const auto idx99   = static_cast<size_t>(sorted.size() * 99 / 100);
		const auto idx999  = static_cast<size_t>(sorted.size() * 999 / 1000);
		_onePctLowMs       = sorted[std::min(idx99,  sorted.size() - 1)];
		_pointOnePctLowMs  = sorted[std::min(idx999, sorted.size() - 1)];
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

		// Position: 4-corner snap by default, free-drag opt-in.
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

		// Pinned width keeps rows uniform regardless of which sections are enabled.
		const float kContentWidth = 280.0f * settings.fontScale;
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

			// Color helpers; high-contrast forces white regardless of FPS coding.
			const ImVec4 colGood = ImVec4(0.20f, 1.00f, 0.20f, 1.00f);
			const ImVec4 colWarn = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
			const ImVec4 colBad  = ImVec4(1.00f, 0.30f, 0.30f, 1.00f);
			const ImVec4 colHi   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

			auto fpsColor = [&](float fps) -> ImVec4 {
				if (settings.highContrast) return colHi;
				if (fps >= settings.fpsGood) return colGood;
				if (fps >= settings.fpsWarn) return colWarn;
				return colBad;
			};

			if (settings.showFps) {
				const bool fgActive = _displayedFrameMultiplier > 1;
				const float outputFps = _displayedFps * static_cast<float>(_displayedFrameMultiplier);
				ImGui::PushStyleColor(ImGuiCol_Text, fpsColor(outputFps));
				if (fgActive)
					ImGui::Text("%.0f FPS  (engine %.0f x%d)", outputFps, _displayedFps, _displayedFrameMultiplier);
				else
					ImGui::Text("%.0f FPS", _displayedFps);
				ImGui::PopStyleColor();
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
				// Re-sort window for ImGui::PlotLines requires linear array; reorder ringbuf into a temp.
				static std::array<float, kHistoryCapacity> linear{};
				for (int i = 0; i < _frameTimesCount; ++i) {
					int src = (_frameTimesHead - _frameTimesCount + i + settings.historySize) % settings.historySize;
					if (src < 0) src += settings.historySize;
					linear[i] = _frameTimesMs[src];
				}
				const float refreshMs = 1000.0f / std::max(_refreshHz, 30.0f);
				const float ymax = std::max(refreshMs * 2.0f, _displayedFrameMs * 1.25f);
				ImGui::PlotLines("##frametimegraph", linear.data(), _frameTimesCount, 0,
					nullptr, 0.0f, ymax, ImVec2(-FLT_MIN, 40.0f * settings.fontScale));
			}

			if (settings.showStats) {
				// Stacked because the single-line form would exceed the pinned width.
				ImGui::Text("avg     %5.2f ms", _avgMs);
				ImGui::Text("1%% low  %5.2f ms", _onePctLowMs);
				ImGui::Text("0.1%% low %5.2f ms", _pointOnePctLowMs);
			}

			if (settings.showVram) {
				if (!_adapter) {
					_adapter = std::make_unique<AdapterCache>();
					Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
					if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
						Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
						if (SUCCEEDED(factory->EnumAdapters1(0, &adapter1)))
							adapter1.As(&_adapter->adapter);
					}
				}
				if (_adapter && _adapter->adapter) {
					DXGI_QUERY_VIDEO_MEMORY_INFO info{};
					if (SUCCEEDED(_adapter->adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
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
		// Reset font scale outside the if(Begin()) block so a collapsed/clipped window
		// doesn't leave the global scale stuck. End() must always pair with Begin().
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
	}

	void PerformanceOverlay::DrawSettings()
	{
		if (ImGui::Button("Reset to defaults")) {
			settings = Settings{};
			SaveSettings();
			cs::Menu::ShowToast("Performance Overlay reset to defaults", 2.5);
		}
		ImGui::SetItemTooltip("Reverts the overlay to plugin defaults (preset, sections, position, colors) and saves.");
		ImGui::Separator();

		ImGui::TextDisabled("Shift+F11 toggles the overlay in-game.");

		if (ImGui::Checkbox("Enabled", &settings.enabled))
			SaveSettings();

		ImGui::Separator();

		// Sliders save on commit (mouse-release / keyboard-deactivate) rather than per-tick;
		// otherwise a slider drag triggers a full TOML rewrite on the render thread every frame.
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
			if (intervalCommitted || historyCommitted) {
				settings.updateInterval = std::clamp(settings.updateInterval, 0.05f, 5.0f);
				settings.historySize    = std::clamp(settings.historySize, 30, kHistoryCapacity);
				if (historyCommitted) {
					// History buffer changes shape; reset rather than reinterpret stale data.
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
