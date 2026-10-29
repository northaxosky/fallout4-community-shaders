#pragma once

#include "LightData.h"
#include "StrictLights.h"

#include <RE/B/BSLight.h>
#include <RE/S/ShadowSceneNode.h>

#include <DirectXMath.h>

#include <cstdint>
#include <span>
#include <vector>

namespace cs::features::light_limit_fix
{
	inline constexpr std::uint32_t kMaxLights = 1024;

	// Sidecar record; flags lack kInitialised when the light is not authored.
	using SidecarSnapshot = LightData (*)(RE::BSLight*, bool a_renderFade);

	struct GatherStats
	{
		std::uint32_t lights = 0;
		std::uint32_t shadowLights = 0;
		std::uint32_t coneLights = 0;
		std::uint32_t strictLights = 0;
		std::uint32_t dropped = 0;
	};

	class LightGather
	{
	public:
		LightGather();

		void Gather(RE::ShadowSceneNode& a_scene, const DirectX::XMFLOAT4& a_posAdjust,
			SidecarSnapshot a_snapshot);
		void Clear();

		std::span<const LightData> Lights() const noexcept { return _lights; }
		std::span<const ConeData> Cones() const noexcept { return _cones; }
		const GatherStats& Stats() const noexcept { return _stats; }
		const StrictLights& Strict() const noexcept { return _strict; }

	private:
		std::vector<LightData> _lights;
		std::vector<ConeData> _cones;
		GatherStats _stats;
		StrictLights _strict;
	};
}
