#pragma once

#include <RE/N/NiLight.h>
#include <RE/N/NiPointer.h>

#include <cstdint>

namespace RE
{
	class BSLight;
}

namespace cs::engine::local_lights
{
	inline constexpr std::uint32_t kCapacity = 625;
	struct TiledRecord
	{
		std::uint32_t flags;
		RE::NiPoint3 position;
		float radius;
		RE::NiColor color;
		RE::NiPoint3 attenuation;
		float unwritten;
	};
	static_assert(sizeof(TiledRecord) == 48);

	// FO4: CommonLib names the verified native radius channels NiLight::spec.
	inline float Radius(const RE::NiLight& a_light) { return a_light.spec.r; }
	inline void SetRadius(RE::NiLight& a_light, float a_radius)
	{
		a_light.spec = { a_radius, a_radius, a_radius };
	}
	// FO4: native render falloff (DFLight/DFTiledLighting) at x = distance / radius.
	float RadialFalloff(float a_x, float a_constant, float a_scalar, float a_exponent);
	RE::NiLight* Light(RE::BSLight& a_light);
	float CurrentFade(RE::BSLight& a_light);
	bool IsShadowLight(RE::BSLight& a_light);
	bool GameplayExcluded(RE::BSLight& a_light);
	float ShapeAttenuation(RE::BSLight& a_light, const RE::NiPoint3& a_position);
	void SetLuminance(RE::BSLight& a_light, float a_luminance);
	void InvalidateSpotCone(RE::BSLight& a_light);
	std::uint32_t AppendSide();
	std::uint32_t AppendCount(std::uint32_t a_side);
	const TiledRecord* Records(std::uint32_t a_side);
}
