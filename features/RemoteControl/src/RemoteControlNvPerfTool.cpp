#include "RemoteControl.h"
#include "RemoteControlTools.h"

#include "Features/RemoteControl/NvPerf.h"
#include "Render/FrameProfiler.h"
#include "Utils/PathUtf8.h"

#include <algorithm>
#include <cmath>

namespace cs::features::remote_control
{
	namespace
	{
		const char* CaptureStateName(NvPerf::CaptureState a_state)
		{
			switch (a_state) {
			case NvPerf::CaptureState::Waiting:
				return "waiting";
			case NvPerf::CaptureState::Capturing:
				return "capturing";
			case NvPerf::CaptureState::Decoding:
				return "decoding";
			case NvPerf::CaptureState::Done:
				return "done";
			case NvPerf::CaptureState::Failed:
				return "failed";
			default:
				return "idle";
			}
		}

		Json StatusJson(const NvPerf::Status& a_status)
		{
			return { { "state", CaptureStateName(a_status.state) }, { "captureId", a_status.captureId }, { "sample", a_status.sample },
				{ "samples", a_status.samples }, { "pass", a_status.pass }, { "passes", a_status.passes }, { "error", a_status.error } };
		}

		Json MetricJson(const NvPerf::MetricRequest& a_metric)
		{
			return { { "metric", NvPerf::FormatMetric(a_metric) }, { "label", NvPerf::MetricLabel(a_metric) } };
		}

		// Per-frame values keyed by metric label; incomplete values are null.
		Json CaptureResultJson(const NvPerf::CaptureResult& a_result, bool a_detail)
		{
			auto metrics = Json::array();
			std::vector<std::string> labels;
			for (const auto& metric : a_result.metrics) {
				metrics.push_back(MetricJson(metric));
				labels.push_back(NvPerf::MetricLabel(metric));
			}
			auto unsupported = Json::array();
			for (const auto& metric : a_result.unsupportedMetrics)
				unsupported.push_back(MetricJson(metric));
			const auto values = [&](const std::vector<double>& a_values) {
				auto out = Json::object();
				for (std::size_t m = 0; m < labels.size() && m < a_values.size(); ++m)
					out[labels[m]] = std::isnan(a_values[m]) ? Json(nullptr) : Json(a_values[m]);
				return out;
			};
			auto summary = Json::array();
			for (const auto& entry : a_result.summary)
				summary.push_back({ { "range", entry.name }, { "perFrame", entry.occurrencesPerFrame }, { "incomplete", entry.incomplete },
					{ "values", values(entry.values) } });
			Json out{ { "captureId", a_result.id }, { "success", a_result.success }, { "error", a_result.error }, { "chip", a_result.chipName },
				{ "startFrame", a_result.startFrame }, { "endFrame", a_result.endFrame }, { "samples", a_result.samples },
				{ "passesPerSample", a_result.passesPerSample }, { "droppedRanges", a_result.droppedRanges },
				{ "droppedTraceBytes", a_result.droppedTraceBytes }, { "ranges", a_result.ranges }, { "metrics", std::move(metrics) },
				{ "unsupportedMetrics", std::move(unsupported) }, { "summary", std::move(summary) }, { "csvPath", util::PathToUtf8(a_result.csvPath) } };
			if (a_detail) {
				auto ranges = Json::array();
				for (const auto& range : a_result.rangeResults)
					ranges.push_back({ { "sample", range.sample }, { "range", range.name }, { "path", range.path }, { "occurrence", range.occurrence },
						{ "values", values(range.values) } });
				out["rangeResults"] = std::move(ranges);
			}
			return out;
		}

		// Captures run on the render thread over later frames; nothing marshals here.
		Json Handle(const Json& a_args)
		{
			const auto action = a_args.value("action", std::string{});
			if (action.empty())
				return Error("missing required parameter 'action'");

			if (action == "status") {
				NvPerf::Probe();
				const auto device = NvPerf::GetDeviceInfo();
				return { { "built", device.built }, { "available", device.available }, { "gpuSupported", device.gpuSupported },
					{ "device", device.deviceName }, { "chip", device.chipName }, { "deviceError", device.error },
					{ "library", util::PathToUtf8(device.libraryPath) }, { "capture", StatusJson(NvPerf::GetStatus()) } };
			}

			if (action == "metrics") {
				auto metrics = Json::array();
				for (const auto& metric : NvPerf::DefaultMetrics())
					metrics.push_back(MetricJson(metric));
				return { { "defaults", std::move(metrics) },
					{ "kinds", Json::array({ "counter", "ratio", "throughput", "active", "elapsed", "percycle" }) } };
			}

			if (action == "capture") {
				NvPerf::CaptureConfiguration configuration;
				if (a_args.contains("metrics")) {
					if (!a_args["metrics"].is_array())
						return Error("metrics must be an array of \"name[:kind]\" strings");
					for (const auto& entry : a_args["metrics"]) {
						if (!entry.is_string())
							return Error("metrics must be an array of \"name[:kind]\" strings");
						NvPerf::MetricRequest metric;
						std::string error;
						if (!NvPerf::ParseMetric(entry.get<std::string>(), metric, error))
							return Error(std::move(error));
						configuration.metrics.push_back(std::move(metric));
					}
				}
				if (a_args.contains("ranges")) {
					if (!a_args["ranges"].is_array())
						return Error("ranges must be an array of strings");
					for (const auto& entry : a_args["ranges"]) {
						if (!entry.is_string() || entry.get<std::string>().empty())
							return Error("ranges must be an array of non-empty strings");
						configuration.ranges.push_back(entry.get<std::string>());
					}
				}
				configuration.samples = a_args.value("samples", 1u);
				configuration.delayFrames = a_args.value("delayFrames", 0u);
				configuration.nestingLevels = static_cast<std::uint16_t>(std::clamp(a_args.value("nestingLevels", 1u), 0u, 0xFFFFu));
				std::uint64_t captureId = 0;
				std::string error;
				if (!NvPerf::RequestCapture(configuration, captureId, error))
					return Error(std::move(error));
				return { { "queued", true }, { "captureId", captureId }, { "enqueued_at_frame", render::profiling::FrameSequence() } };
			}

			if (action == "cancel") {
				NvPerf::CancelCapture();
				return { { "cancelRequested", true }, { "capture", StatusJson(NvPerf::GetStatus()) } };
			}

			if (action == "result") {
				const auto status = NvPerf::GetStatus();
				const auto result = NvPerf::LatestResult();
				const std::uint64_t wanted = a_args.value("captureId", result ? result->id : status.captureId);
				if (result && result->id == wanted)
					return CaptureResultJson(*result, a_args.value("detail", false));
				if (status.captureId == wanted && wanted)
					return { { "pending", true }, { "capture", StatusJson(status) } };
				return Error("no such capture", { { "captureId", wanted } });
			}

			return Error("unknown action (status|metrics|capture|cancel|result)", { { "action", action } });
		}
	}

	void SubscribeNvPerfEvents()
	{
		NvPerf::SetCompletionCallback([](const NvPerf::CaptureResult& a_result) {
			RemoteControl::GetSingleton()->EmitEvent("communityshaders.nvperf.completed",
				{ { "captureId", a_result.id }, { "success", a_result.success }, { "error", a_result.error } });
		});
	}

	const Tool kNvPerfTool{
		"communityshaders.nvperf",
		R"({"description":"NVIDIA Nsight Perf GPU hardware counters per render pass (D3D11; builds with the nvperf_sdk option, NVIDIA GPUs only). Ranges are the perf events Community Shaders emits (engine passes need Frame Annotations enabled at startup). Action-dispatched. status -> {built,available,gpuSupported,device,chip,deviceError,library,capture:{state,captureId,sample,samples,pass,passes,error}}. metrics -> {defaults:[{metric,label}],kinds}. capture: optional metrics (array of name[:counter|ratio|throughput|active|elapsed|percycle] strings, default the defaults), ranges (array of names, trailing * = prefix; default every outermost range), samples (1-64, default 1), delayFrames (default 0), nestingLevels (1-8, default 1; each level multiplies the passes) -> {queued,captureId}; runs over the next frames, one profiler pass per frame. cancel: stop the running capture. result: optional captureId (default latest), detail (boolean: include every range of every sample) -> {pending,capture} while running, else {captureId,success,error,chip,startFrame,endFrame,samples,passesPerSample,metrics:[{metric,label}],unsupportedMetrics,summary:[{range,perFrame,incomplete,values:{label:value}}],csvPath}. Summary values are per frame: counters summed (gpu__time_duration in ns), rates weighted by range time. Event communityshaders.nvperf.completed {captureId,success,error} fires when a capture ends.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","metrics","capture","cancel","result"]},"metrics":{"type":"array","items":{"type":"string"}},"ranges":{"type":"array","items":{"type":"string"}},"samples":{"type":"number"},"delayFrames":{"type":"number"},"nestingLevels":{"type":"number"},"captureId":{"type":"number"},"detail":{"type":"boolean"}},"required":["action"]}})",
		&Handle
	};
}
