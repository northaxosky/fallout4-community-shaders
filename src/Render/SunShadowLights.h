#pragma once

#include "Render/DirectionalShadowLightData.h"
#include "Render/ShadowCascades.h"

#include <cstddef>
#include <cstdint>

struct ID3D11ShaderResourceView;

namespace cs::engine
{
	// One light block per consumer layout, built from one snapshot per frame.

	// Type-erased DirectionalShadowLightData<N>; use the templates below.
	struct SunShadowLightLayout
	{
		std::size_t cascades;
		std::size_t stride;
		bool (*build)(const SunCascadeSnapshot&, void*) noexcept;
	};

	template <std::size_t N>
	inline constexpr SunShadowLightLayout kSunShadowLightLayout{
		N, sizeof(DirectionalShadowLightData<N>),
		[](const SunCascadeSnapshot& a_snapshot, void* a_out) noexcept {
			return BuildDirectionalShadowLightData<N>(a_snapshot, *static_cast<DirectionalShadowLightData<N>*>(a_out));
		}
	};

	[[nodiscard]] bool AcquireSunShadowLights(const SunShadowLightLayout& a_layout);
	[[nodiscard]] ID3D11ShaderResourceView* GetSunShadowLightsSRV(const SunShadowLightLayout& a_layout) noexcept;

	// Call in Load/OnPostPostLoad; buffers exist only for acquired layouts.
	template <std::size_t N>
	[[nodiscard]] bool AcquireSunShadowLights()
	{
		return AcquireSunShadowLights(kSunShadowLightLayout<N>);
	}

	struct SunShadowLightState
	{
		SunCascadeStatus status = SunCascadeStatus::kNoLight;
		bool published = false;
		std::uint64_t generation = 0;
		// Borrowed; use only inside a post-sun-render callback.
		SunCascadeSnapshot snapshot;
	};

	// Render thread.
	[[nodiscard]] const SunShadowLightState& GetSunShadowLightState() noexcept;

	// Live sun cascade array view; null unless a valid R16 array is published.
	[[nodiscard]] ID3D11ShaderResourceView* GetSunCascadeArraySRV() noexcept;

	// Zeroed while unpublished; null before D3D is ready or when not acquired.
	template <std::size_t N>
	[[nodiscard]] ID3D11ShaderResourceView* GetSunShadowLightsSRV() noexcept
	{
		return GetSunShadowLightsSRV(kSunShadowLightLayout<N>);
	}
}
