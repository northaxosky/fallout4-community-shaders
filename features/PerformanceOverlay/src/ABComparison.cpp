#include "ABComparison.h"

#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "OverlayTable.h"
#include "Render/FrameDispatcher.h"
#include "Render/ShaderInjection.h"
#include "Settings/LiveSettings.h"
#include "Shared/PerfUtils.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace cs::features::performance_overlay
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.performanceoverlay");

		// A switch recreates resources; sample only after both gates pass.
		constexpr int kSettleFrames = 10;

		// Mean Total frame time of one window; none when it sampled nothing.
		std::optional<double> WindowMean(const ABInterval& a_interval)
		{
			double sum = 0.0;
			int count = 0;
			for (const auto& frame : a_interval.frameRows) {
				if (const auto total = std::ranges::find(frame, -1, &DrawCallRow::shaderType); total != frame.end()) {
					sum += total->frameTime;
					++count;
				}
			}
			return count ? std::optional{ sum / count } : std::nullopt;
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

		nlohmann::json OwnershipJson(const feature_config::ShaderOwnershipConfig& a_ownership)
		{
			auto targets = nlohmann::json::object();
			for (const auto& target : engine::GetShaderInjectionTargets())
				targets[std::string(target.name)] = a_ownership.targets[target.id];
			return { { "enable_shaders", a_ownership.enableShaders }, { "targets", std::move(targets) } };
		}
	}

	ABComparison::Progress ABComparison::GetProgress() const noexcept
	{
		return { _testing, _restorePending, ScriptedRunning(), std::min(_windowsDone + 1, _windowLimit), _windowLimit, _variantB,
			_testing ? std::max(0.0, _testInterval - (Util::GetNowSecs() - _lastSwitch)) : 0.0 };
	}

	std::optional<ABComparison::Summary> ABComparison::Summarize() const
	{
		if (!_aggregator.HasResults())
			return std::nullopt;
		Summary summary;
		summary.frames = _aggregator.GetTotalFrameCount();
		summary.duration = _aggregator.GetTotalTestDuration();
		bool hasSamplesA = false;
		bool hasSamplesB = false;
		std::vector<double> deltas;
		std::optional<double> pendingB;
		for (const auto& interval : _aggregator.GetIntervals()) {
			const auto mean = WindowMean(interval);
			if (interval.variant == ABVariant::B) {
				pendingB = mean;
			} else {
				if (pendingB && mean)
					deltas.push_back(*pendingB - *mean);
				pendingB.reset();
			}
			++summary.windows;
			summary.excluded += interval.excludedFrames;
			hasSamplesA = hasSamplesA || (interval.variant == ABVariant::A && !interval.frameRows.empty());
			hasSamplesB = hasSamplesB || (interval.variant == ABVariant::B && !interval.frameRows.empty());
		}
		const bool hasBothVariants = hasSamplesA && hasSamplesB;
		summary.pairs = static_cast<int>(deltas.size());
		if (deltas.size() >= 2) {
			const auto n = static_cast<double>(deltas.size());
			const double average = std::accumulate(deltas.begin(), deltas.end(), 0.0) / n;
			double squares = 0.0;
			for (const auto delta : deltas)
				squares += (delta - average) * (delta - average);
			summary.deltaStdErr = static_cast<float>(std::sqrt(squares / (n - 1.0) / n));
		}
		summary.comparable = hasBothVariants;
		for (const auto& row : _aggregator.GetAggregatedResults()) {
			if (row.shaderType != -1 || !hasBothVariants)
				continue;
			summary.meanA = row.meanA;
			summary.meanB = row.meanB;
			summary.medianA = row.medianA;
			summary.medianB = row.medianB;
			summary.delta = row.delta;
			summary.fpsA = Util::CalcFPS(row.meanA);
			summary.fpsB = Util::CalcFPS(row.meanB);
		}
		summary.validPercent = summary.frames + summary.excluded > 0 ? 100.0f * static_cast<float>(summary.frames) / static_cast<float>(summary.frames + summary.excluded) : 0.0f;
		summary.validity = hasBothVariants && summary.frames >= kMinimumSamplesForValidity && summary.duration >= kMinimumTestDuration &&
		                           summary.validPercent >= kMinimumValidFramesPercent ?
		                       Validity::kValid :
		                   hasBothVariants && summary.frames >= kMinimumSamplesForMarginal && summary.duration >= kMinimumDurationForMarginal ? Validity::kMarginal :
		                                                                                                                                        Validity::kInsufficient;
		return summary;
	}

	void ABComparison::CaptureBaseline()
	{
		_baseline = Capture();
		_a = _b = {};
		_aggregator.Clear();
		_lastSummary.reset();
		_scripted = false;
		_testError.clear();
	}

	void ABComparison::Abort(std::string_view a_reason) noexcept
	{
		_testError = a_reason;
		L->error("A/B test stopped: {}", _testError);
		try {
			SetInterval(0);
		} catch (const std::exception&) {
			// The stop path already recorded the failed restoration.
		}
		Menu::ShowToast(_testError, 8.0, DMUI_STATUS_SEVERITY_ERROR, std::string(_owner.GetDisplayName()));
	}

	void ABComparison::Fail(std::string a_message)
	{
		_testError = std::move(a_message);
		Menu::ShowToast(_testError, 6.0, DMUI_STATUS_SEVERITY_ERROR, std::string(_owner.GetDisplayName()));
	}

	ABComparison::State ABComparison::Capture()
	{
		State state;
		state.ownership = engine::GetShaderOwnershipConfig();
		for (auto* feature : FeatureManager::Get().GetAll()) {
			const auto& access = feature->GetLiveSettingsAccess();
			if (!feature->IsHealthy() || !access.snapshot)
				continue;
			auto values = access.snapshot();
			for (const auto key : access.notComparable)
				values.erase(key);
			state.live.emplace(feature, std::move(values));
		}
		return state;
	}

	void ABComparison::Apply(const State& a_state, bool a_restoring)
	{
		auto& manager = FeatureManager::Get();
		std::vector<cs::settings::PreparedLiveSettings> prepared;
		for (const auto& [feature, values] : a_state.live) {
			if (!a_restoring && !feature->IsHealthy())
				throw std::runtime_error(std::format("{} is no longer healthy", feature->GetName()));
			std::string error;
			auto value = manager.PrepareLiveSettings(*feature, values, "ABComparison::Finalize", error);
			if (!value)
				throw std::runtime_error(std::format("{}: {}", feature->GetName(), error));
			if (a_restoring && !feature->IsHealthy())
				value->finalize = {};
			prepared.push_back(std::move(*value));
		}
		render::PostToFrame([&manager, ownership = a_state.ownership, prepared = std::move(prepared)]() mutable {
			if (ownership != engine::GetShaderOwnershipConfig())
				manager.ApplyShaderOwnership(ownership);
			cs::settings::ApplyPreparedLiveSettings(prepared);
		});
	}

	void ABComparison::Begin(State a_a, State a_b, std::string a_labelA, std::string a_labelB, int a_interval, int a_windowLimit, double a_settleSeconds)
	{
		_a = std::move(a_a);
		_b = std::move(a_b);
		_labelA = std::move(a_labelA);
		_labelB = std::move(a_labelB);
		_aggregator.Clear();
		_lastSummary.reset();
		auto describe = [](const State& a_state) {
			auto json = SnapshotJson(a_state.live);
			json["ShaderOwnership"] = OwnershipJson(a_state.ownership);
			return json;
		};
		_aggregator.SetSettingsA(describe(_a));
		_aggregator.SetSettingsB(describe(_b));
		_variantB = true;
		_windowsDone = 0;
		_windowLimit = a_windowLimit;
		_scripted = a_windowLimit > 0;
		_settleSeconds = a_settleSeconds;
		_framesSinceSwitch = 0;
		_aggregator.OnABSwitch(ABVariant::B);
		_lastSwitch = Util::GetNowSecs();
		_testInterval = a_interval;
		_testError.clear();
		_testing = true;
		cs::settings::liveComparisonActive = true;
	}

	void ABComparison::SetInterval(int a_interval)
	{
		if (!a_interval) {
			if (_testing || _restorePending) {
				try {
					Apply(_b, true);
				} catch (const std::exception& error) {
					// Leave the run so shaders stop toggling; a retry restores.
					const auto message = std::format("{} restoration failed: {}", _labelB, error.what());
					_testError += (_testError.empty() ? "" : "; ") + message;
					L->error("{}", _testError);
					_testing = false;
					_restorePending = true;
					_testInterval = 0;
					_aggregator.OnTestEnd();
					throw std::runtime_error(message);
				}
				_aggregator.OnTestEnd();
				_lastSummary = Summarize();
				if (_scripted)
					LogSummary();
			}
			_testing = false;
			_restorePending = false;
			cs::settings::liveComparisonActive = false;
			_testInterval = 0;
			return;
		}
		if (_restorePending)
			throw std::runtime_error(std::format("Restore {} before starting another comparison", _labelB));
		if (_testing) {
			if (_scripted)
				throw std::runtime_error("A scripted comparison is running; stop it first");
			_testInterval = a_interval;
			_testError.clear();
			return;
		}
		if (_baseline.live.empty())
			throw std::runtime_error("No healthy live-effect settings are available for A/B testing");
		auto test = Capture();
		if (test.live.size() != _baseline.live.size())
			throw std::runtime_error("Loaded features changed; capture a new USER baseline");
		for (const auto& [feature, values] : test.live) {
			(void)values;
			if (!_baseline.live.contains(feature))
				throw std::runtime_error("Loaded features changed; capture a new USER baseline");
		}
		if (test == _baseline)
			throw std::runtime_error("USER and TEST live settings are identical");
		Begin(_baseline, std::move(test), "USER", "TEST", a_interval, 0, RunOptions{}.settleSeconds);
	}

	void ABComparison::StartRun(const Variant& a_variant, const RunOptions& a_options, std::string a_labelA, std::string a_labelB)
	{
		if (_testing || _restorePending)
			throw std::runtime_error("A comparison is already running");
		if (a_options.windowSeconds < 1 || a_options.cycles < 1)
			throw std::runtime_error("A run needs at least one cycle of one-second windows");
		if (a_options.settleSeconds < kMinSettleSeconds || a_options.settleSeconds >= a_options.windowSeconds) {
			throw std::runtime_error(std::format("settleSeconds must be from {} up to but not including windowSeconds ({})",
				kMinSettleSeconds, a_options.windowSeconds));
		}
		auto current = Capture();
		auto variant = current;
		for (const auto& [feature, overrides] : a_variant.settings) {
			const auto live = variant.live.find(feature);
			if (live == variant.live.end())
				throw std::runtime_error(std::format("{} has no live settings to compare (not loaded or not healthy)", feature->GetName()));
			for (const auto& [key, node] : overrides) {
				if (!live->second.contains(key))
					throw std::runtime_error(std::format("{}.{} is not a comparable live setting", feature->GetName(), key.str()));
				live->second.insert_or_assign(key, node);
			}
			if (std::string error; !FeatureManager::Get().PrepareLiveSettings(*feature, live->second, "ABComparison::StartRun", error))
				throw std::runtime_error(std::format("{}: {}", feature->GetName(), error));
		}
		if (a_variant.ownership)
			variant.ownership = *a_variant.ownership;
		if (variant == current)
			throw std::runtime_error("The variant changes nothing from the current state");
		Begin(std::move(variant), std::move(current), std::move(a_labelA), std::move(a_labelB), a_options.windowSeconds, a_options.cycles * 2, a_options.settleSeconds);
	}

	void ABComparison::StartCostRun(const RunOptions& a_options)
	{
		if (!engine::GetShaderOwnershipConfig().enableShaders)
			throw std::runtime_error("Custom shaders are already off; turn them on to measure their cost");
		auto ownership = engine::GetShaderOwnershipConfig();
		ownership.enableShaders = false;
		StartRun({ .ownership = ownership }, a_options, "CS off", "CS on");
	}

	void ABComparison::Update(double a_now, const std::vector<DrawCallRow>& a_rows)
	{
		if (!_testing)
			return;
		for (const auto& [feature, values] : _b.live) {
			(void)values;
			if (!feature->IsHealthy())
				throw std::runtime_error(std::format("{} was quarantined", feature->GetName()));
		}
		++_framesSinceSwitch;
		if (_framesSinceSwitch >= kSettleFrames && a_now - _lastSwitch >= _settleSeconds) {
			// Per-type rows freeze without draw profiling; scripted runs keep Total.
			if (!_scripted) {
				_aggregator.OnFrame(a_rows);
			} else if (const auto total = std::ranges::find(a_rows, -1, &DrawCallRow::shaderType); total != a_rows.end()) {
				_aggregator.OnFrame({ *total });
			}
		}
		if (a_now - _lastSwitch > static_cast<double>(_testInterval)) {
			++_windowsDone;
			if (_scripted && _windowsDone >= _windowLimit) {
				SetInterval(0);
				return;
			}
			Apply(_variantB ? _a : _b);
			_variantB = !_variantB;
			_aggregator.OnABSwitch(_variantB ? ABVariant::B : ABVariant::A);
			_lastSwitch = a_now;
			_framesSinceSwitch = 0;
		}
	}

	void ABComparison::LogSummary() const
	{
		if (!_lastSummary)
			return;
		const auto& summary = *_lastSummary;
		if (!summary.comparable) {
			L->info("{} vs {}: not measured, {} windows, {}", _labelB, _labelA, summary.windows, ValidityName(summary.validity));
			return;
		}
		L->info("{} vs {}: {} {:.2f} ms ({:.1f} FPS), {} {:.2f} ms ({:.1f} FPS), delta {:+.2f}{} ms, {} windows, {}",
			_labelB, _labelA, _labelB, summary.meanB, summary.fpsB, _labelA, summary.meanA, summary.fpsA, summary.delta,
			summary.deltaStdErr ? std::format(" \u00b1 {:.2f}", *summary.deltaStdErr) : std::string{},
			summary.windows, ValidityName(summary.validity));
	}

	std::string ABComparison::CostHelp() const
	{
		std::string unaffected;
		for (auto* feature : FeatureManager::Get().GetAll()) {
			if (!feature->IsActive())
				continue;
			if (!feature->FollowsShaderOwnership())
				unaffected += std::format("{}{}", unaffected.empty() ? "" : ", ", feature->GetDisplayName());
		}
		const RunOptions options;
		return std::format(
			"Alternates custom shaders on and off in {} second windows for {} cycles. Off still includes CS hooks and per-draw lookups{}, "
			"so it reads lower than removing the plugin.",
			options.windowSeconds, options.cycles,
			unaffected.empty() ? "" : std::format(", and features that do not follow shader ownership ({})", unaffected));
	}

	std::expected<void, std::string> ABComparison::Try(const std::function<void()>& a_action)
	{
		try {
			a_action();
		} catch (const std::exception& error) {
			return std::unexpected(error.what());
		}
		return {};
	}

	void ABComparison::Attempt(const std::function<void()>& a_action)
	{
		if (auto result = Try(a_action); !result)
			Fail(std::move(result.error()));
	}

	void ABComparison::DrawControls()
	{
		dmui::ui::TextWrapped("Capture USER before editing TEST. Set the interval to 0 to restore TEST.");
		if (const auto retry = std::format("Retry {} Restoration", _labelB); _restorePending && dmui::ui::Button(retry.c_str()))
			Attempt([&] { SetInterval(0); });
		{
			const dmui::DisabledScope disabled{ _testing || _restorePending };
			if (dmui::ui::Button("Capture USER Baseline"))
				CaptureBaseline();
		}
		const dmui::DisabledScope disabled{ ScriptedRunning() };
		int interval = _testInterval;
		if (ui::EditSetting({ "overlay-ab-test-interval", { .label = "A/B Test Interval" } }, interval, 0, cs::settings::Range{ 0, 10 }).changed)
			Attempt([&] { SetInterval(interval); });
	}

	void ABComparison::DrawCostControls()
	{
		{
			const dmui::DisabledScope disabled{ _testing || _restorePending };
			if (dmui::ui::Button("Measure CS cost"))
				Attempt([&] { StartCostRun(RunOptions{}); });
		}
		if (ScriptedRunning() && dmui::ui::Button("Stop Scripted Comparison"))
			Attempt([&] { SetInterval(0); });
		dmui::ui::TextWrapped("%s", CostHelp().c_str());
	}

	void ABComparison::DrawResults()
	{
		if (_testing) {
			const auto progress = GetProgress();
			dmui::ui::Text("Variant %s (%s): %.1f seconds left",
				_variantB ? "B" : "A", (_variantB ? _labelB : _labelA).c_str(), progress.windowSecondsLeft);
			if (progress.scripted)
				dmui::ui::Text("Window %d of %d", progress.window, progress.windows);
		}
		std::vector<std::string> changes;
		for (const auto& [feature, values] : _b.live) {
			const auto baseline = _a.live.find(feature);
			if (baseline == _a.live.end())
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
		const auto& ownershipA = _a.ownership;
		const auto& ownershipB = _b.ownership;
		if (ownershipA.enableShaders != ownershipB.enableShaders)
			changes.push_back(std::format("ShaderOwnership.enable_shaders: {} -> {}", ownershipA.enableShaders, ownershipB.enableShaders));
		for (const auto& target : engine::GetShaderInjectionTargets()) {
			if (ownershipA.targets[target.id] != ownershipB.targets[target.id])
				changes.push_back(std::format("ShaderOwnership.targets.{}: {} -> {}", target.name, ownershipA.targets[target.id], ownershipB.targets[target.id]));
		}
		if (!changes.empty()) {
			const auto title = std::format("Changes from {}", _labelA);
			if (const ui::Section section{ "overlay-ab-changes", title.c_str(), ui::Collapsible{ .count = changes.size(), .framed = false } }; section) {
				for (const auto& change : changes)
					dmui::ui::TextUnformatted(change.c_str());
			}
		}
		if (!_testError.empty())
			dmui::ui::Text("A/B error: %s", _testError.c_str());
		const auto summary = Summarize();
		if (!summary)
			return;
		if (_scripted && summary->comparable) {
			const auto margin = summary->deltaStdErr ? std::format(" \u00b1 {:.2f}", *summary->deltaStdErr) : std::string{};
			dmui::ui::Text("%s minus %s: %+.2f ms%s per frame (%.2f ms / %.1f FPS %s, %.2f ms / %.1f FPS %s)",
				_labelB.c_str(), _labelA.c_str(), summary->delta, margin.c_str(),
				summary->meanB, summary->fpsB, _labelB.c_str(), summary->meanA, summary->fpsA, _labelA.c_str());
		}
		dmui::ui::Text("%s: %d frames, %.1f seconds, %d excluded, %.1f%% valid", ValidityName(summary->validity).data(),
			summary->frames, summary->duration, summary->excluded, summary->validPercent);
		if (_scripted)
			return;
		if (!dmui::ui::BeginTable("ABResults", 7, dmui::ui::TableFlags::kSortable))
			return;
		const char* labels[]{ "Shader Type", "A Avg", "B Avg", "Delta", "A Median", "B Median", "Median Delta" };
		const auto sort = SortHeaders(labels, 3);
		auto rows = _aggregator.GetAggregatedResults();
		SortRows(rows, sort, [&sort](const auto& row) {
			switch (sort.column) {
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
				if (row.shaderType == -1 && row.frameCountA > 0 && row.frameCountB > 0)
					ui::Tooltip("A (%s): %.2f FPS; B (%s): %.2f FPS", _labelA.c_str(), Util::CalcFPS(row.meanA), _labelB.c_str(), Util::CalcFPS(row.meanB));
			}
		}
		dmui::ui::EndTable();
	}
}
