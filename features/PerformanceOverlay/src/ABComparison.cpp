#include "ABComparison.h"

#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "OverlayTable.h"
#include "Settings/LiveSettings.h"
#include "Shared/PerfUtils.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace cs::features::performance_overlay
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.performanceoverlay");

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

	void ABComparison::CaptureBaseline()
	{
		_baseline = Capture();
		_test.clear();
		_aggregator.Clear();
		_testError.clear();
	}

	void ABComparison::Abort(std::string_view a_reason) noexcept
	{
		_testError = a_reason;
		L->error("A/B test stopped: {}", _testError);
		try {
			SetInterval(0);
		} catch (const std::exception& error) {
			_testError += std::format("; TEST restoration failed: {}", error.what());
			L->error("{}", _testError);
			_testing = false;
			_restorePending = true;
			cs::settings::liveComparisonActive = true;
			_testInterval = 0;
			_aggregator.OnTestEnd();
		}
		Menu::ShowToast(_testError, 8.0, DMUI_STATUS_SEVERITY_ERROR, std::string(_owner.GetDisplayName()));
	}

	ABComparison::Snapshot ABComparison::Capture()
	{
		Snapshot snapshot;
		for (auto* feature : FeatureManager::Get().GetAll()) {
			const auto& access = feature->GetLiveSettingsAccess();
			if (!feature->IsHealthy() || !access.snapshot)
				continue;
			auto values = access.snapshot();
			for (const auto key : access.notComparable)
				values.erase(key);
			snapshot.emplace(feature, std::move(values));
		}
		return snapshot;
	}

	void ABComparison::Apply(const Snapshot& a_snapshot, bool a_restoring)
	{
		std::vector<cs::settings::PreparedLiveSettings> prepared;
		for (const auto& [feature, values] : a_snapshot) {
			if (!a_restoring && !feature->IsHealthy())
				throw std::runtime_error(std::format("{} is no longer healthy", feature->GetName()));
			std::string error;
			auto value = FeatureManager::Get().PrepareLiveSettings(*feature, values, "ABComparison::Finalize", error);
			if (!value)
				throw std::runtime_error(std::format("{}: {}", feature->GetName(), error));
			if (a_restoring && !feature->IsHealthy())
				value->finalize = {};
			prepared.push_back(std::move(*value));
		}
		cs::settings::ApplyPreparedLiveSettings(prepared);
	}

	void ABComparison::SetInterval(int a_interval)
	{
		if (!a_interval) {
			if (_testing || _restorePending) {
				Apply(_test, true);
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
			auto test = Capture();
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

	void ABComparison::Update(double a_now, const std::vector<DrawCallRow>& a_rows)
	{
		if (!_testing)
			return;
		for (const auto& [feature, values] : _test) {
			(void)values;
			if (!feature->IsHealthy())
				throw std::runtime_error(std::format("{} was quarantined", feature->GetName()));
		}
		_aggregator.OnFrame(a_rows);
		if (a_now - _lastSwitch > static_cast<double>(_testInterval)) {
			Apply(_variantB ? _baseline : _test);
			_variantB = !_variantB;
			_aggregator.OnABSwitch(_variantB ? ABVariant::B : ABVariant::A);
			_lastSwitch = a_now;
		}
	}

	void ABComparison::DrawControls()
	{
		dmui::ui::TextWrapped("Capture USER before editing TEST. Set the interval to 0 to restore TEST.");
		if (_restorePending && dmui::ui::Button("Retry TEST Restoration"))
			Abort("Retrying TEST restoration");
		dmui::ui::BeginDisabled(_testing || _restorePending);
		if (dmui::ui::Button("Capture USER Baseline"))
			CaptureBaseline();
		dmui::ui::EndDisabled();
		int interval = _testInterval;
		if (ui::EditSetting({ "overlay-ab-test-interval", { .label = "A/B Test Interval" } }, interval, 0, cs::settings::Range{ 0, 10 }).changed) {
			try {
				SetInterval(interval);
			} catch (const std::exception& error) {
				_testError = error.what();
				Menu::ShowToast(_testError, 6.0, DMUI_STATUS_SEVERITY_ERROR, std::string(_owner.GetDisplayName()));
			}
		}
		DrawResults();
	}

	void ABComparison::DrawResults()
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
		bool hasSamplesA = false;
		bool hasSamplesB = false;
		for (const auto& interval : _aggregator.GetIntervals()) {
			excluded += interval.excludedFrames;
			hasSamplesA = hasSamplesA || (interval.variant == ABVariant::A && !interval.frameRows.empty());
			hasSamplesB = hasSamplesB || (interval.variant == ABVariant::B && !interval.frameRows.empty());
		}
		const bool hasBothVariants = hasSamplesA && hasSamplesB;
		const float validPercent = frames + excluded > 0 ? 100.0f * static_cast<float>(frames) / static_cast<float>(frames + excluded) : 0.0f;
		const char* validity = hasBothVariants && frames >= kMinimumSamplesForValidity && duration >= kMinimumTestDuration &&
		                               validPercent >= kMinimumValidFramesPercent ?
		                           "Valid" :
		                       hasBothVariants && frames >= kMinimumSamplesForMarginal && duration >= kMinimumDurationForMarginal ? "Marginal" :
		                                                                                                                            "Insufficient";
		dmui::ui::Text("%s: %d frames, %.1f seconds, %d excluded, %.1f%% valid", validity, frames, duration, excluded, validPercent);
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
					ui::Tooltip("A (USER): %.2f FPS; B (TEST): %.2f FPS", Util::CalcFPS(row.meanA), Util::CalcFPS(row.meanB));
			}
		}
		dmui::ui::EndTable();
	}
}
