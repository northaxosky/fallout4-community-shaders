#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

struct ID3D11ShaderResourceView;

namespace cs
{
	class Feature;
}

namespace cs::engine
{
	// The host renders the sky into the engine reflection cube for consumers.

	// Call during Load or OnPostPostLoad; false keeps the consumer's fallback.
	// a_wantsLod (optional) asks for LOD land, objects and trees on the side faces.
	[[nodiscard]] bool RegisterSkyReflectionCubeConsumer(
		std::string_view a_name,
		std::function<bool()> a_wantsCube,
		std::function<bool()> a_wantsLod = {});
	[[nodiscard]] bool RegisterSkyReflectionCubeConsumer(
		Feature& a_owner,
		std::string_view a_name,
		std::function<bool()> a_wantsCube,
		std::function<bool()> a_wantsLod = {});

	// Render thread. Null unless all six faces are fresh.
	[[nodiscard]] ID3D11ShaderResourceView* GetSkyReflectionCubeSRV() noexcept;

	// Engine cube and depth face size; set before the renderer creates targets.
	void SetSkyReflectionCubeSize(std::uint32_t a_size) noexcept;

	struct SkyReflectionCubeStatus
	{
		bool hookInstalled = false;
		bool disabled = false;
		bool valid = false;
		std::uint32_t faceMask = 0;
		std::uint32_t consumers = 0;
		std::uint64_t renders = 0;
		std::uint64_t faces = 0;
		std::uint64_t lodFaces = 0;
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
