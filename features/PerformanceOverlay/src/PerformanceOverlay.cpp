#include "PerformanceOverlay.h"

#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/FrameProfiler.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/TemporalPipeline.h"
#include "Settings/SettingsPersistence.h"
#include "Shared/PerfUtils.h"
#include "Telemetry/Telemetry.h"

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

		void SortHeader(std::span<const char* const> a_labels, int& a_column, bool& a_descending)
		{
			dmui::ui::TableNextRow();
			for (std::size_t i = 0; i < a_labels.size(); ++i) {
				(void)dmui::ui::TableSetColumnIndex(static_cast<int>(i));
				if (dmui::ui::Selectable(a_labels[i])) {
					a_descending = a_column == static_cast<int>(i) ? !a_descending : true;
					a_column = static_cast<int>(i);
				}
			}
		}

		template <class Row, class Metric>
		void SortRows(std::vector<Row>& a_rows, int a_column, bool a_descending, Metric a_metric)
		{
			std::ranges::stable_sort(a_rows, [&](const auto& a, const auto& b) {
				if constexpr (requires { a.shaderType; }) {
					if (a.shaderType < 0 || b.shaderType < 0) {
						if (a.shaderType < 0 && b.shaderType < 0)
							return a.shaderType < b.shaderType;
						return a.shaderType >= 0;
					}
				}
				const auto left = a_metric(a), right = a_metric(b);
				const int order = a_column == 0 ? a.label.compare(b.label) : (left > right) - (left < right);
				return a_descending ? order > 0 : order < 0;
			});
		}

		std::string Milliseconds(float a_value)
		{
			if (std::abs(a_value) < 1e-4f)
				return "0 ms";
			return a_value < 0.1f ? std::format("{:.3f} ms", a_value) : std::format("{:.2f} ms", a_value);
		}

		dmui::TextTone MetricTone(float a_value, bool a_costPerCall)
		{
			using Settings = performance_overlay::Settings;
			const float good = a_costPerCall ? Settings::kCostPerCallGoodThreshold : Settings::kFrameTimeGoodThreshold;
			const float warning = a_costPerCall ? Settings::kCostPerCallWarningThreshold : Settings::kFrameTimeWarningThreshold;
			return a_value < good ? dmui::TextTone::kSuccess : a_value < warning ? dmui::TextTone::kWarning :
			                                                                       dmui::TextTone::kError;
		}

		dmui::TextTone ComparisonTone(float a_left, float a_right)
		{
			return a_left < a_right ? dmui::TextTone::kSuccess : a_left > a_right ? dmui::TextTone::kError :
			                                                                        dmui::TextTone::kInherit;
		}

		void DrawMilliseconds(float a_value, dmui::TextTone a_tone = dmui::TextTone::kInherit, bool a_microseconds = false)
		{
			std::string text;
			if (a_microseconds && a_value < performance_overlay::Settings::kMicrosecondThreshold)
				text = std::abs(a_value * 1000.0f) < 1e-4f ? "0 us" : std::format("{:.2f} us", a_value * 1000.0f);
			else
				text = Milliseconds(a_value);
			(void)dmui::DrawStyledText(host::HostClient::Get().Client(), text, { .tone = a_tone });
		}

		void DrawDelta(float a_baseline, float a_test)
		{
			const float delta = a_test - a_baseline;
			float percent{};
			if (a_baseline < a_test && a_baseline > 0.0f)
				percent = 100.0f * (a_test - a_baseline) / a_baseline;
			else if (a_test < a_baseline && a_test > 0.0f)
				percent = 100.0f * (a_baseline - a_test) / a_test;
			auto text = (delta > 0.0f ? "+" : "") + Milliseconds(delta);
			if (percent >= performance_overlay::Settings::kPercentDisplayThreshold)
				text += std::format(" (+{:.1f}%)", a_test < a_baseline ? -percent : percent);
			(void)dmui::DrawStyledText(host::HostClient::Get().Client(), text,
				{ .tone = ComparisonTone(a_test, a_baseline) });
		}

		struct TimingRow
		{
			std::string label;
			float average{}, p95{}, p99{}, percent{};
		};

		void DrawTimingTable(const char* a_id, std::vector<TimingRow> a_rows, int& a_sort, bool& a_descending)
		{
			if (!dmui::ui::BeginTable(a_id, 5))
				return;
			const char* labels[]{ "Feature / Pass", "Avg", "P95", "P99", "%" };
			SortHeader(labels, a_sort, a_descending);
			SortRows(a_rows, a_sort, a_descending, [a_sort](const auto& a_row) {
				switch (a_sort) {
				case 2:
					return a_row.p95;
				case 3:
					return a_row.p99;
				case 4:
					return a_row.percent;
				default:
					return a_row.average;
				}
			});
			for (const auto& row : a_rows) {
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableSetColumnIndex(0);
				dmui::ui::TextUnformatted(row.label.c_str());
				const float values[]{ row.average, row.p95, row.p99, row.percent };
				for (int i = 0; i < 4; ++i) {
					(void)dmui::ui::TableSetColumnIndex(i + 1);
					if (i == 3)
						dmui::ui::Text("%.1f%%", values[i]);
					else
						DrawMilliseconds(values[i]);
				}
			}
			dmui::ui::EndTable();
		}

		nlohmann::json SnapshotJson(const std::map<Feature*, toml::table>& a_snapshot)
		{
			auto result = nlohmann::json::object();
			for (const auto& [feature, values] : a_snapshot) {
				auto& output = result[std::string(feature->GetName())];
				output = nlohmann::json::object();
				for (const auto& [key, node] : values) {
					if (const auto* boolean = node.as_boolean())
						output[std::string(key.str())] = boolean->get();
					else if (const auto* integer = node.as_integer())
						output[std::string(key.str())] = integer->get();
					else if (const auto* number = node.as_floating_point())
						output[std::string(key.str())] = number->get();
					else if (const auto* text = node.as_string())
						output[std::string(key.str())] = text->get();
				}
			}
			return result;
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
		L->info("Loaded: ShowInOverlay={} FrameHistorySize={} toggle_hotkey={}",
			settings.ShowInOverlay, settings.FrameHistorySize, settings.toggleHotkey);
	}

	void PerformanceOverlay::OnDataLoaded()
	{
		_baseline = CaptureSettings();
		QueryPerformanceCounter(&_lastFrame);
		_history.Resize(static_cast<std::size_t>(settings.FrameHistorySize));
		_postHistory.Resize(static_cast<std::size_t>(settings.FrameHistorySize));
		DEVMODEW mode{};
		mode.dmSize = sizeof(mode);
		if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
			_refreshHz = std::max(30.0f, static_cast<float>(mode.dmDisplayFrequency));
	}

	void PerformanceOverlay::OnRuntimeQuarantined() noexcept
	{
		render::profiling::SetEnabled(false);
		AbortTest("Performance Overlay was quarantined; restart required");
	}

	void PerformanceOverlay::AbortTest(std::string_view a_reason) noexcept
	{
		_testError = a_reason;
		L->error("A/B test stopped: {}", _testError);
		try {
			SetTestInterval(0);
		} catch (const std::exception& error) {
			_testError += std::format("; TEST restoration failed: {}", error.what());
			L->error("{}", _testError);
			_testing = false;
			_restorePending = true;
			cs::settings::liveComparisonActive = true;
			_testInterval = 0;
			_aggregator.OnTestEnd();
		}
		Menu::ShowToast(_testError, 8.0, DMUI_STATUS_SEVERITY_ERROR, std::string(GetDisplayName()));
	}

	bool PerformanceOverlay::SaveSettings()
	{
		return cs::settings::SaveDelta(performance_overlay::kSchema, GetConfigKey(), settings, *L);
	}

	PerformanceOverlay::Snapshot PerformanceOverlay::CaptureSettings()
	{
		Snapshot snapshot;
		for (auto* feature : FeatureManager::Get().GetAll()) {
			const auto& access = feature->GetLiveSettingsAccess();
			if (feature->IsHealthy() && access.snapshot)
				snapshot.emplace(feature, access.snapshot());
		}
		return snapshot;
	}

	void PerformanceOverlay::ApplySettings(const Snapshot& a_snapshot, bool a_restoring)
	{
		std::vector<cs::settings::PreparedLiveSettings> prepared;
		for (const auto& [feature, values] : a_snapshot) {
			if (!a_restoring && !feature->IsHealthy())
				throw std::runtime_error(std::format("{} is no longer healthy", feature->GetName()));
			std::string error;
			auto value = feature->GetLiveSettingsAccess().prepare(values, error);
			if (!value)
				throw std::runtime_error(std::format("{}: {}", feature->GetName(), error));
			if (a_restoring && !feature->IsHealthy())
				value->finalize = {};
			if (value->finalize) {
				value->finalize = [feature, finalize = std::move(value->finalize)] {
					auto& manager = FeatureManager::Get();
					if (!manager.PrepareRuntimeCallback(*feature, "ABComparison::Finalize"))
						return;
					try {
						finalize();
					} catch (const std::exception& error) {
						manager.QuarantineRuntimeCallback(*feature, "ABComparison::Finalize", error.what());
						manager.FinishRuntimeCallbackPass();
						throw;
					} catch (...) {
						manager.QuarantineRuntimeCallback(*feature, "ABComparison::Finalize", "non-standard exception");
						manager.FinishRuntimeCallbackPass();
						throw;
					}
				};
			}
			prepared.push_back(std::move(*value));
		}
		cs::settings::ApplyPreparedLiveSettings(prepared);
	}

	void PerformanceOverlay::SetTestInterval(int a_interval)
	{
		if (!a_interval) {
			if (_testing || _restorePending) {
				ApplySettings(_test, true);
				_aggregator.OnTestEnd();
			}
			_testing = false;
			_restorePending = false;
			cs::settings::liveComparisonActive = false;
			_testInterval = 0;
			return;
		}
		if (_restorePending)
			throw std::runtime_error("Restore TEST before starting another comparison");
		if (!_testing) {
			if (_baseline.empty())
				throw std::runtime_error("No healthy live-effect settings are available for A/B testing");
			auto test = CaptureSettings();
			if (test.size() != _baseline.size())
				throw std::runtime_error("Loaded features changed; capture a new USER baseline");
			for (const auto& [feature, values] : test) {
				(void)values;
				if (!_baseline.contains(feature))
					throw std::runtime_error("Loaded features changed; capture a new USER baseline");
			}
			if (test == _baseline)
				throw std::runtime_error("USER and TEST live settings are identical");
			_test = std::move(test);
			_aggregator.Clear();
			_aggregator.SetSettingsA(SnapshotJson(_baseline));
			_aggregator.SetSettingsB(SnapshotJson(_test));
			_variantB = true;
			_aggregator.OnABSwitch(ABVariant::B);
			_lastSwitch = Util::GetNowSecs();
			_testing = true;
			cs::settings::liveComparisonActive = true;
		}
		_testInterval = a_interval;
		_testError.clear();
	}

	void PerformanceOverlay::UpdateTest(double a_now)
	{
		if (!_testing)
			return;
		for (const auto& [feature, values] : _test) {
			(void)values;
			if (!feature->IsHealthy())
				throw std::runtime_error(std::format("{} was quarantined", feature->GetName()));
		}
		_aggregator.OnFrame(BuildRows());
		if (a_now - _lastSwitch > static_cast<double>(_testInterval)) {
			ApplySettings(_variantB ? _baseline : _test);
			_variantB = !_variantB;
			_aggregator.OnABSwitch(_variantB ? ABVariant::B : ABVariant::A);
			_lastSwitch = a_now;
		}
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
		if (_overlayFrequency.QuadPart == 0) {
			QueryPerformanceFrequency(&_overlayFrequency);
			QueryPerformanceCounter(&_lastUpdate);
		}
		QueryPerformanceCounter(&counter);
		const float delta = static_cast<float>(counter.QuadPart - _lastUpdate.QuadPart) /
		                    static_cast<float>(_overlayFrequency.QuadPart);
		_lastUpdate = counter;

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
			// FO4: the pinned active-FG branch always selects the calculated 2x fallback.
			const float postMs = _frameMs / Settings::kFrameGenerationMultiplier;
			if (_updateTimer <= 0.0f) {
				_postDisplayMs = postMs;
				_postDisplayFps = _fps * Settings::kFrameGenerationMultiplier;
			}
			_postHistory.Push(postMs);
		}
		_updateTimer += delta;
		if (_updateTimer >= settings.UpdateInterval) {
			_displayMs = _frameMs;
			_displayFps = _fps;
			_updateTimer = 0.0f;
		}
	}

	void PerformanceOverlay::TickHostFrame(std::uint64_t a_used, std::uint64_t a_budget)
	{
		_vramUsed = a_used;
		_vramBudget = a_budget;
		render::profiling::SetEnabled(settings.ShowInOverlay || _testing);
		if (!settings.ShowInOverlay && !_testing) {
			QueryPerformanceCounter(&_lastFrame);
			_overlayFrequency = {};
			return;
		}
		// FO4: DearModdingUI supplies the native-frame UI cadence independently of the deferred scene.
		UpdateGraphValues();
		try {
			UpdateTest(Util::GetNowSecs());
		} catch (const std::exception& error) {
			AbortTest(error.what());
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
		const float remainingOther = std::max(0.0f, other - csPasses);
		rows.push_back({ "CS Passes:", -3, -1, csPasses, Percentage(csPasses, _displayMs), 0.0f,
			"GPU time spent in Community Shaders compute passes (profiled).", true, {}, {} });
		rows.push_back({ "Other:", -2, -1, remainingOther, Percentage(remainingOther, _displayMs), 0.0f,
			"Frame time not attributed to any measured shader type or CS compute pass. This includes UI, post-processing, engine work, and any GPU activity not directly measured.", true, {}, {} });
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
				.size = { OverlayContentWidth(), 50.0f * settings.TextSize },
				.overlayText = a_overlay,
				.referenceLines = references.data(),
				.referenceLineCount = static_cast<std::uint32_t>(references.size()) });
		dmui::ui::PopStyleColor();
		if (dmui::ui::IsItemHovered())
			dmui::ui::SetTooltip("Reference lines: 30 FPS = 33.3 ms, 60 FPS = 16.7 ms, 120 FPS = 8.3 ms");
	}

	float PerformanceOverlay::OverlayContentWidth() noexcept
	{
		return dmui::ui::GetContentRegionAvail().x * 0.9f;
	}

	void PerformanceOverlay::DrawDrawCalls(const std::vector<DrawCallRow>& a_rows)
	{
		auto rows = a_rows;
		if (!dmui::ui::BeginTable("ShaderTimings", 4))
			return;
		const char* labels[]{ "Shader Type", "Draw Calls", "Frame Time", "Cost/Call" };
		SortHeader(labels, _drawSort, _drawDescending);
		SortRows(rows, _drawSort, _drawDescending, [this](const auto& a_row) {
			switch (_drawSort) {
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
			if (dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", row.tooltip.c_str());
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

	void PerformanceOverlay::DrawPasses()
	{
		const auto& results = render::profiling::GetProfiler().GetResults();
		(void)dmui::ui::Checkbox("CPU timings", &_cpuTimings);
		if (results.empty()) {
			dmui::ui::TextDisabled("No timing data available (enter game world)");
			return;
		}
		std::map<std::string, TimingRow> groups;
		std::vector<TimingRow> passes;
		float total{};
		for (const auto& result : results) {
			if (!result.valid)
				continue;
			const auto split = result.name.find("::");
			const auto name = result.name.substr(0, split == std::string::npos ? result.name.find('/') : split);
			TimingRow row{ result.name,
				_cpuTimings ? result.cpuAvgMs : result.avgMs,
				_cpuTimings ? result.cpuP95Ms : result.p95Ms,
				_cpuTimings ? result.cpuP99Ms : result.p99Ms };
			auto& group = groups[name];
			group.label = name;
			group.average += row.average;
			group.p95 += row.p95;
			group.p99 += row.p99;
			total += row.average;
			passes.push_back(std::move(row));
		}
		std::vector<TimingRow> summary;
		for (auto& [name, group] : groups) {
			(void)name;
			group.percent = Percentage(group.average, total);
			summary.push_back(group);
		}
		for (auto& row : passes)
			row.percent = Percentage(row.average, total);
		DrawTimingTable("CSFeatures", std::move(summary), _groupSort, _groupDescending);
		DrawTimingTable("CSPasses", std::move(passes), _passSort, _passDescending);
		dmui::ui::Text("CS total: %.3f GPU ms / %.3f CPU ms",
			render::profiling::GetProfiler().GetTotalTimeMs(),
			render::profiling::GetProfiler().GetCpuTotalTimeMs());
	}

	void PerformanceOverlay::DrawTestResults()
	{
		if (_testing)
			dmui::ui::Text("Variant %s: %.1f seconds left",
				_variantB ? "B (TEST)" : "A (USER)",
				std::max(0.0, _testInterval - (Util::GetNowSecs() - _lastSwitch)));
		std::vector<std::string> changes;
		for (const auto& [feature, values] : _test) {
			const auto baseline = _baseline.find(feature);
			if (baseline == _baseline.end())
				continue;
			for (const auto& [key, value] : values) {
				const auto* original = baseline->second.get(key);
				if (original && toml::node_view<const toml::node>{ original } == toml::node_view<const toml::node>{ &value })
					continue;
				std::ostringstream text;
				text << feature->GetName() << "." << key.str() << ": ";
				if (original)
					original->visit([&](const auto& node) { text << node; });
				text << " -> ";
				value.visit([&](const auto& node) { text << node; });
				changes.push_back(text.str());
			}
		}
		if (!changes.empty()) {
			if (const ui::Section section{ "overlay-ab-changes", "Changes from USER", ui::Collapsible{ .count = changes.size(), .framed = false } }; section) {
				for (const auto& change : changes)
					dmui::ui::TextUnformatted(change.c_str());
			}
		}
		if (!_testError.empty())
			dmui::ui::Text("A/B error: %s", _testError.c_str());
		if (!_aggregator.HasResults())
			return;
		const int frames = _aggregator.GetTotalFrameCount();
		const float duration = _aggregator.GetTotalTestDuration();
		int excluded{};
		for (const auto& interval : _aggregator.GetIntervals())
			excluded += interval.excludedFrames;
		const float validPercent = frames + excluded > 0 ? 100.0f * static_cast<float>(frames) / static_cast<float>(frames + excluded) : 0.0f;
		const char* validity = frames >= kMinimumSamplesForValidity && duration >= kMinimumTestDuration &&
		                               validPercent >= kMinimumValidFramesPercent ?
		                           "Valid" :
		                       frames >= kMinimumSamplesForMarginal && duration >= kMinimumDurationForMarginal ? "Marginal" :
		                                                                                                         "Insufficient";
		dmui::ui::Text("%s: %d frames, %.1f seconds, %d excluded, %.1f%% valid", validity, frames, duration, excluded, validPercent);
		if (!dmui::ui::BeginTable("ABResults", 7))
			return;
		const char* labels[]{ "Shader Type", "A Avg", "B Avg", "Delta", "A Median", "B Median", "Median Delta" };
		SortHeader(labels, _testSort, _testDescending);
		auto rows = _aggregator.GetAggregatedResults();
		SortRows(rows, _testSort, _testDescending, [this](const auto& row) {
			switch (_testSort) {
			case 1:
				return row.meanA;
			case 2:
				return row.meanB;
			case 4:
				return row.medianA;
			case 5:
				return row.medianB;
			case 6:
				return row.medianB - row.medianA;
			default:
				return row.delta;
			}
		});
		for (const auto& row : rows) {
			dmui::ui::TableNextRow();
			(void)dmui::ui::TableSetColumnIndex(0);
			dmui::ui::TextUnformatted(row.label.c_str());
			const float values[]{ row.meanA, row.meanB, row.delta, row.medianA, row.medianB, row.medianB - row.medianA };
			for (int i = 0; i < 6; ++i) {
				(void)dmui::ui::TableSetColumnIndex(i + 1);
				const bool available = (i == 0 || i == 3) ? row.frameCountA > 0 :
				                       (i == 1 || i == 4) ? row.frameCountB > 0 :
				                                            row.frameCountA > 0 && row.frameCountB > 0;
				if (!available) {
					dmui::ui::TextUnformatted("-");
				} else if (i == 2 || i == 5) {
					DrawDelta(i == 2 ? row.meanA : row.medianA, i == 2 ? row.meanB : row.medianB);
				} else {
					const float counterpart = i == 0 ? row.meanB : i == 1 ? row.meanA :
					                                           i == 3     ? row.medianB :
					                                                        row.medianA;
					const auto tone = row.frameCountA > 0 && row.frameCountB > 0 ?
					                      ComparisonTone(values[i], counterpart) :
					                      dmui::TextTone::kInherit;
					DrawMilliseconds(values[i], tone);
				}
				if (row.shaderType == -1 && row.frameCountA > 0 && row.frameCountB > 0 && dmui::ui::IsItemHovered())
					dmui::ui::SetTooltip("A (USER): %.2f FPS; B (TEST): %.2f FPS", Util::CalcFPS(row.meanA), Util::CalcFPS(row.meanB));
			}
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
				const auto samples = _history.GetData();
				if (std::ranges::all_of(samples, [](float a_sample) { return a_sample > 0.0f; }))
					dmui::ui::Text("Avg: %.1f FPS", Util::CalcFPS(_averageMs));
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
				DrawPasses();
		}
		DrawTestResults();
	}

	void PerformanceOverlay::DrawSettings()
	{
		cs::settings::SettingsEdit edit{ *this };
		const auto checkbox = [&](const char* label, bool& value) {
			edit.Discrete(dmui::ui::Checkbox(label, &value));
		};
		const auto slider = [&](const char* label, auto member, const char* format) {
			const auto range = performance_overlay::kSchema.EditRange(member);
			edit.Continuous(dmui::ui::SliderScalar(label, &(settings.*member), &range.min, &range.max, format,
				dmui::ui::SliderFlags::kAlwaysClamp));
		};
		if (const ui::Section section{ "overlay-content", "Display Options" }; section) {
			checkbox("Show in Overlay", settings.ShowInOverlay);
			if (settings.ShowInOverlay) {
				checkbox("Show FPS Counter", settings.ShowFPS);
				checkbox("Show Draw Calls", settings.ShowDrawCalls);
				checkbox("Show VRAM Usage", settings.ShowVRAM);
				checkbox("Show CS Render Passes", settings.ShowCSPasses);
				if (settings.ShowFPS) {
					checkbox(_frameGeneration ? "Show Pre-FG Frametime Graph" : "Show Frametime Graph", settings.ShowPreFGFrameTimeGraph);
					if (_frameGeneration)
						checkbox("Show Post-FG Frametime Graph", settings.ShowPostFGFrameTimeGraph);
				}
			}
		}
		if (settings.ShowInOverlay) {
			if (const ui::Section section{ "overlay-layout", "Appearance" }; section) {
				slider("Text Size", &Settings::TextSize, "%.2f");
				slider("Background Opacity", &Settings::BackgroundOpacity, "%.2f");
				checkbox("Show Border", settings.ShowBorder);
				slider("Update Interval", &Settings::UpdateInterval, "%.2f seconds");
				slider("Frame History Size", &Settings::FrameHistorySize, "%d");
				if (dmui::ui::Button("Reset Layout"))
					host::HostClient::Get().ResetOverlay();
			}
		}
		if (const ui::Section section{ "overlay-ab-test", "A/B Testing" }; section) {
			dmui::ui::TextWrapped("Capture USER before editing TEST. Set the interval to 0 to restore TEST.");
			if (_restorePending && dmui::ui::Button("Retry TEST Restoration"))
				AbortTest("Retrying TEST restoration");
			dmui::ui::BeginDisabled(_testing || _restorePending);
			if (dmui::ui::Button("Capture USER Baseline")) {
				_baseline = CaptureSettings();
				_test.clear();
				_aggregator.Clear();
				_testError.clear();
			}
			dmui::ui::EndDisabled();
			int interval = _testInterval;
			constexpr int minimum = 0, maximum = 10;
			if (dmui::ui::SliderScalar("A/B Test Interval", &interval, &minimum, &maximum)) {
				try {
					SetTestInterval(interval);
				} catch (const std::exception& error) {
					_testError = error.what();
					Menu::ShowToast(_testError, 6.0, DMUI_STATUS_SEVERITY_ERROR, std::string(GetDisplayName()));
				}
			}
			DrawTestResults();
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
		SetTestInterval(0);
		settings = Settings{};
		_history.Resize(600);
		_postHistory.Resize(600);
		_displayMs = _displayFps = _postDisplayMs = _postDisplayFps = 0.0f;
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
			.Field("avg_ms", static_cast<double>(_averageMs))
			.Field("refresh_hz", static_cast<double>(_refreshHz))
			.Field("frame_history_size", static_cast<std::int64_t>(settings.FrameHistorySize))
			.Field("engine_frame_sequence", static_cast<std::int64_t>(render::profiling::FrameSequence()))
			.Field("ab_testing", _testing)
			.Field("ab_restore_pending", _restorePending)
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
