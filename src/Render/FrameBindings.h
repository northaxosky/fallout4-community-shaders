#pragma once

#include "Render/ShaderInjectionTargets.h"

#include <array>
#include <bitset>
#include <cstdint>

struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Buffer;

namespace cs::engine
{
	struct FrameBindingMetrics
	{
		std::uint64_t checks = 0;
		std::uint64_t lost = 0;
		std::uint64_t lostTotal = 0;
		std::array<std::bitset<128>, 3> resources;
		std::array<std::bitset<14>, 3> buffers;
	};

	void ResetFrameBindings() noexcept;
	FrameBindingMetrics GetFrameBindingMetrics() noexcept;
	void BindFrameShaderResources(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11ShaderResourceView* const*) noexcept;
	void BindFrameConstantBuffers(ID3D11DeviceContext*, ShaderStage, std::uint32_t, std::uint32_t, ID3D11Buffer* const*) noexcept;
	void VerifyFrameBindings(ID3D11DeviceContext*, ShaderStage, ShaderInjectionTarget) noexcept;
}
