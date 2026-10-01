#include <Features/PerformanceOverlay/ABTesting/ABTestAggregator.h>
#include <Profiler.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>

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
	TestQueryCollectionAndRetirement();
	TestAggregationWithOutliers();
	if (failures)
		return 1;
	std::cout << "PerformanceOverlay tests passed\n";
	return 0;
}
