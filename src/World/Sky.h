#pragma once

#include <DirectXMath.h>

namespace RE
{
	class BSGeometry;
	class BSSkyShaderProperty;
	class TESObjectCELL;
}

namespace cs::engine
{
	// Normalized light propagation direction, away from the sun.
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept;

	// Unit direction from the camera to the drawn sun disc.
	bool TryGetSunDiscDirectionWS(float& outX, float& outY, float& outZ) noexcept;

	// Linear sun radiance as the deferred sun pass receives it.
	bool TryGetSunLightColor(DirectX::XMFLOAT3& a_color) noexcept;

	// The fog sun glare BSDFComposite blends toward: lerp(fog, color, pow(dot(view, sun), exponent) * intensity).
	struct SunGlare
	{
		DirectX::XMFLOAT3 color{};
		float exponent = 0.0f;
		// The sun disc fade; zero outside Sky::Mode::kFull.
		float intensity = 0.0f;
	};
	[[nodiscard]] SunGlare GetSunGlare() noexcept;

	// The geometry's sky shader property; null for any other.
	RE::BSSkyShaderProperty* GetSkyShaderProperty(RE::BSGeometry* a_geometry);
	const RE::BSSkyShaderProperty* GetSkyShaderProperty(const RE::BSGeometry* a_geometry);

	// Sky::Flags::kHideSky: the sky is not rendered this frame.
	bool IsSkyHidden() noexcept;

	// Sky::Mode::kFull: the exterior sky lights the scene.
	bool IsFullSky() noexcept;

	// Interior cell, no-sky worldspace, or Sky::mode != kFull.
	bool IsInterior() noexcept;

	// Interior cell with both sun-shadow bits: the engine renders sun cascades.
	bool InteriorRendersSunShadows(const RE::TESObjectCELL& a_cell) noexcept;

	// The engine's main sun-shadow eligibility for the current cell.
	bool HasSunShadows() noexcept;

	// The main menu or a loading screen is open.
	bool IsMainOrLoadingMenuOpen() noexcept;
}
