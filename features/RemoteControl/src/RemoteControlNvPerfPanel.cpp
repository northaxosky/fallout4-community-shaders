#include "RemoteControl.h"
#include "RemoteControlTools.h"

#include "Features/RemoteControl/NvPerf.h"
#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/Section.h"
#include "Utils/PathUtf8.h"

#include <DearModdingUI/Client.h>
#include <DearModdingUI/UI.h>

#include <array>
#include <cmath>
#include <sstream>

namespace cs::features::remote_control
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.remotecontrol");

		// UI state only: nothing here is saved.
		struct Panel
		{
			std::vector<char> defaultMetricEnabled;  // per NvPerf::DefaultMetrics() entry
			std::array<char, 512> extraMetrics{};    // comma-separated name[:kind]
			std::array<char, 512> ranges{};          // comma-separated range filters
			int samples = 1;
			int delayFrames = 0;
			int nestingLevels = 1;
			std::string requestError;
		};

		Panel g_panel;

		std::vector<std::string> SplitList(const char* a_text)
		{
			std::vector<std::string> parts;
			std::stringstream stream(a_text);
			for (std::string part; std::getline(stream, part, ',');) {
				const auto first = part.find_first_not_of(" \t");
				if (first == std::string::npos)
					continue;
				parts.push_back(part.substr(first, part.find_last_not_of(" \t") - first + 1));
			}
			return parts;
		}

		bool IsTimeMetric(const NvPerf::MetricRequest& a_metric)
		{
			return a_metric.name == "gpu__time_duration";
		}

		void Draw(std::string_view a_text, dmui::TextTone a_tone)
		{
			auto& client = host::HostClient::Get().Client();
			if (!dmui::DrawStyledText(client, a_text, { .tone = a_tone, .wrapped = true }))
				L->warn("DearModdingUI draw failed: {}", DMUI_ResultToString(client.LastResult()));
		}

		void RequestCapture(const std::vector<NvPerf::MetricRequest>& a_defaults)
		{
			auto& panel = g_panel;
			NvPerf::CaptureConfiguration configuration;
			for (std::size_t i = 0; i < a_defaults.size(); ++i) {
				if (panel.defaultMetricEnabled[i])
					configuration.metrics.push_back(a_defaults[i]);
			}
			panel.requestError.clear();
			for (const auto& text : SplitList(panel.extraMetrics.data())) {
				NvPerf::MetricRequest metric;
				if (!NvPerf::ParseMetric(text, metric, panel.requestError))
					return;
				configuration.metrics.push_back(std::move(metric));
			}
			if (configuration.metrics.empty()) {
				panel.requestError = "Select at least one metric.";
				return;
			}
			configuration.ranges = SplitList(panel.ranges.data());
			configuration.samples = static_cast<std::uint32_t>(panel.samples);
			configuration.delayFrames = static_cast<std::uint32_t>(panel.delayFrames);
			configuration.nestingLevels = static_cast<std::uint16_t>(panel.nestingLevels);
			std::uint64_t id = 0;
			NvPerf::RequestCapture(configuration, id, panel.requestError);
		}

		void DrawStatus(const NvPerf::Status& a_status)
		{
			switch (a_status.state) {
			case NvPerf::CaptureState::Waiting:
				Draw(std::format("Capture {}: waiting to start", a_status.captureId), dmui::TextTone::kStatusInfo);
				break;
			case NvPerf::CaptureState::Capturing:
				Draw(std::format("Capture {}: sample {}/{}, pass {}/{}", a_status.captureId, a_status.sample + 1, a_status.samples,
						 a_status.pass + 1, a_status.passes),
					dmui::TextTone::kStatusInfo);
				break;
			case NvPerf::CaptureState::Decoding:
				Draw(std::format("Capture {}: sample {}/{}, decoding", a_status.captureId, a_status.sample + 1, a_status.samples),
					dmui::TextTone::kStatusInfo);
				break;
			default:
				break;
			}
		}

		void DrawSummary(const NvPerf::CaptureResult& a_result)
		{
			const auto columns = static_cast<int>(a_result.metrics.size()) + 2;
			const auto height = dmui::ui::GetTextLineHeightWithSpacing() * (std::min)(static_cast<float>(a_result.summary.size()) + 2.0f, 20.0f);
			if (!dmui::ui::BeginTable("nvperf-summary", columns,
					dmui::ui::TableFlags::kBorders | dmui::ui::TableFlags::kRowBg | dmui::ui::TableFlags::kScrollX | dmui::ui::TableFlags::kScrollY |
						dmui::ui::TableFlags::kResizable,
					dmui::ui::Vec2{ 0.0f, height }))
				return;
			dmui::ui::TableSetupScrollFreeze(1, 1);
			dmui::ui::TableSetupColumn("Range", dmui::ui::TableColumnFlags::kWidthFixed, 260.0f);
			dmui::ui::TableSetupColumn("Per frame", dmui::ui::TableColumnFlags::kWidthFixed);
			for (const auto& metric : a_result.metrics) {
				const auto label = IsTimeMetric(metric) ? std::string("GPU time (ms)") : metric.name;
				dmui::ui::TableSetupColumn(label.c_str(), dmui::ui::TableColumnFlags::kWidthFixed);
			}
			dmui::ui::TableHeadersRow();
			for (const auto& entry : a_result.summary) {
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableNextColumn();
				dmui::ui::TextUnformatted(entry.name.c_str());
				(void)dmui::ui::TableNextColumn();
				dmui::ui::Text("%.3g", entry.occurrencesPerFrame);
				for (std::size_t m = 0; m < a_result.metrics.size(); ++m) {
					(void)dmui::ui::TableNextColumn();
					const double value = m < entry.values.size() ? entry.values[m] : std::nan("");
					if (std::isnan(value))
						dmui::ui::TextDisabled("-");
					else
						dmui::ui::Text("%.4g", IsTimeMetric(a_result.metrics[m]) ? value / 1.0e6 : value);
				}
			}
			dmui::ui::EndTable();
		}

		void DrawResult(const NvPerf::CaptureResult& a_result)
		{
			if (a_result.success)
				Draw(std::format("Capture {}: {} ranges, frames {}-{}, {} samples x {} passes", a_result.id, a_result.rangeResults.size(),
						 a_result.startFrame, a_result.endFrame, a_result.samples, a_result.passesPerSample),
					dmui::TextTone::kStatusSuccess);
			else
				Draw(std::format("Capture {} failed: {}", a_result.id, a_result.error), dmui::TextTone::kStatusError);
			for (const auto& metric : a_result.unsupportedMetrics)
				Draw(std::format("Unavailable on this GPU: {}", NvPerf::FormatMetric(metric)), dmui::TextTone::kStatusWarning);

			if (!a_result.summary.empty())
				DrawSummary(a_result);
			const auto csv = util::PathToUtf8(a_result.csvPath);
			dmui::ui::TextDisabled("Every range of every sample: %s", csv.c_str());
			if (dmui::ui::Button("Copy path"))
				dmui::ui::SetClipboardText(csv.c_str());
		}
	}

	// Probe on first open; the capture itself runs on the render thread later.
	void DrawNvPerfPanel()
	{
		const ui::Section section{ "nvperf", "Nsight Perf (DX11)" };
		if (!section)
			return;

		auto& panel = g_panel;
		dmui::ui::TextWrapped(
			"Measures NVIDIA GPU hardware counters (time, unit throughput, warp stalls) over the frame's perf events with the Nsight Perf SDK. "
			"Each frame is one profiler pass; a sample takes as many frames as its metrics need passes. "
			"Also drivable through the devbench tool communityshaders.nvperf.");

		NvPerf::Probe();
		const auto device = NvPerf::GetDeviceInfo();
		if (!device.built) {
			Draw("This build was compiled without Nsight Perf. Configure with --nvperf_sdk=<SDK folder>.", dmui::TextTone::kStatusWarning);
			return;
		}
		if (!device.gpuSupported) {
			Draw(std::format("Nsight Perf is unavailable: {}", device.error), dmui::TextTone::kStatusWarning);
			dmui::ui::TextDisabled("Library: %s", util::PathToUtf8(device.libraryPath).c_str());
			return;
		}
		Draw(std::format("{} (chip {})", device.deviceName, device.chipName), dmui::TextTone::kStatusSuccess);

		const auto defaults = NvPerf::DefaultMetrics();
		if (panel.defaultMetricEnabled.size() != defaults.size())
			panel.defaultMetricEnabled.assign(defaults.size(), 1);

		(void)dmui::ui::InputText("Ranges", panel.ranges.data(), panel.ranges.size());
		dmui::ui::TextDisabled(
			"Comma-separated perf event names; a trailing * matches every name with that prefix. "
			"Empty measures every outermost range. Engine passes (shadow maps, BSShaderAccumulator batches) "
			"only exist when Frame Annotations was enabled at startup.");

		if (const ui::Section metrics{ "nvperf-metrics", "Metrics", ui::Collapsible{ .count = defaults.size() } }; metrics) {
			for (std::size_t i = 0; i < defaults.size(); ++i) {
				bool enabled = panel.defaultMetricEnabled[i] != 0;
				const auto label = NvPerf::FormatMetric(defaults[i]);
				if (dmui::ui::Checkbox(label.c_str(), &enabled))
					panel.defaultMetricEnabled[i] = enabled ? 1 : 0;
			}
			(void)dmui::ui::InputText("Extra metrics", panel.extraMetrics.data(), panel.extraMetrics.size());
			dmui::ui::TextDisabled("Comma-separated name[:counter|ratio|throughput|active|elapsed|percycle].");
		}

		constexpr int kSamplesMin = 1, kSamplesMax = 16, kNestingMin = 1, kNestingMax = 4, kDelayMin = 0, kDelayMax = 600;
		(void)dmui::ui::SliderScalar("Samples", &panel.samples, &kSamplesMin, &kSamplesMax);
		(void)dmui::ui::SliderScalar("Nesting levels", &panel.nestingLevels, &kNestingMin, &kNestingMax);
		dmui::ui::TextDisabled("Selected ranges nested this deep are measured too. Each level multiplies the passes.");
		(void)dmui::ui::SliderScalar("Delay (frames)", &panel.delayFrames, &kDelayMin, &kDelayMax);

		const auto status = NvPerf::GetStatus();
		const bool busy = status.state == NvPerf::CaptureState::Waiting || status.state == NvPerf::CaptureState::Capturing ||
		                  status.state == NvPerf::CaptureState::Decoding;
		if (busy) {
			if (dmui::ui::Button("Cancel capture"))
				NvPerf::CancelCapture();
		} else if (dmui::ui::Button("Capture")) {
			RequestCapture(defaults);
		}
		if (!panel.requestError.empty())
			Draw(panel.requestError, dmui::TextTone::kError);
		DrawStatus(status);

		if (const auto result = NvPerf::LatestResult())
			DrawResult(*result);
	}
}
