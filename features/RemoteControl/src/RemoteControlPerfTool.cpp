#include "RemoteControlTools.h"

#include "PerformanceOverlay.h"

namespace cs::features::remote_control
{
	namespace
	{
		using Comparison = performance_overlay::ABComparison;

		// B is custom shaders on, A is off; cost is on minus off, set only when both were sampled.
		Json SummaryJson(const Comparison::Summary& a_summary)
		{
			Json json{
				{ "comparable", a_summary.comparable },
				{ "frames", a_summary.frames },
				{ "excludedFrames", a_summary.excluded },
				{ "durationSeconds", a_summary.duration },
				{ "windows", a_summary.windows },
				{ "validity", std::string(Comparison::ValidityName(a_summary.validity)) }
			};
			if (a_summary.comparable) {
				json.update({ { "costMs", a_summary.delta },
					{ "onMs", a_summary.meanB },
					{ "offMs", a_summary.meanA },
					{ "onMedianMs", a_summary.medianB },
					{ "offMedianMs", a_summary.medianA },
					{ "onFps", a_summary.fpsB },
					{ "offFps", a_summary.fpsA } });
			}
			return json;
		}

		Json Status(const PerformanceOverlay& a_overlay)
		{
			const auto& comparison = a_overlay.Comparison();
			const auto progress = comparison.GetProgress();
			Json status{ { "action", "status" }, { "running", progress.running }, { "costRun", progress.costRun }, { "restorePending", progress.restorePending } };
			if (progress.running && progress.costRun) {
				status["window"] = progress.window;
				status["windows"] = progress.windows;
				status["variant"] = progress.variantB ? "on" : "off";
				status["windowSecondsLeft"] = progress.windowSecondsLeft;
			}
			if (!comparison.Error().empty())
				status["error"] = comparison.Error();
			return status;
		}

		Json Summary(const PerformanceOverlay& a_overlay)
		{
			const auto& comparison = a_overlay.Comparison();
			if (!comparison.LastRunWasCost())
				return Error("no cost measurement has run", { { "hint", "communityshaders.perf(action='start')" } });
			const auto summary = comparison.Summarize();
			if (!summary)
				return Error("the cost measurement has no samples yet", { { "hint", "poll communityshaders.perf(action='status')" } });
			const bool running = comparison.Testing();
			return { { "action", "summary" }, { "running", running }, { "final", !running }, { "summary", SummaryJson(*summary) } };
		}

		Json Apply(const std::string& a_action)
		{
			auto* overlay = PerformanceOverlay::GetSingleton();
			if (!overlay->IsLoaded() || !overlay->IsHealthy()) {
				return Error("Performance Overlay is not loaded",
					{ { "hint", "communityshaders.feature(action='list') shows PerformanceOverlay.loaded" } });
			}
			const auto& comparison = overlay->Comparison();
			if (a_action == "status")
				return Status(*overlay);
			if (a_action == "summary")
				return Summary(*overlay);
			if (a_action == "start") {
				if (auto started = overlay->StartCostMeasurement(); !started)
					return Error(std::move(started.error()));
				return Status(*overlay);
			}
			if (!comparison.Testing() && !comparison.RestorePending())
				return Error("no comparison is running");
			if (!comparison.LastRunWasCost())
				return Error("the running comparison is not a cost measurement", { { "hint", "stop it in the Performance Overlay" } });
			if (auto stopped = overlay->StopComparison(); !stopped)
				return Error("the comparison could not be stopped", { { "detail", std::move(stopped.error()) } });
			Json result{ { "action", "stop" }, { "stopped", true } };
			if (const auto summary = comparison.LastSummary())
				result["summary"] = SummaryJson(*summary);
			return result;
		}

		Json Handle(const Json& a_args)
		{
			const auto action = a_args.value("action", std::string{});
			if (action != "start" && action != "status" && action != "summary" && action != "stop") {
				return action.empty() ? Error("missing required parameter 'action'") :
				                        Error("unknown action (start|status|summary|stop)", { { "action", action } });
			}
			return OnFrame([action] { return Apply(action); });
		}
	}

	const Tool kPerfTool{
		"communityshaders.perf",
		R"({"description":"Measure the frame-time cost of Community Shaders by alternating custom shaders ON and OFF (stock shaders bound) in interleaved fixed-length windows in one session. Action-dispatched. start: begins a run of a fixed number of on/off cycles from the current live state, which must have custom shaders on; replies with the status. The run advances only from the DearModdingUI frame tick, so it needs the DearModdingUI host ready, and it is refused when the Performance Overlay is not loaded or any A/B comparison is active. While it runs, live settings, presets and the shader ownership controls are locked. status: {running,costRun,restorePending} plus {window,windows,variant:'on'|'off',windowSecondsLeft} while running, and any error. summary: {comparable,frames,excludedFrames,durationSeconds,windows,validity:'Valid'|'Marginal'|'Insufficient'} plus {costMs,onMs,offMs,onMedianMs,offMedianMs,onFps,offFps} only when comparable (both on and off were sampled), with final=false while running; costMs is mean frame time on minus off. Off still includes CS hooks, per-draw lookups and every feature that does not follow shader ownership, so it reads lower than removing the plugin. stop: ends a running cost run early, restores the state it started from, and returns its summary. The run also stops by itself and logs one cost line.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["start","status","summary","stop"]}},"required":["action"]}})",
		&Handle
	};
}
