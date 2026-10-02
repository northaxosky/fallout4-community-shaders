#include "Log.h"
#include "Render/Annotation.h"
#include "Render/FrameProfiler.h"
#include <Features/PerformanceOverlay/ABTesting/ABTestAggregator.h>
#include <Profiler.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>

namespace cs::log
{
	spdlog::logger* Get(const char*)
	{
		return spdlog::default_logger_raw();
	}
}

namespace
{
	int failures{};

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": " << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	void TestProfilerConsumers()
	{
		namespace profiling = cs::render::profiling;
		using cs::render::annotation::ScopedEvent;
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		const auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put());
		CHECK(SUCCEEDED(result));
		if (FAILED(result))
			return;
		profiling::InitializeD3D11(device.get(), context.get());
		profiling::SetEnabled(true);
		profiling::MarkEngineFrame(false);
		CHECK(profiling::BeginPass("Test::OverlayOnly"));
		profiling::EndPass();
		profiling::SetEnabled(false);
		profiling::MarkEngineFrame(false);
		CHECK(!profiling::BeginPass("Test::Disabled"));
		CHECK(profiling::GetProfiler().GetResults().empty());
		profiling::MarkEngineFrame(true);
		{
			ScopedEvent group("Test::Group", false);
			for (int i = 0; i < 2; ++i) {
				ScopedEvent leaf("Test::Repeated");
				// A rejected nested scope must not end the active leaf.
				ScopedEvent nested("Test::Nested");
			}
			ScopedEvent sibling("Test::Sibling");
		}
		bool collected{};
		for (int frame = 0; frame < 12; ++frame) {
			profiling::MarkEngineFrame(true);
			context->Flush();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			const auto& results = profiling::GetProfiler().GetResults();
			if (results.empty())
				continue;
			CHECK(results.size() == 2);
			const auto repeated = std::ranges::find(results, "Test::Repeated", &Profiler::TimerResult::name);
			const auto sibling = std::ranges::find(results, "Test::Sibling", &Profiler::TimerResult::name);
			CHECK(repeated != results.end() && sibling != results.end());
			if (repeated != results.end() && !collected) {
				CHECK(repeated->historyCount == 2);
				CHECK(std::abs(repeated->gpuTimeMs -
							   (repeated->GetHistorySample(0) + repeated->GetHistorySample(1))) < 0.0001f);
			}
			collected = true;
		}
		CHECK(collected);
		profiling::MarkEngineFrame(false);
		CHECK(!profiling::BeginPass("Test::DisabledAgain"));
		CHECK(profiling::GetProfiler().GetResults().empty());
		// Re-enabling starts a fresh query ring, not stale pre-disable results.
		profiling::MarkEngineFrame(true);
		CHECK(profiling::BeginPass("Test::Reenabled"));
		profiling::EndPass();
		CHECK(profiling::GetProfiler().GetResults().empty());
		profiling::MarkEngineFrame(false);
	}

	void TestQueryCollectionAndRetirement()
	{
		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		const auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.put(), nullptr, context.put());
		CHECK(SUCCEEDED(result));
		if (FAILED(result))
			return;
		Profiler profiler;
		profiler.Initialize(device.get(), context.get());
		bool collected{};
		for (std::uint32_t frame = 0; frame < Profiler::kTimerRetireFrames + 12; ++frame) {
			profiler.BeginFrame();
			if (!frame) {
				profiler.BeginPass("Test::Once");
				profiler.EndPass();
			}
			profiler.EndFrame();
			context->Flush();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			for (const auto& timing : profiler.GetResults()) {
				CHECK(timing.name == "Test::Once" && timing.valid);
				CHECK(timing.historyCount == 1 && timing.gpuTimeMs >= 0.0f && timing.cpuTimeMs >= 0.0f);
				CHECK(std::isfinite(timing.avgMs) && std::isfinite(timing.cpuAvgMs));
				collected = true;
			}
		}
		CHECK(collected);
		CHECK(profiler.GetResults().empty());
		profiler.Release();
	}

	void TestAggregationWithOutliers()
	{
		ABTestAggregator aggregator;
		const auto rows = [](float a_total, float a_shader) {
			return std::vector<DrawCallRow>{
				{ "FO4 Family", 11, 2, a_shader, 0.0f, a_shader / 2.0f, "", true, {}, {} },
				{ "Total:", -1, 2, a_total, 100.0f, a_total / 2.0f, "", true, {}, {} }
			};
		};
		aggregator.OnABSwitch(ABVariant::B);
		for (int frame = 0; frame < 12; ++frame)
			aggregator.OnFrame(rows(20.0f, 4.0f));
		aggregator.OnFrame(rows(120.0f, 100.0f));
		aggregator.OnABSwitch(ABVariant::A);
		for (int frame = 0; frame < 12; ++frame)
			aggregator.OnFrame(rows(10.0f, 2.0f));
		aggregator.OnTestEnd();
		const auto results = aggregator.GetAggregatedResults();
		const auto family = std::ranges::find(results, 11, &AggregatedDrawCallStats::shaderType);
		CHECK(family != results.end());
		if (family != results.end()) {
			CHECK(family->meanA == 2.0f && family->meanB == 4.0f && family->delta == 2.0f);
			CHECK(family->medianA == 2.0f && family->medianB == 4.0f);
			CHECK(family->frameCountA == 12 && family->frameCountB == 12);
		}
		CHECK(aggregator.GetTotalFrameCount() == 24);
		CHECK(aggregator.GetIntervals()[0].excludedFrames == 1);
	}
}

int main()
{
	TestProfilerConsumers();
	TestQueryCollectionAndRetirement();
	TestAggregationWithOutliers();
	if (failures)
		return 1;
	std::cout << "PerformanceOverlay tests passed\n";
	return 0;
}
