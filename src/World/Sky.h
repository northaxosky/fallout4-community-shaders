#pragma once

#include <DirectXMath.h>

namespace RE
{
	class BSGeometry;
	class BSSkyShaderProperty;
}

namespace cs::engine
{
	// Normalized light propagation direction, away from the sun.
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept;

	// Unit direction from the camera to the drawn sun disc.
	bool TryGetSunDiscDirectionWS(float& outX, float& outY, float& outZ) noexcept;

	// Linear sun radiance as the deferred sun pass receives it.
	bool TryGetSunLightColor(DirectX::XMFLOAT3& a_color) noexcept;

	// The geometry's sky shader property; null for any other.
	RE::BSSkyShaderProperty* GetSkyShaderProperty(RE::BSGeometry* a_geometry);
	const RE::BSSkyShaderProperty* GetSkyShaderProperty(const RE::BSGeometry* a_geometry);

	// Sky::Flags::kHideSky: the sky is not rendered this frame.
	bool IsSkyHidden() noexcept;

	// Sky::Mode::kFull: the exterior sky lights the scene.
	bool IsFullSky() noexcept;

	// Interior cell, no-sky worldspace, or Sky::mode != kFull.
	bool IsInterior() noexcept;

	// The main menu or a loading screen is open.
	bool IsMainOrLoadingMenuOpen() noexcept;
}
