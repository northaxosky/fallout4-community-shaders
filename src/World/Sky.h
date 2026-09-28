#pragma once

#include <DirectXMath.h>

namespace cs::engine
{
	// Returns false without a usable exterior sun.
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept;

	// Linear sun radiance as the deferred sun pass receives it.
	bool TryGetSunLightColor(DirectX::XMFLOAT3& a_color) noexcept;

	// Sky::Flags::kHideSky: the sky is not rendered this frame.
	bool IsSkyHidden() noexcept;
}
