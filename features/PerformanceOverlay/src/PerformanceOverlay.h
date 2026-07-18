#pragma once

#include "Feature.h"
#include "Utils/Hotkey.h"

#include <array>
#include <cstdint>
#include <string>

struct IDXGIAdapter3;

namespace cs::features
{
	class PerformanceOverlay : public Feature
	{
	public:
		static PerformanceOverlay* GetSingleton();

		std::string_view GetName() const override { return "PerformanceOverlay"; }
		std::string GetFeatureSummary() const override { return "On-screen FPS counter and frame-time graph with estimated post-FG values."; }
		std::string GetCategory() const override { return "Diagnostics"; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		void DrawOverlay() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		enum class Preset : int
		{
			Off      = 0,
			Minimal  = 1,
			Standard = 2,
			Verbose  = 3
		};

		enum class Corner : int
		{
			TopLeft     = 0,
			TopRight    = 1,
			BottomLeft  = 2,
			BottomRight = 3
		};

		struct Settings
		{
			bool   enabled        = false;
			int    preset         = static_cast<int>(Preset::Standard);

			bool   showFps        = true;
			bool   showFrameTime  = true;
			bool   showGraph      = true;
			bool   showEstimatedPostFGFrameTime = true;
			bool   showVram       = false;
			bool   showStats      = false;

			// Corner snap by default; free-drag opt-in.
			int    corner         = static_cast<int>(Corner::TopLeft);
			bool   freeDrag       = false;
			float  dragPosX       = 10.0f;
			float  dragPosY       = 10.0f;

			float  opacity        = 0.5f;
			bool   showBorder     = true;
			float  fontScale      = 1.0f;
			bool   highContrast   = false;

			// FPS thresholds auto-seed from monitor refresh when enabled.
			bool   autoThresholds = true;
			float  fpsGood        = 60.0f;
			float  fpsWarn        = 30.0f;

			float  updateInterval = 0.5f;
			int    historySize    = 120;

			// Graph height at fontScale=1.0; scales with fontScale.
			float  graphHeightPx  = 80.0f;

			// In-game overlay toggle chord; parsed via cs::input::Hotkey. "none"/"" unbinds it.
			std::string toggleHotkey = "F10";
		};

		Settings settings;

	private:
		PerformanceOverlay() = default;

		void SaveSettings();
		void ApplyPreset(Preset preset);
		void RefreshToggleHotkey();
		static bool HandleWndProc(HWND, UINT, WPARAM, LPARAM);
		void TickFrame();
		void RecomputeStats();
		void EnsureRefreshHz();

		static constexpr int kHistoryCapacity = 600;
		std::array<float, kHistoryCapacity> _frameTimesMs{};
		std::array<float, kHistoryCapacity> _postFgFrameTimesMs{};
		int    _frameTimesHead    = 0;
		int    _frameTimesCount   = 0;

		double _lastFrameQpc      = 0.0;
		double _lastDisplayUpdate = 0.0;
		double _qpcFreq           = 0.0;

		float  _curFrameMs        = 0.0f;
		float  _displayedFps      = 0.0f;
		float  _displayedFrameMs  = 0.0f;
		int    _displayedFrameMultiplier = 1;
		// Backend-reported displayed FPS from env-counter deltas; equals _displayedFps when FG is off.
		float    _measuredDisplayedFps = 0.0f;
		uint64_t _lastDisplayedFrameTotal = 0;
		double   _lastDisplayedSampleSec  = 0.0;
		float  _avgMs             = 0.0f;
		float  _stddevMs          = 0.0f;
		float  _graphYMaxSmoothed = 0.0f;
		float  _onePctLowMs       = 0.0f;
		float  _pointOnePctLowMs  = 0.0f;

		float  _refreshHz         = 60.0f;
		bool   _refreshKnown      = false;

		IDXGIAdapter3* _adapter      = nullptr;
		uint64_t _vramUsedBytes   = 0;
		uint64_t _vramBudgetBytes = 0;

		cs::input::Hotkey _toggleHotkey;
		std::uint32_t     _toggleReleaseVk = 0;  // VK of a consumed toggle press whose key-up is still pending
	};
}
