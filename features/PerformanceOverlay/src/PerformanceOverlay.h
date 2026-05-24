#pragma once

#include "Feature.h"

#include <array>
#include <cstdint>
#include <memory>

namespace cs::features
{
	class PerformanceOverlay : public Feature
	{
	public:
		static PerformanceOverlay* GetSingleton();

		std::string_view GetName() const override { return "PerformanceOverlay"; }

		void Load() override;
		void DrawSettings() override;
		void DrawOverlay() override;

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

			// Per-section toggles (override the preset bundle).
			bool   showFps        = true;
			bool   showFrameTime  = true;
			bool   showGraph      = true;
			bool   showEstimatedPostFGFrameTime = true;
			bool   showVram       = false;
			bool   showStats      = false;

			// Position. Corner snap by default; free-drag opt-in for power users.
			int    corner         = static_cast<int>(Corner::TopLeft);
			bool   freeDrag       = false;
			float  dragPosX       = 10.0f;
			float  dragPosY       = 10.0f;

			// Style.
			float  opacity        = 0.5f;
			bool   showBorder     = true;
			float  fontScale      = 1.0f;
			bool   highContrast   = false;

			// Color thresholds (FPS). Auto-seeded from monitor refresh when autoThresholds=true.
			bool   autoThresholds = true;
			float  fpsGood        = 60.0f;
			float  fpsWarn        = 30.0f;

			// Tracking.
			float  updateInterval = 0.5f;
			int    historySize    = 120;
		};

		Settings settings;

	private:
		PerformanceOverlay() = default;

		void LoadSettings();
		void SaveSettings();
		void ApplyPreset(Preset preset);
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
		float  _avgMs             = 0.0f;
		float  _onePctLowMs       = 0.0f;
		float  _pointOnePctLowMs  = 0.0f;

		float  _refreshHz         = 60.0f;
		bool   _refreshKnown      = false;

		struct AdapterCache;
		std::unique_ptr<AdapterCache> _adapter;
		uint64_t _vramUsedBytes   = 0;
		uint64_t _vramBudgetBytes = 0;
	};
}
