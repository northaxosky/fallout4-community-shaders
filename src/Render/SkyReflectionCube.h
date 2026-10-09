#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

struct ID3D11ShaderResourceView;

namespace cs::engine
{
	// The host renders the sky into the engine reflection cube for consumers.

	// Call during Load or OnPostPostLoad; false keeps the consumer's fallback.
	[[nodiscard]] bool RegisterSkyReflectionCubeConsumer(
		std::string_view a_name,
		std::function<bool()> a_wantsCube);

	// Render thread. Null unless all six faces are fresh.
	[[nodiscard]] ID3D11ShaderResourceView* GetSkyReflectionCubeSRV() noexcept;

	struct SkyReflectionCubeStatus
	{
		bool hookInstalled = false;
		bool disabled = false;
		bool valid = false;
		std::uint32_t faceMask = 0;
		std::uint32_t consumers = 0;
		std::uint64_t renders = 0;
		std::uint64_t faces = 0;
		std::uint64_t invalidations = 0;
		std::uint64_t unboundTargets = 0;
		std::uint64_t skippedNoDemand = 0;
		std::uint64_t skippedInterior = 0;
		std::uint64_t skippedSkyHidden = 0;
		std::uint64_t skippedNoCamera = 0;
		std::uint64_t skippedNoTargets = 0;
		std::uint64_t skippedNoWorld = 0;
	};

	[[nodiscard]] SkyReflectionCubeStatus GetSkyReflectionCubeStatus() noexcept;
}
