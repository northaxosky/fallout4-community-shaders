#pragma once

#include "Render/ShaderInjectionTargets.h"

#include <cstdint>

struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Buffer;
struct ID3D11SamplerState;

namespace cs::engine
{
	struct FrameBindingMetrics
	{
		std::uint64_t skipped = 0;
		bool collect = false;
	};

	// Stage-dispatched context binds and reads; unknown stages are no-ops.
	void SetStageBindings(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11ShaderResourceView* const*) noexcept;
	void SetStageBindings(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11Buffer* const*) noexcept;
	void SetStageBindings(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11SamplerState* const*) noexcept;
	void GetStageBindings(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11ShaderResourceView**) noexcept;
	void GetStageBindings(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11Buffer**) noexcept;
	void GetStageBindings(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11SamplerState**) noexcept;

	// Starts a frame; metrics accumulate only when a_collectMetrics is set.
	void ResetFrameBindings(bool a_collectMetrics = false) noexcept;
	// Outputs or plugin GPU work may have unbound views; rebind on next use.
	void InvalidateFrameBindings() noexcept;
	// Advances on reset or invalidation; earlier outputs may be gone.
	std::uint64_t GetFrameBindingEpoch() noexcept;
	FrameBindingMetrics GetFrameBindingMetrics() noexcept;
	void BindFrameShaderResources(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11ShaderResourceView* const*) noexcept;
	void BindFrameConstantBuffers(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11Buffer* const*) noexcept;
	// Binds only slots whose trusted binding differs; returns the D3D calls made.
	std::uint32_t BindDrawShaderResources(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11ShaderResourceView* const*) noexcept;
	std::uint32_t BindDrawConstantBuffers(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11Buffer* const*) noexcept;
}
