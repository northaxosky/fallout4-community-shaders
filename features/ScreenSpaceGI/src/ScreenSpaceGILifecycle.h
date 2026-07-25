#pragma once

#include <array>

namespace cs::features::ssgi_lifecycle
{
	inline constexpr std::array<float, 4> kAOIdentity{ 1.0f, 1.0f, 1.0f, 1.0f };
	inline constexpr std::array<float, 4> kBounceIdentity{ 0.0f, 0.0f, 0.0f, 0.0f };

	constexpr bool CanBakeAmbientInjection(
		bool a_started,
		bool a_resourcesReady,
		bool a_hasBounceTexture,
		bool,
		int) noexcept
	{
		return a_started && a_resourcesReady && a_hasBounceTexture;
	}

	constexpr bool UsesDirectAmbientBounce(bool a_enabled, int a_delivery) noexcept
	{
		return a_enabled && a_delivery == 1;
	}
}
