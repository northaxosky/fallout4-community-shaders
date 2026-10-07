#pragma once

#include <DirectXMath.h>

namespace cs::features::skylighting
{
	struct alignas(16) DirectionalShadowLightData
	{
		DirectX::XMFLOAT4X4 ShadowProj[2];
		DirectX::XMFLOAT4X4 InvShadowProj[2];
		DirectX::XMFLOAT2 EndSplitDistances;
		DirectX::XMFLOAT2 StartSplitDistances;
	};
}
