#pragma once

#include <DirectXMath.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace cs::features::subsurface_scattering
{
	inline constexpr std::size_t kKernelSamples = 21;

	// Mirrors upstream's PerFrameSSS (b1) without change.
	struct alignas(16) Constants
	{
		std::array<DirectX::XMFLOAT4, 2 * kKernelSamples> Kernels{};
		DirectX::XMFLOAT4 BaseProfile{}, HumanProfile{};
		float SSSS_FOVY{};
		std::uint32_t BurleySamples{}, ScatterMode{}, pad{};
		DirectX::XMFLOAT4 MeanFreePathBase{}, MeanFreePathHuman{};
	};
	static_assert(sizeof(Constants) == 752);
	static_assert(offsetof(Constants, BaseProfile) == 672);
	static_assert(offsetof(Constants, SSSS_FOVY) == 704);
	static_assert(offsetof(Constants, MeanFreePathBase) == 720);
}
