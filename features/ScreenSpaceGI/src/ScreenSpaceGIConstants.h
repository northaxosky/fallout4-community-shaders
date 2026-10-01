#pragma once

#include <DirectXMath.h>

#include <cstddef>
#include <cstdint>

namespace cs::features::ssgi
{
	// FO4: fill the unchanged upstream b1 ABI from the world-camera record.
	struct alignas(16) Constants
	{
		DirectX::XMFLOAT4X4 PrevInvViewMat{};
		DirectX::XMFLOAT4 NDCToViewMul{}, NDCToViewAdd{};
		DirectX::XMFLOAT2 TexDim{}, RcpTexDim{}, FrameDim{}, RcpFrameDim{};
		std::uint32_t FrameIndex{}, NumSlices{}, NumSteps{};
		float MinScreenRadius{}, AORadius{}, GIRadius{}, EffectRadius{}, Thickness{};
		DirectX::XMFLOAT2 DepthFadeRange{};
		float DepthFadeScaleConst{}, GISaturation{}, GIDistanceCompensation{};
		float GICompensationMaxDist{}, pad1{}, AOPower{}, GIStrength{};
		float DepthDisocclusion{}, NormalDisocclusion{};
		std::uint32_t MaxAccumFrames{};
		float BlurRadius{}, DistanceNormalisation{}, pad[2]{};
	};
	static_assert(sizeof(Constants) == 224);
	static_assert(offsetof(Constants, FrameIndex) == 128);
	static_assert(offsetof(Constants, MaxAccumFrames) == 204);
}
