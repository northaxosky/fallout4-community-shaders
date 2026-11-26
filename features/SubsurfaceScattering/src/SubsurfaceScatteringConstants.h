#pragma once

#include <DirectXMath.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace cs::features::subsurface_scattering
{
	inline constexpr std::size_t kKernelSamples = 21;

	// Mirrors upstream's PerFrameSSS (b1) plus the trailing dispatch origin.
	struct alignas(16) Constants
	{
		std::array<DirectX::XMFLOAT4, 2 * kKernelSamples> Kernels{};
		DirectX::XMFLOAT4 BaseProfile{}, HumanProfile{};
		float SSSS_FOVY{};
		std::uint32_t BurleySamples{}, ScatterMode{}, pad{};
		DirectX::XMFLOAT4 MeanFreePathBase{}, MeanFreePathHuman{};
		DirectX::XMUINT2 DispatchOrigin{};
		std::uint32_t padOrigin[2]{};
	};
	static_assert(sizeof(Constants) == 768);
	static_assert(offsetof(Constants, BaseProfile) == 672);
	static_assert(offsetof(Constants, SSSS_FOVY) == 704);
	static_assert(offsetof(Constants, MeanFreePathBase) == 720);
	static_assert(offsetof(Constants, DispatchOrigin) == 752);
}
