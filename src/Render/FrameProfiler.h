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
	// Initializes the GPU profiler once from the engine-owned D3D11 device.
	void InitializeD3D11(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context) noexcept;

	// Marks the engine's completed deferred-composite frame. Generated D3D12
	// presentation frames are intentionally outside this boundary.
	void MarkEngineFrame() noexcept;

	Profiler& GetProfiler() noexcept;
	void SetEnabled(bool a_enabled) noexcept;
	bool BeginPass(std::string_view a_name);
	void EndPass() noexcept;

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
