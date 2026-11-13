#include "PerformanceOverlay.h"

#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/ProfilingRenderer.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "OverlayTable.h"
#include "Render/FrameBoundary.h"
#include "Render/FrameProfiler.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/TemporalPipeline.h"
#include "Settings/SettingsPersistence.h"
#include "Shared/PerfUtils.h"
#include "Telemetry/Telemetry.h"
#include "Utils/UI.h"

#include <DearModdingUI/Client.h>
#include <RE/B/BSShaderManager.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.performanceoverlay");

		using performance_overlay::DrawMilliseconds;
		using performance_overlay::SortHeaders;
		using performance_overlay::SortRows;

		float Percentage(float a_part, float a_total)
		{
			return a_total > 0.0f ? a_part / a_total * 100.0f : 0.0f;
		}

		float CostPerCall(float a_ms, float a_calls)
		{
			return a_calls > 0.0f ? a_ms / a_calls : 0.0f;
		}

		const char* ShaderTooltip(int a_type)
		{
			using Type = RE::BSShaderManager::ShaderEnum;
			switch (static_cast<Type>(a_type)) {
			case Type::kSky:
				return "Draw calls for the sky dome, clouds, and related effects.";
			case Type::kWater:
				return "Draw calls for water surfaces and effects.";
			case Type::kLighting:
				return "Draw calls for dynamic and static lighting passes.";
			case Type::kEffect:
				return "Draw calls for special effects, particles, and post-processing.";
			case Type::kUtility:
				return "Draw calls for utility passes, such as shadow masks or G-buffer fills.";
			case Type::kDistantTree:
				return "Draw calls for distant tree rendering (LOD vegetation).";
			case Type::kParticle:
				return "Draw calls for particle systems (smoke, sparks, etc.).";
			case Type::kBloodSpatter:
				return "Draw calls for blood splatter effects.";
			case Type::kImageSpace:
				return "Draw calls for image space post-processing effects.";
			default:
				return "Draw calls for this shader type.";
			}
		}

		dmui::TextTone MetricTone(float a_value, bool a_costPerCall)
		{
			using Settings = performance_overlay::Settings;
			const float good = a_costPerCall ? Settings::kCostPerCallGoodThreshold : Settings::kFrameTimeGoodThreshold;
			const float warning = a_costPerCall ? Settings::kCostPerCallWarningThreshold : Settings::kFrameTimeWarningThreshold;
			return a_value < good ? dmui::TextTone::kSuccess : a_value < warning ? dmui::TextTone::kWarning :
			                                                                       dmui::TextTone::kError;
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
		if (!cs::settings::Parse(performance_overlay::kSchema, a_config, candidate, a_error))
			return false;
		settings = candidate;
		return true;
	}

	void PerformanceOverlay::Load()
	{
		engine::InstallShaderSubclassHooks();
		if (!engine::EnsureDrawProfilingInstalled())
			FailLoad("Cannot install shader draw timing boundary");
		render::profiling::SetEnabled(settings.ShowInOverlay);
		QueryPerformanceFrequency(&_frequency);
		QueryPerformanceCounter(&_lastFrame);
		if (!render::RegisterPreSwap(*this, [this] { QueryPerformanceCounter(&_presentStart); }) ||
			!render::RegisterPostSwap(*this, [this] { AccumulatePresentTime(); }))
			L->warn("Present timing unavailable; frame boundary callbacks were not registered");
		L->info("Loaded: ShowInOverlay={} FrameHistorySize={} toggle_hotkey={}",
			settings.ShowInOverlay, settings.FrameHistorySize, settings.toggleHotkey);
	}

	void PerformanceOverlay::AccumulatePresentTime()
	{
		LARGE_INTEGER end;
		QueryPerformanceCounter(&end);
		const auto ms = static_cast<float>(static_cast<double>(end.QuadPart - _presentStart.QuadPart) * 1000.0 / static_cast<double>(_frequency.QuadPart));
		_smoothPresentMs = _smoothPresentMs * 0.95f + ms * 0.05f;
	}

	void PerformanceOverlay::OnDataLoaded()
	{
		_comparison.CaptureBaseline();
		QueryPerformanceCounter(&_lastFrame);
		_history.Resize(static_cast<std::size_t>(settings.FrameHistorySize));
		_postHistory.Resize(static_cast<std::size_t>(settings.FrameHistorySize));
		DEVMODEW mode{};
		mode.dmSize = sizeof(mode);
		if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
			_refreshHz = std::max(30.0f, static_cast<float>(mode.dmDisplayFrequency));
	}

	void PerformanceOverlay::OnRuntimeQuarantined(std::string_view, std::string_view) noexcept
	{
		render::profiling::SetEnabled(false);
		_comparison.Abort("Performance Overlay was quarantined; restart required");
	}

	bool PerformanceOverlay::SaveSettings()
	{
		return cs::settings::SaveDelta(performance_overlay::kSchema, GetConfigKey(), settings, *L);
	}

	void PerformanceOverlay::UpdateGraphValues()
	{
		_history.Resize(static_cast<std::size_t>(settings.FrameHistorySize));
		_postHistory.Resize(static_cast<std::size_t>(settings.FrameHistorySize));
		LARGE_INTEGER counter{};
		QueryPerformanceCounter(&counter);
		_frameMs = Util::CalcFrameTime(counter.QuadPart - _lastFrame.QuadPart, _frequency.QuadPart);
		_lastFrame = counter;
		_fps = Util::CalcFPS(_frameMs);
		AccumulateFrameStats();
		if (_overlayFrequency.QuadPart == 0) {
			QueryPerformanceFrequency(&_overlayFrequency);
			QueryPerformanceCounter(&_lastUpdate);
		}
		QueryPerformanceCounter(&counter);
		const float delta = static_cast<float>(counter.QuadPart - _lastUpdate.QuadPart) /
		                    static_cast<float>(_overlayFrequency.QuadPart);
		_lastUpdate = counter;

		// The open hitch would dominate the graph scale.
		if (!_graphGate.ShouldSample(++_hostTick))
			return;

		const float oldFrame = _history.GetData()[_history.GetHeadIdx()];
		_history.Push(_frameMs);
		const auto samples = _history.GetData();
		if (_frameMs > _maxFrameMs)
			_maxFrameMs = _frameMs;
		else if (_frameMs < _minFrameMs)
			_minFrameMs = _frameMs;
		else if (oldFrame == _minFrameMs)
			_minFrameMs = *std::ranges::min_element(samples);
		else if (oldFrame == _maxFrameMs)
			_maxFrameMs = *std::ranges::max_element(samples);
		_averageMs = std::accumulate(samples.begin(), samples.end(), 0.0f) / static_cast<float>(samples.size());
		float variance{};
		for (float sample : samples) {
			const float diff = sample - _averageMs;
			variance += diff * diff;
		}
		const float deviation = std::sqrt(variance / static_cast<float>(samples.size()));
		const float spread = std::clamp(deviation * Settings::kGraphSpreadMultiplier,
			Settings::kGraphMinSpread, Settings::kGraphMaxSpread);
		_graphMin += Settings::kSmoothingFactor * (std::max(0.0f, _averageMs - spread) - _graphMin);
		_graphMax += Settings::kSmoothingFactor * (_averageMs + spread - _graphMax);
		_frameGeneration = render::TemporalPipeline::Get().GetFrameGenerationDiagnostics(false).active;
		if (_frameGeneration) {
			// Upstream derives post-FG figures from the fixed multiplier.
			_postHistory.Push(_frameMs / Settings::kFrameGenerationMultiplier);
		}
		if (_smoothFrameWeight > 0.0 && _smoothTimeSumMs > 0.0) {
			_displayMs = static_cast<float>(_smoothTimeSumMs / _smoothFrameWeight);
			_displayFps = Util::CalcFPS(_displayMs);
		} else {
			_displayMs = _frameMs;
			_displayFps = _fps;
		}
		if (_frameGeneration) {
			_postDisplayMs = _displayMs / Settings::kFrameGenerationMultiplier;
			_postDisplayFps = _displayFps * Settings::kFrameGenerationMultiplier;
		}
		// Avg and 1% low sort the whole window, so they refresh on an interval.
		_updateTimer += delta;
		if (_updateTimer >= std::max(settings.UpdateInterval, Settings::kMinStatsRefreshSeconds)) {
			UpdateFrameStats();
			_updateTimer = 0.0f;
		}
	}

	void PerformanceOverlay::ResetFrameStats()
	{
		_smoothTimeSumMs = 0.0;
		_smoothFrameWeight = 0.0;
		_windowFrameMs.clear();
		_windowSumMs = 0.0;
		_windowAvgMs = 0.0f;
		_windowP99Ms = 0.0f;
	}

	void PerformanceOverlay::AccumulateFrameStats()
	{
		if (_frameMs <= 0.0f)
			return;
		// Decay by elapsed time, not frame count; the sums' ratio stays frames / time.
		const double timeConstantMs = std::max(settings.UpdateInterval, Settings::kMinSmoothingSeconds) * 1000.0;
		const double decay = std::exp(-_frameMs / timeConstantMs);
		_smoothTimeSumMs = _smoothTimeSumMs * decay + _frameMs;
		_smoothFrameWeight = _smoothFrameWeight * decay + 1.0;

		// Hitches stay in so the average matches frames / elapsed time.
		_windowFrameMs.push_back(_frameMs);
		_windowSumMs += _frameMs;
		constexpr double windowMs = Settings::kStatsWindowSeconds * 1000.0;
		while (_windowFrameMs.size() > 1 && _windowSumMs - _windowFrameMs.front() >= windowMs) {
			_windowSumMs -= _windowFrameMs.front();
			_windowFrameMs.pop_front();
		}
	}

	void PerformanceOverlay::UpdateFrameStats()
	{
		if (_windowFrameMs.empty() || _windowSumMs < Settings::kStatsMinSeconds * 1000.0) {
			_windowAvgMs = 0.0f;
			_windowP99Ms = 0.0f;
			return;
		}
		_windowAvgMs = static_cast<float>(_windowSumMs / static_cast<double>(_windowFrameMs.size()));
		// 1% low is the FPS of the 99th percentile frame time.
		std::vector<float> sorted(_windowFrameMs.begin(), _windowFrameMs.end());
		const auto index = static_cast<std::size_t>(0.99 * static_cast<double>(sorted.size() - 1));
		std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(index), sorted.end());
		_windowP99Ms = sorted[index];
	}

	void PerformanceOverlay::TickHostFrame(std::uint64_t a_used, std::uint64_t a_budget)
	{
		_vramUsed = a_used;
		_vramBudget = a_budget;
		const bool consuming = settings.ShowInOverlay || _comparison.Testing();
		render::profiling::SetEnabled(consuming);
		if (consuming)
			render::profiling::KeepActive();
		if (!consuming) {
			QueryPerformanceCounter(&_lastFrame);
			_overlayFrequency = {};
			_graphGate = {};
			ResetFrameStats();
			return;
		}
		// FO4: DearModdingUI supplies the native-frame UI cadence independently of the deferred scene.
		UpdateGraphValues();
		try {
			if (_comparison.Testing())
				_comparison.Update(Util::GetNowSecs(), BuildRows());
		} catch (const std::exception& error) {
			_comparison.Abort(error.what());
		}
	}

	std::vector<DrawCallRow> PerformanceOverlay::BuildRows() const
	{
		std::vector<DrawCallRow> rows;
		float measured{}, calls{};
		const auto timings = render::profiling::GetShaderTimings();
		for (const auto& timing : timings) {
			measured += timing.milliseconds;
			calls += timing.calls;
		}
		for (const auto& timing : timings) {
			rows.push_back({ timing.name, timing.type, static_cast<int>(timing.calls),
				timing.milliseconds, Percentage(timing.milliseconds, measured),
				CostPerCall(timing.milliseconds, timing.calls),
				ShaderTooltip(timing.type),
				true, {}, {} });
		}
		float other = _displayMs - measured;
		if (std::abs(other) < 1e-4f)
			other = 0.0f;
		const float csPasses = render::profiling::GetProfiler().GetTotalTimeMs();
		// Capped at the unattributed time it is carved from.
		const float present = std::min(_smoothPresentMs, std::max(0.0f, other));
		const float remainingOther = std::max(0.0f, other - present);
		rows.push_back({ "CS Passes:", -3, -1, csPasses, Percentage(csPasses, _displayMs), 0.0f,
			"GPU time spent in Community Shaders compute passes (profiled).", true, {}, {} });
		rows.push_back({ "Present:", -4, -1, present, Percentage(present, _displayMs), 0.0f,
			"CPU time spent waiting inside Main::Swap, which presents the frame. Capped at the frame time not attributed to shader types, so it can read lower than the measured wait.", true, {}, {} });
		rows.push_back({ "Other:", -2, -1, remainingOther, Percentage(remainingOther, _displayMs), 0.0f,
			"Frame time not attributed to any measured shader type or to Present. This includes UI, post-processing, engine work, and CS compute passes (whose GPU time is shown separately).", true, {}, {} });
		rows.push_back({ "Total:", -1, static_cast<int>(calls), _displayMs, 100.0f,
			CostPerCall(_displayMs, calls), "Total frame time.", true, {}, {} });
		return rows;
	}

	void PerformanceOverlay::DrawGraph(const char* a_id, const char* a_overlay, const CircularBuffer<float>& a_history, dmui::ui::Vec4 DMUI_ThemeColors::* a_role)
	{
		const auto theme = dmui::ui::GetThemeColors();
		const std::array references{
			DMUI_PlotReferenceLine{ 1000.0f / 30.0f, theme.error },
			DMUI_PlotReferenceLine{ 1000.0f / 60.0f, theme.warning },
			DMUI_PlotReferenceLine{ 1000.0f / 120.0f, theme.success }
		};
		const auto samples = a_history.GetData();
		dmui::ui::PushStyleColor(dmui::ui::Color::kPlotLines, theme.*a_role);
		dmui::ui::PlotAnnotated(a_id,
			{ .samples = samples.data(),
				.sampleCount = static_cast<std::uint32_t>(samples.size()),
				.sampleOffset = static_cast<std::uint32_t>(a_history.GetHeadIdx()),
				.scaleMinimum = _graphMin,
				// The host rejects an empty scale range.
				.scaleMaximum = std::max(_graphMax, _graphMin + 0.001f),
				.size = { OverlayContentWidth(), 50.0f * Util::GetUIScale() * settings.TextSize },
				.overlayText = a_overlay,
				.referenceLines = references.data(),
				.referenceLineCount = static_cast<std::uint32_t>(references.size()) });
		dmui::ui::PopStyleColor();
		ui::Tooltip("Reference lines: 30 FPS = 33.3 ms, 60 FPS = 16.7 ms, 120 FPS = 8.3 ms");
	}

	float PerformanceOverlay::OverlayContentWidth() noexcept
	{
		return dmui::ui::GetContentRegionAvail().x * 0.9f;
	}

	void PerformanceOverlay::DrawDrawCalls(const std::vector<DrawCallRow>& a_rows)
	{
		auto rows = a_rows;
		if (!dmui::ui::BeginTable("ShaderTimings", 4, dmui::ui::TableFlags::kSortable))
			return;
		const char* labels[]{ "Shader Type", "Draw Calls", "Frame Time", "Cost/Call" };
		const auto sort = SortHeaders(labels, 2);
		SortRows(rows, sort, [&sort](const auto& a_row) {
			switch (sort.column) {
			case 1:
				return static_cast<float>(a_row.drawCalls);
			case 3:
				return a_row.costPerCall;
			default:
				return a_row.frameTime;
			}
		});
		for (const auto& row : rows) {
			dmui::ui::TableNextRow();
			(void)dmui::ui::TableSetColumnIndex(0);
			dmui::ui::TextUnformatted(row.label.c_str());
			ui::Tooltip(row.tooltip.c_str());
			(void)dmui::ui::TableSetColumnIndex(1);
			if (row.drawCalls >= 0)
				dmui::ui::Text("%d", row.drawCalls);
			else
				dmui::ui::TextUnformatted("-");
			(void)dmui::ui::TableSetColumnIndex(2);
			DrawMilliseconds(row.frameTime, MetricTone(row.frameTime, false), true);
			(void)dmui::ui::TableSetColumnIndex(3);
			if (row.drawCalls >= 0)
				DrawMilliseconds(row.costPerCall, MetricTone(row.costPerCall, true), true);
			else
				dmui::ui::TextUnformatted("-");
		}
		dmui::ui::EndTable();
	}

	void PerformanceOverlay::DrawOverlay()
	{
		if (!IsOverlayActive())
			return;
		if (settings.ShowInOverlay) {
			if (settings.ShowFPS) {
				dmui::ui::Text("%s %.1f FPS (%.2f ms)", _frameGeneration ? "[Pre-FG]" : "[Engine]", _displayFps, _displayMs);
				if (_windowAvgMs > 0.0f)
					dmui::ui::Text("Avg: %.1f FPS | 1%% Low: %.1f FPS", Util::CalcFPS(_windowAvgMs), Util::CalcFPS(_windowP99Ms));
				if (settings.ShowPreFGFrameTimeGraph) {
					const auto overlay = std::format("{}{:.2f} ms ({:.1f} FPS)", _frameGeneration ? "Pre-FG: " : "", _displayMs, _displayFps);
					DrawGraph("##frametime", overlay.c_str(), _history, &DMUI_ThemeColors::success);
				}
				if (_frameGeneration) {
					dmui::ui::Text("[Post-FG calculated, 2x] %.1f FPS (%.2f ms)", _postDisplayFps, _postDisplayMs);
					if (settings.ShowPostFGFrameTimeGraph) {
						const auto overlay = std::format("Post-FG: {:.2f} ms ({:.1f} FPS)", _postDisplayMs, _postDisplayFps);
						DrawGraph("##postfgframetime", overlay.c_str(), _postHistory, &DMUI_ThemeColors::info);
					}
				}
			}
			if (settings.ShowVRAM) {
				if (_vramBudget) {
					const float fraction = static_cast<float>(static_cast<double>(_vramUsed) / static_cast<double>(_vramBudget));
					dmui::ui::Text("VRAM %.2f / %.2f GB (%.1f%%)",
						_vramUsed / (1024.0 * 1024 * 1024), _vramBudget / (1024.0 * 1024 * 1024), fraction * 100.0f);
					dmui::ui::ProgressBar(fraction, { OverlayContentWidth(), 0.0f }, "");
				} else {
					dmui::ui::TextDisabled("VRAM unavailable");
				}
			}
			if (settings.ShowDrawCalls)
				DrawDrawCalls(BuildRows());
			if (settings.ShowCSPasses)
				ProfilingRenderer::RenderStatistics(false, false);
		}
		_comparison.DrawResults();
	}

	void PerformanceOverlay::DrawSettings()
	{
		cs::settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(performance_overlay::kSchema, settings);
		constexpr auto clamped = dmui::ui::SliderFlags::kAlwaysClamp;
		if (const ui::Section section{ "overlay-content", "Display Options" }; section) {
			field(&Settings::ShowInOverlay);
			if (settings.ShowInOverlay) {
				field(&Settings::ShowFPS);
				field(&Settings::ShowDrawCalls);
				field(&Settings::ShowVRAM);
				field(&Settings::ShowCSPasses);
				if (settings.ShowFPS) {
					field(&Settings::ShowPreFGFrameTimeGraph, { .label = _frameGeneration ? "Show Pre-FG Frametime Graph" : "Show Frametime Graph" });
					if (_frameGeneration)
						field(&Settings::ShowPostFGFrameTimeGraph);
				}
			}
		}
		if (settings.ShowInOverlay) {
			if (const ui::Section section{ "overlay-layout", "Appearance" }; section) {
				field(&Settings::TextSize, { .format = "%.2f", .flags = clamped });
				field(&Settings::BackgroundOpacity, { .format = "%.2f", .flags = clamped });
				field(&Settings::ShowBorder);
				field(&Settings::UpdateInterval, { .help = "How long the FPS readout averages over. It updates every frame; higher values steady it, lower values follow changes faster. Avg and 1% low refresh at this rate too (at most 4 times a second).", .format = "%.2f seconds", .flags = clamped });
				field(&Settings::FrameHistorySize, { .format = "%d", .flags = clamped });
				if (dmui::ui::Button("Reset Layout"))
					host::HostClient::Get().ResetOverlay();
			}
		}
		if (const ui::Section section{ "overlay-ab-test", "A/B Testing" }; section) {
			_comparison.DrawControls();
		}
	}

	DMUI_ManagedOverlayOptions PerformanceOverlay::ManagedOverlayOptions() const noexcept
	{
		return {
			.anchor = DMUI_OVERLAY_ANCHOR_FREE,
			.offset = { 10.0f, 10.0f },
			.size = { 0.0f, 0.0f },
			.minimumSize = { 600.0f, 0.0f },
			.maximumSize = { 1000.0f, 10000.0f },
			.opacity = settings.BackgroundOpacity,
			.contentScale = settings.TextSize,
			.backgroundVisible = settings.ShowBorder ? 1u : 0u,
			.borderVisible = settings.ShowBorder ? 1u : 0u,
			.allowArrangement = settings.ShowBorder ? 1u : 0u
		};
	}

	void PerformanceOverlay::RestoreDefaultSettings()
	{
		_comparison.SetInterval(0);
		settings = Settings{};
		_history.Resize(600);
		_postHistory.Resize(600);
		_displayMs = _displayFps = _postDisplayMs = _postDisplayFps = 0.0f;
		ResetFrameStats();
		_graphMin = 0.0f;
		_graphMax = 50.0f;
		_minFrameMs = 1000.0f;
		_maxFrameMs = 0.0f;
		SaveSettings();
	}

	void PerformanceOverlay::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink.Field("enabled", settings.ShowInOverlay)
			.Field("fps", static_cast<double>(_displayFps))
			.Field("frame_ms", static_cast<double>(_frameMs))
			.Field("avg_ms", static_cast<double>(_windowAvgMs))
			.Field("refresh_hz", static_cast<double>(_refreshHz))
			.Field("frame_history_size", static_cast<std::int64_t>(settings.FrameHistorySize))
			.Field("engine_frame_sequence", static_cast<std::int64_t>(render::profiling::FrameSequence()))
			.Field("ab_testing", _comparison.Testing())
			.Field("ab_restore_pending", _comparison.RestorePending())
			.Field("post_fg_calculated", _frameGeneration)
			.Field("vram_available", _vramBudget > 0)
			.Field("vram_used_mb", static_cast<std::int64_t>(_vramUsed / (1024 * 1024)))
			.Field("vram_budget_mb", static_cast<std::int64_t>(_vramBudget / (1024 * 1024)));
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(PerformanceOverlay::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
