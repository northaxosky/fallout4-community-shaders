#pragma once

#include <DirectXMath.h>

#include <cmath>

namespace cs::engine
{
	// Builds a D3D LH perspective from unit-depth frustum half-extents and its UV-to-view ray transform.
	[[nodiscard]] inline bool BuildPerspectiveFromFrustum(
		float left,
		float right,
		float top,
		float bottom,
		float nearZ,
		float farZ,
		DirectX::XMFLOAT4X4& outProj,
		DirectX::XMFLOAT4X4& outInvProj,
		DirectX::XMFLOAT4&   outNdcToViewMul,
		DirectX::XMFLOAT4&   outNdcToViewAdd)
	{
		if (!std::isfinite(left) ||
			!std::isfinite(right) ||
			!std::isfinite(top) ||
			!std::isfinite(bottom) ||
			!std::isfinite(nearZ) ||
			!std::isfinite(farZ) ||
			nearZ <= 0.0f ||
			farZ <= nearZ ||
			left >= right ||
			bottom >= top) {
			return false;
		}

		const auto proj = DirectX::XMMatrixPerspectiveOffCenterLH(
			nearZ * left,
			nearZ * right,
			nearZ * bottom,
			nearZ * top,
			nearZ,
			farZ);
		const auto invProj = DirectX::XMMatrixInverse(nullptr, proj);

		const auto viewTopLeft = DirectX::XMVector4Transform(DirectX::XMVectorSet(-1.0f, 1.0f, 1.0f, 1.0f), invProj);
		const auto viewBottomRight = DirectX::XMVector4Transform(DirectX::XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f), invProj);
		const float topLeftZ = DirectX::XMVectorGetZ(viewTopLeft);
		const float bottomRightZ = DirectX::XMVectorGetZ(viewBottomRight);
		if (topLeftZ == 0.0f || bottomRightZ == 0.0f) {
			return false;
		}

		DirectX::XMFLOAT4 topLeft;
		DirectX::XMFLOAT4 bottomRight;
		DirectX::XMStoreFloat4(&topLeft, DirectX::XMVectorScale(viewTopLeft, 1.0f / topLeftZ));
		DirectX::XMStoreFloat4(&bottomRight, DirectX::XMVectorScale(viewBottomRight, 1.0f / bottomRightZ));

		DirectX::XMStoreFloat4x4(&outProj, proj);
		DirectX::XMStoreFloat4x4(&outInvProj, invProj);
		outNdcToViewMul = { bottomRight.x - topLeft.x, bottomRight.y - topLeft.y, 0.0f, 0.0f };
		outNdcToViewAdd = { topLeft.x, topLeft.y, 0.0f, 0.0f };
		return true;
	}
}
