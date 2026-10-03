#pragma once

#include "Feature.h"
#include "PerformanceOverlaySettings.h"
#include <DearModdingUI/API.h>
#include <DearModdingUI/UI.h>
#include <Features/PerformanceOverlay/ABTesting/ABTestAggregator.h>
#include <Features/PerformanceOverlay/CircularBuffer.h>
#include <map>

namespace cs::features
{
	class PerformanceOverlay : public Feature
	{
	public:
		static PerformanceOverlay* GetSingleton();
		std::string_view GetName() const override { return "PerformanceOverlay"; }
		std::string_view GetDisplayName() const override { return "Performance Overlay"; }
		std::string GetFeatureSummary() const override { return "FPS, frame times, draw calls, VRAM usage, and shader performance."; }
		std::string GetCategory() const override { return FeatureCategories::kPerformance; }
		bool Configure(const toml::table&, std::string&) override;
		void Load() override;
		void OnDataLoaded() override;
		void OnRuntimeQuarantined() noexcept override;
		void DrawSettings() override;
		void DrawOverlay() override;
		bool IsOverlayActive() const override { return settings.ShowInOverlay || _testing || _restorePending; }
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink&) const override;
		void TickHostFrame(std::uint64_t a_vramUsedBytes, std::uint64_t a_vramBudgetBytes);
		DMUI_ManagedOverlayOptions ManagedOverlayOptions() const noexcept;
		const std::string& SuggestedToggleHotkey() const noexcept { return settings.toggleHotkey; }
		using Settings = performance_overlay::Settings;
		Settings settings;

	private:
		using Snapshot = std::map<Feature*, toml::table>;
		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(performance_overlay::kSchema); }
		static Snapshot CaptureSettings();
		static void ApplySettings(const Snapshot&, bool a_restoring = false);
		void SetTestInterval(int);
		void AbortTest(std::string_view a_reason) noexcept;
		void UpdateTest(double);
		void DrawTestResults();
		void DrawPasses();
		void DrawDrawCalls(const std::vector<DrawCallRow>&);
		void DrawGraph(const char*, const char*, const CircularBuffer<float>&, dmui::ui::Vec4);
		static float OverlayContentWidth() noexcept;
		void UpdateGraphValues();
		std::vector<DrawCallRow> BuildRows() const;

		CircularBuffer<float> _history{ 600 }, _postHistory{ 600 };
		LARGE_INTEGER _frequency{}, _lastFrame{}, _overlayFrequency{}, _lastUpdate{};
		double _lastSwitch{};
		float _frameMs{}, _fps{}, _displayMs{}, _displayFps{};
		float _postDisplayMs{}, _postDisplayFps{}, _updateTimer{};
		float _graphMin{}, _graphMax = 50.0f;
		float _minFrameMs = 1000.0f, _maxFrameMs{};
		float _averageMs{}, _refreshHz{};
		bool _frameGeneration{};
		std::uint64_t _vramUsed{}, _vramBudget{};
		Snapshot _baseline, _test;
		ABTestAggregator _aggregator;
		bool _testing{}, _variantB = true, _restorePending{};
		int _testInterval{}, _passSort = 1, _drawSort = 2, _groupSort = 1, _testSort = 3;
		bool _passDescending = true, _drawDescending = true, _groupDescending = true, _testDescending = true;
		bool _cpuTimings{};
		std::string _testError;
	};
}
