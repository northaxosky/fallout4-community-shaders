#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "PerformanceOverlaySettings.h"

#include <DearModdingUI/API.h>

#include <array>
#include <cstdint>
#include <string>

namespace cs::features
{
	class PerformanceOverlay : public Feature
	{
	public:
		static PerformanceOverlay* GetSingleton();

		std::string_view GetName() const override { return "PerformanceOverlay"; }
		std::string_view GetDisplayName() const override { return "Performance Overlay"; }
		std::string GetFeatureSummary() const override { return "On-screen FPS counter and frame-time graph."; }
		std::string GetCategory() const override { return FeatureCategories::kPerformance; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		void DrawOverlay() override;
		bool IsOverlayActive() const override
		{
			return settings.enabled && settings.preset != static_cast<int>(Preset::Off);
		}
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		void TickHostFrame(
			std::uint64_t a_vramUsedBytes,
			std::uint64_t a_vramBudgetBytes);
		[[nodiscard]] DMUI_ManagedOverlayOptions ManagedOverlayOptions() const noexcept;
		void CommitOverlayPlacement(const DMUI_ManagedOverlayPlacement& a_placement);
		[[nodiscard]] const std::string& SuggestedToggleHotkey() const noexcept
		{
			return settings.toggleHotkey;
		}
		using Preset = performance_overlay::Preset;
		using Corner = performance_overlay::Corner;
		using Settings = performance_overlay::Settings;

		Settings settings;

	private:
		PerformanceOverlay() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(performance_overlay::kSchema); }
		void ApplyPreset(Preset preset);
		void TickFrame();
		void RecomputeStats();
		void EnsureRefreshHz();

		static constexpr int kHistoryCapacity = performance_overlay::kHistoryCapacity;
		std::array<float, kHistoryCapacity> _frameTimesMs{};
		int    _frameTimesHead    = 0;
		int    _frameTimesCount   = 0;

		double _lastFrameQpc      = 0.0;
		double _lastDisplayUpdate = 0.0;
		double _qpcFreq           = 0.0;

		float  _curFrameMs        = 0.0f;
		float  _displayedFps      = 0.0f;
		float  _displayedFrameMs  = 0.0f;
		float  _avgMs             = 0.0f;
		float  _stddevMs          = 0.0f;
		float  _graphYMaxSmoothed = 0.0f;
		float  _onePctLowMs       = 0.0f;
		float  _pointOnePctLowMs  = 0.0f;

		float  _refreshHz         = 60.0f;
		bool   _refreshKnown      = false;

		uint64_t _vramUsedBytes   = 0;
		uint64_t _vramBudgetBytes = 0;
	};
}
