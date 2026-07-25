#pragma once

#include <array>

namespace cs::features::ssgi_lifecycle
{
	inline constexpr std::array<float, 4> kAOIdentity{ 1.0f, 1.0f, 1.0f, 1.0f };
	inline constexpr std::array<float, 4> kBounceIdentity{ 0.0f, 0.0f, 0.0f, 0.0f };

	struct InitialTextureAllocationPlan
	{
		bool bakeCritical = true;
		bool enableOnly = false;
	};

	constexpr InitialTextureAllocationPlan InitialTexturePlan(bool a_enabled) noexcept
	{
		return {
			.bakeCritical = true,
			.enableOnly = a_enabled
		};
	}

	constexpr bool CanBakeAmbientInjection(
		bool a_started,
		bool a_bakeResourcesReady,
		bool a_hasBounceTexture,
		bool,
		int) noexcept
	{
		return a_started && a_bakeResourcesReady && a_hasBounceTexture;
	}

	constexpr bool UsesDirectAmbientBounce(bool a_enabled, int a_delivery) noexcept
	{
		return a_enabled && a_delivery == 1;
	}
}
