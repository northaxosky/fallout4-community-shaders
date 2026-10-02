#pragma once

#include <Profiler.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace cs::render::profiling
{
	// Records the engine-owned device; query allocation waits for an enabled telemetry frame.
	void InitializeD3D11(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context) noexcept;

	// Marks the engine's completed deferred-composite frame. Generated D3D12
	// presentation frames are intentionally outside this boundary.
	void MarkEngineFrame(bool a_telemetryEnabled) noexcept;

	Profiler& GetProfiler() noexcept;
	// Controls overlay CPU draw sampling only; GPU queries follow telemetry at the frame boundary.
	void SetEnabled(bool a_enabled) noexcept;
	bool BeginPass(std::string_view a_name);
	void EndPass() noexcept;

	// Empty prefix publishes fully qualified annotation names; feature prefixes preserve local fields.
	template <class Sink>
	void CollectPassTimings(Sink& a_sink, std::string_view a_prefix = {})
	{
		std::uint32_t count{};
		for (const auto& result : GetProfiler().GetResults()) {
			if (!result.valid || !result.name.starts_with(a_prefix))
				continue;
			const auto pass = result.name.substr(a_prefix.size());
			a_sink.Field(pass + "_gpu_ms", result.gpuTimeMs)
				.Field(pass + "_cpu_ms", result.cpuTimeMs);
			++count;
		}
		a_sink.Field("timing_passes", count);
	}

	struct ShaderTiming
	{
		int type;
		std::string name;
		float calls{};
		float milliseconds{};
	};

	void SetShaderFamily(int a_type, std::string_view a_name);
	void RecordDraw();
	std::span<const ShaderTiming> GetShaderTimings() noexcept;
	std::uint64_t FrameSequence() noexcept;
}
