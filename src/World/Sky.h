#pragma once

#include <DirectXMath.h>

namespace cs::engine
{
	// Normalized light propagation direction, away from the sun.
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept;

	// Linear sun radiance as the deferred sun pass receives it.
	bool TryGetSunLightColor(DirectX::XMFLOAT3& a_color) noexcept;

	// Sky::Flags::kHideSky: the sky is not rendered this frame.
	bool IsSkyHidden() noexcept;

	// Sky::Mode::kFull: the exterior sky lights the scene.
	bool IsFullSky() noexcept;

	// Interior cell, or a no-sky or fixed-dimension worldspace.
	bool IsInterior() noexcept;
}
