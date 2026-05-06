#include "PerformanceOverlay.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <imgui.h>
#include <Windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Log.h"
#include "SimpleIni.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.perfoverlay"); }

	struct PerformanceOverlay::AdapterCache
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
	};

	constexpr const char* kIniPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\PerformanceOverlay.ini";

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
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		settings.enabled        = ini.GetBoolValue("Settings", "bEnabled", settings.enabled);
		settings.preset         = static_cast<int>(ini.GetLongValue("Settings", "iPreset", settings.preset));

		settings.showFps        = ini.GetBoolValue("Settings", "bShowFps", settings.showFps);
		settings.showFrameTime  = ini.GetBoolValue("Settings", "bShowFrameTime", settings.showFrameTime);
		settings.showGraph      = ini.GetBoolValue("Settings", "bShowGraph", settings.showGraph);
		settings.showVram       = ini.GetBoolValue("Settings", "bShowVram", settings.showVram);
		settings.showStats      = ini.GetBoolValue("Settings", "bShowStats", settings.showStats);

		settings.corner         = static_cast<int>(ini.GetLongValue("Settings", "iCorner", settings.corner));
		settings.freeDrag       = ini.GetBoolValue("Settings", "bFreeDrag", settings.freeDrag);
		settings.dragPosX       = static_cast<float>(ini.GetDoubleValue("Settings", "fDragPosX", settings.dragPosX));
		settings.dragPosY       = static_cast<float>(ini.GetDoubleValue("Settings", "fDragPosY", settings.dragPosY));

		settings.opacity        = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fOpacity", settings.opacity)), 0.0f, 1.0f);
		settings.showBorder     = ini.GetBoolValue("Settings", "bShowBorder", settings.showBorder);
		settings.fontScale      = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fFontScale", settings.fontScale)), 0.5f, 3.0f);
		settings.highContrast   = ini.GetBoolValue("Settings", "bHighContrast", settings.highContrast);

		settings.autoThresholds = ini.GetBoolValue("Settings", "bAutoThresholds", settings.autoThresholds);
		settings.fpsGood        = static_cast<float>(ini.GetDoubleValue("Settings", "fFpsGood", settings.fpsGood));
		settings.fpsWarn        = static_cast<float>(ini.GetDoubleValue("Settings", "fFpsWarn", settings.fpsWarn));

		settings.updateInterval = std::clamp(static_cast<float>(ini.GetDoubleValue("Settings", "fUpdateInterval", settings.updateInterval)), 0.05f, 5.0f);
		settings.historySize    = std::clamp(static_cast<int>(ini.GetLongValue("Settings", "iHistorySize", settings.historySize)), 30, kHistoryCapacity);
	}

	void PerformanceOverlay::SaveSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		ini.SetBoolValue("Settings", "bEnabled", settings.enabled);
		ini.SetLongValue("Settings", "iPreset", settings.preset);

		ini.SetBoolValue("Settings", "bShowFps", settings.showFps);
		ini.SetBoolValue("Settings", "bShowFrameTime", settings.showFrameTime);
		ini.SetBoolValue("Settings", "bShowGraph", settings.showGraph);
		ini.SetBoolValue("Settings", "bShowVram", settings.showVram);
		ini.SetBoolValue("Settings", "bShowStats", settings.showStats);

		ini.SetLongValue("Settings", "iCorner", settings.corner);
		ini.SetBoolValue("Settings", "bFreeDrag", settings.freeDrag);
		ini.SetDoubleValue("Settings", "fDragPosX", settings.dragPosX);
		ini.SetDoubleValue("Settings", "fDragPosY", settings.dragPosY);

		ini.SetDoubleValue("Settings", "fOpacity", settings.opacity);
		ini.SetBoolValue("Settings", "bShowBorder", settings.showBorder);
		ini.SetDoubleValue("Settings", "fFontScale", settings.fontScale);
		ini.SetBoolValue("Settings", "bHighContrast", settings.highContrast);

		ini.SetBoolValue("Settings", "bAutoThresholds", settings.autoThresholds);
		ini.SetDoubleValue("Settings", "fFpsGood", settings.fpsGood);
		ini.SetDoubleValue("Settings", "fFpsWarn", settings.fpsWarn);

		ini.SetDoubleValue("Settings", "fUpdateInterval", settings.updateInterval);
		ini.SetLongValue("Settings", "iHistorySize", settings.historySize);

		ini.SaveFile(kIniPath);
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
				ImGui::PushStyleColor(ImGuiCol_Text, fpsColor(_displayedFps));
				ImGui::Text("%.0f FPS", _displayedFps);
				ImGui::PopStyleColor();
			}
			if (settings.showFrameTime)
				ImGui::Text("%.2f ms", _displayedFrameMs);

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
		ImGui::TextDisabled("Shift+F11 toggles the overlay in-game.");

		if (ImGui::Checkbox("Enabled", &settings.enabled))
			SaveSettings();

		ImGui::Separator();

		// Sliders save on commit (mouse-release / keyboard-deactivate) rather than per-tick;
		// otherwise a slider drag triggers a full INI rewrite on the render thread every frame.
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
