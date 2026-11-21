#include "RemoteControlTools.h"

#include "PerformanceOverlay.h"
#include "Render/ShaderInjection.h"
#include "Settings/FeatureConfig.h"
#include "Settings/SettingsJson.h"

#include <format>

namespace cs::features::remote_control
{
	namespace
	{
		using Comparison = performance_overlay::ABComparison;

		// The variant is A and the current state is B; delta is B minus A.
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
				json.update({ { "currentMs", a_summary.meanB },
					{ "variantMs", a_summary.meanA },
					{ "deltaMs", a_summary.delta },
					{ "currentMedianMs", a_summary.medianB },
					{ "variantMedianMs", a_summary.medianA },
					{ "currentFps", a_summary.fpsB },
					{ "variantFps", a_summary.fpsA } });
				if (a_summary.deltaStdErr) {
					json["deltaStdErrMs"] = *a_summary.deltaStdErr;
					json["pairs"] = a_summary.pairs;
				}
			}
			return json;
		}

		Json Status(const PerformanceOverlay& a_overlay)
		{
			const auto& comparison = a_overlay.Comparison();
			const auto progress = comparison.GetProgress();
			Json status{ { "action", "status" }, { "running", progress.running }, { "scripted", progress.scripted }, { "restorePending", progress.restorePending } };
			if (progress.running && progress.scripted) {
				status["window"] = progress.window;
				status["windows"] = progress.windows;
				status["variant"] = progress.variantB ? "current" : "variant";
				status["windowSecondsLeft"] = progress.windowSecondsLeft;
			}
			if (!comparison.Error().empty())
				status["error"] = comparison.Error();
			return status;
		}

		Json Summary(const PerformanceOverlay& a_overlay)
		{
			const auto& comparison = a_overlay.Comparison();
			if (!comparison.LastRunWasScripted())
				return Error("no scripted comparison has run", { { "hint", "communityshaders.perf(action='start')" } });
			const auto summary = comparison.Summarize();
			if (!summary)
				return Error("the scripted comparison has no samples yet", { { "hint", "poll communityshaders.perf(action='status')" } });
			const bool running = comparison.Testing();
			return { { "action", "summary" }, { "running", running }, { "final", !running }, { "summary", SummaryJson(*summary) } };
		}

		std::expected<int, Json> IntParam(const Json& a_args, const char* a_key, int a_min, int a_max, int a_default)
		{
			if (!a_args.contains(a_key))
				return a_default;
			const auto& value = a_args[a_key];
			if (!value.is_number_integer() || value.get<long long>() < a_min || value.get<long long>() > a_max)
				return std::unexpected(Error(std::format("'{}' must be an integer from {} to {}", a_key, a_min, a_max)));
			return value.get<int>();
		}

		std::expected<Comparison::RunOptions, Json> ParseOptions(const Json& a_args)
		{
			Comparison::RunOptions options;
			const auto window = IntParam(a_args, "windowSeconds", 1, 60, options.windowSeconds);
			if (!window)
				return std::unexpected(window.error());
			const auto cycles = IntParam(a_args, "cycles", 1, 100, options.cycles);
			if (!cycles)
				return std::unexpected(cycles.error());
			options.windowSeconds = *window;
			options.cycles = *cycles;
			if (a_args.contains("settleSeconds")) {
				const auto& settle = a_args["settleSeconds"];
				if (!settle.is_number())
					return std::unexpected(Error("'settleSeconds' must be a number"));
				options.settleSeconds = settle.get<double>();
			}
			return options;
		}

		std::expected<feature_config::ShaderOwnershipConfig, Json> ParseOwnership(const Json& a_ownership)
		{
			if (!a_ownership.is_object())
				return std::unexpected(Error("'ownership' must be an object"));
			auto values = settings::JsonToToml(a_ownership);
			if (!values)
				return std::unexpected(Error("'ownership' is not valid settings", { { "detail", std::move(values.error()) } }));
			auto parsed = feature_config::ParseShaderOwnership(toml::table{ { "shader_ownership", std::move(*values) } }, engine::GetShaderOwnershipConfig());
			if (!parsed.valid)
				return std::unexpected(Error(std::move(parsed.error)));
			return parsed.config;
		}

		std::expected<Comparison::Variant, Json> ParseVariant(const Json& a_args)
		{
			Comparison::Variant variant;
			if (a_args.contains("settings")) {
				if (!a_args["settings"].is_object())
					return std::unexpected(Error("'settings' must be an object of {shortName: {key: value}}"));
				for (const auto& [shortName, values] : a_args["settings"].items()) {
					auto parsed = ParseFeatureSettings(shortName, values);
					if (!parsed)
						return std::unexpected(std::move(parsed.error()));
					variant.settings[parsed->feature] = std::move(parsed->values);
				}
			}
			if (a_args.contains("ownership")) {
				auto ownership = ParseOwnership(a_args["ownership"]);
				if (!ownership)
					return std::unexpected(std::move(ownership.error()));
				variant.ownership = *ownership;
			}
			return variant;
		}

		Json Start(PerformanceOverlay& a_overlay, const Json& a_args)
		{
			const auto options = ParseOptions(a_args);
			if (!options)
				return options.error();
			const auto variant = ParseVariant(a_args);
			if (!variant)
				return variant.error();
			if (auto started = a_overlay.StartComparison(*variant, *options); !started)
				return Error(std::move(started.error()));
			return Status(a_overlay);
		}

		Json Apply(const std::string& a_action, const Json& a_args)
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
			if (a_action == "start")
				return Start(*overlay, a_args);
			if (!comparison.Testing() && !comparison.RestorePending())
				return Error("no comparison is running");
			if (!comparison.LastRunWasScripted())
				return Error("the running comparison is not a scripted comparison", { { "hint", "stop it in the Performance Overlay" } });
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
			return OnFrame([action, args = a_args] { return Apply(action, args); });
		}
	}

	const Tool kPerfTool{
		"communityshaders.perf",
		R"({"description":"Compare the frame time of the current state against a variant of it by alternating them in interleaved fixed-length windows in one session. Action-dispatched. start: begins a run from the current live state. The variant is given by 'settings' (live setting overrides per feature shortName, validated as communityshaders.feature set; a toggleable feature's framework 'enabled' switch is a setting too) and/or 'ownership' (shader ownership: enable_shaders and targets by name, both optional; omitted members keep their current values, unknown targets are refused). With neither, the variant is custom shaders OFF (stock shaders bound), which must currently be on. windowSeconds (integer 1-60, default 3) is the full window and includes the settle period; settleSeconds (number >= 0.25 and < windowSeconds, default 0.25) drops samples after each switch; cycles (integer 1-100, default 10) is the number of current/variant pairs. The run advances only from the DearModdingUI frame tick, so it needs the DearModdingUI host ready, and it is refused when the Performance Overlay is not loaded or any A/B comparison is active. It restores the current state when it ends or is stopped; while it runs, live settings, presets and the shader ownership controls are locked. Replies with the status. status: {running,scripted,restorePending} plus {window,windows,variant:'current'|'variant',windowSecondsLeft} while running, and any error. summary: {comparable,frames,excludedFrames,durationSeconds,windows,validity:'Valid'|'Marginal'|'Insufficient'|'Frame-capped' (most frames ran at the refresh rate or under frame generation, so frame time is not GPU cost)} plus {currentMs,variantMs,deltaMs,currentMedianMs,variantMedianMs,currentFps,variantFps} only when comparable (both were sampled) and {deltaStdErrMs,pairs} when at least two current/variant window pairs completed, with final=false while running. deltaMs is mean frame time of the current state minus the variant (positive: current is slower); deltaStdErrMs is its standard error over the window pairs. For the default run the variant still includes CS hooks, per-draw lookups and every feature that does not follow shader ownership, so it reads lower than removing the plugin. stop: ends a running scripted run early, restores the current state, and returns its summary. The run also stops by itself and logs one summary line.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["start","status","summary","stop"]},"settings":{"type":"object","description":"{shortName:{key:value}} live overrides for the variant","additionalProperties":{"type":"object"}},"ownership":{"type":"object","properties":{"enable_shaders":{"type":"boolean"},"targets":{"type":"object","additionalProperties":{"type":"boolean"}}}},"windowSeconds":{"type":"integer","minimum":1,"maximum":60},"cycles":{"type":"integer","minimum":1,"maximum":100},"settleSeconds":{"type":"number","minimum":0.25}},"required":["action"]}})",
		&Handle
	};
}
