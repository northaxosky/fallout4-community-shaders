#include "RemoteControlTools.h"

#include "Plugin.h"
#include "Render/FrameProfiler.h"
#include "Render/ShaderVariantCompilation.h"
#include "Render/TemporalPipeline.h"
#include "Utils/ShaderCache/ShaderCache.h"

#include <string>

namespace cs::features::remote_control
{
	namespace
	{
		Json State()
		{
			return {
				{ "plugin", std::string(Plugin::NAME) },
				{ "version", Plugin::VERSION.string(".") },
				{ "frame_count", render::profiling::FrameSequence() },
			};
		}

		// The render thread writes the profiler; read it at the frame boundary.
		Json Timings(const std::string& a_filter)
		{
			auto& profiler = render::profiling::GetProfiler();
			auto passes = Json::array();
			for (const auto& pass : profiler.GetStatistics()) {
				if (!pass.valid || (!a_filter.empty() && !pass.name.contains(a_filter)))
					continue;
				auto history = Json::array();
				for (std::uint32_t i = 0; i < pass.historyCount; ++i)
					history.push_back(pass.GetHistorySample(i));
				passes.push_back({ { "name", pass.name },
					{ "gpuMs", pass.gpuTimeMs },
					{ "gpuAvgMs", pass.avgMs },
					{ "gpuP95Ms", pass.p95Ms },
					{ "gpuP99Ms", pass.p99Ms },
					{ "cpuMs", pass.cpuTimeMs },
					{ "cpuAvgMs", pass.cpuAvgMs },
					{ "gpuHistory", std::move(history) } });
			}
			auto shaders = Json::array();
			for (const auto& shader : render::profiling::GetShaderTimings()) {
				if (!a_filter.empty() && !shader.name.contains(a_filter))
					continue;
				shaders.push_back({ { "name", shader.name }, { "calls", shader.calls }, { "ms", shader.milliseconds } });
			}
			const auto frame = profiler.GetFrameStats();
			Json result{
				{ "totalGpuMs", profiler.GetTotalTimeMs() },
				{ "totalCpuMs", profiler.GetCpuTotalTimeMs() },
				{ "frame_count", render::profiling::FrameSequence() },
				{ "frame", { { "ms", frame.lastMs }, { "avgMs", frame.avgMs }, { "p95Ms", frame.p95Ms }, { "p99Ms", frame.p99Ms }, { "samples", frame.count }, { "frameGeneration", render::TemporalPipeline::Get().GetFrameGenerationDiagnostics(false).active } } },
				{ "passes", std::move(passes) },
				{ "shaders", std::move(shaders) },
			};
			if (result["passes"].empty())
				result["hint"] = "GPU timing runs only while telemetry or the Performance Overlay is enabled";
			return result;
		}

		Json ShaderCache()
		{
			const auto batch = engine::GetShaderCompileBatch();
			const auto cache = shader_cache::GetShaderCacheCounters();
			return {
				{ "compiling", batch.active },
				{ "completedTasks", batch.completedTasks },
				{ "totalTasks", batch.totalTasks },
				{ "failedTasks", batch.failedTasks },
				{ "durationMs", batch.duration.count() },
				{ "cache", { { "hit", cache.hit }, { "absent", cache.absent }, { "stale", cache.stale }, { "rejected", cache.rejected }, { "written", cache.written } } },
				{ "frame_count", render::profiling::FrameSequence() },
			};
		}

		Json Handle(const Json& a_args)
		{
			const auto kind = a_args.value("kind", std::string{});
			if (kind.empty())
				return Error("missing required parameter 'kind'");
			if (kind == "state")
				return State();
			if (kind == "profiler")
				return OnFrame([filter = a_args.value("filter", std::string{})] { return Timings(filter); });
			if (kind == "shadercache")
				return ShaderCache();
			return Error("unknown kind", { { "kind", kind }, { "supported", Json::array({ "state", "shadercache", "profiler" }) } });
		}
	}

	const Tool kInspectTool{
		"communityshaders.inspect",
		R"({"description":"Read Community Shaders engine state. Kind-dispatched; the response is a JSON object. kind=state -> {plugin,version,frame_count}. kind=shadercache -> {compiling,completedTasks,totalTasks,failedTasks,durationMs,cache:{hit,absent,stale,rejected,written},frame_count}; the task counts describe the running runtime shader compile batch, else the last finished one (see the communityshaders.shaderRecompiled event). kind=profiler -> {totalGpuMs,totalCpuMs,frame_count,frame:{ms,avgMs,p95Ms,p99Ms,samples,frameGeneration} (engine-frame wall clock; presents run ~2x when frameGeneration),passes:[{name,gpuMs,gpuAvgMs,gpuP95Ms,gpuP99Ms,cpuMs,cpuAvgMs,gpuHistory:[...]}],shaders:[{name,calls,ms}]}; optional filter param matches pass and shader names.","readOnly":true,"inputSchema":{"type":"object","properties":{"kind":{"type":"string","enum":["state","shadercache","profiler"]},"filter":{"type":"string"}},"required":["kind"]}})",
		&Handle
	};
}
