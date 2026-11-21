#pragma once

#include "Feature.h"

#include <Profiler.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace cs::render::profiling
{
	// Records the engine device; queries allocate once a consumer calls KeepActive.
	void InitializeD3D11(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context) noexcept;

	// Marks the engine's completed deferred-composite frame. Generated D3D12
	// presentation frames are intentionally outside this boundary.
	void MarkEngineFrame() noexcept;

	Profiler& GetProfiler() noexcept;
	// A pass's group: the name before "::", else the whole name.
	constexpr std::string_view GroupOf(std::string_view a_passName) noexcept
	{
		return a_passName.substr(0, a_passName.find("::"));
	}
	// Any thread; the one GPU-query gate. Call each frame timings are read.
	void KeepActive(std::uint32_t a_frames = 2) noexcept;
	// Overlay consumer; gates per-shader draw timings only, not GPU queries.
	void SetEnabled(bool a_enabled) noexcept;
	// Nested passes pause their parent, so every row is self time.
	bool BeginPass(std::string_view a_name);
	void EndPass() noexcept;
	// Startup only: registration closes at the first render hook.
	bool InstallDeferredCompositePass();

	namespace detail
	{
		template <class Sink>
		void CollectPassTimings(Sink& a_sink, std::string_view a_prefix)
		{
			std::uint32_t count{};
			for (const auto& result : GetProfiler().GetResults()) {
				if (!result.valid || !result.name.starts_with(a_prefix))
					continue;
				const auto pass = result.name.substr(a_prefix.size());
				a_sink.Field(pass + "_gpu_ms", result.gpuTimeMs);
				if (result.external)
					continue;
				a_sink.Field(pass + "_cpu_ms", result.cpuTimeMs);
				++count;
			}
			a_sink.Field("timing_passes", count);
		}
	}

	// Publishes fully qualified annotation names.
	template <class Sink>
	void CollectPassTimings(Sink& a_sink)
	{
		detail::CollectPassTimings(a_sink, {});
	}

	// Publishes one feature's "Name::" passes with local field names.
	template <class Sink>
	void CollectPassTimings(Sink& a_sink, const Feature& a_feature)
	{
		detail::CollectPassTimings(a_sink, std::string(a_feature.GetName()) + "::");
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
	// Any thread.
	std::uint64_t FrameSequence() noexcept;
}
