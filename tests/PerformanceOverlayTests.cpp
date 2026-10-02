#include "Log.h"
#include "Render/Annotation.h"
#include "Render/FrameProfiler.h"

#include <iostream>

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

	// FO4 gating and nesting only; query readback is upstream Profiler behaviour.
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
			ScopedEvent leaf("Test::Leaf");
			{
				// A rejected nested scope must not end the active leaf.
				ScopedEvent nested("Test::Nested");
			}
			CHECK(!profiling::BeginPass("Test::WhileLeafActive"));
		}
		CHECK(profiling::BeginPass("Test::AfterLeaf"));
		profiling::EndPass();
		profiling::MarkEngineFrame(false);
		CHECK(!profiling::BeginPass("Test::DisabledAgain"));
	}
}

int main()
{
	TestProfilerConsumers();
	if (failures)
		return 1;
	std::cout << "PerformanceOverlay tests passed\n";
	return 0;
}