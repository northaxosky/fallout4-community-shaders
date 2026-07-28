#pragma once

#include "Provenance.h"
#include "Sha1.h"

#include <cstdint>
#include <memory>

struct ID3D11PixelShader;

namespace cs::features::catalog::shader_tracker
{
	struct Stats
	{
		std::uint64_t tracked = 0;
		std::uint64_t aliases = 0;
	};

	struct Lookup
	{
		Sha1Result sha{};
		bool alias = false;
		bool ambiguousOrigin = false;
	};

	enum class TrackResult
	{
		kIgnored,
		kTracked,
		kUpdated,
		kAmbiguousOrigin,
		kAllocationFailure
	};

	enum class RouteTrackResult
	{
		kIgnored,
		kTracked,
		kDuplicate,
		kAmbiguous,
		kAllocationFailure
	};

	TrackResult TrackPixelShader(
		ID3D11PixelShader* shader,
		const Sha1Result& sha,
		bool alias = false) noexcept;
	bool TryGetPixelShader(ID3D11PixelShader* shader, Lookup& result) noexcept;
	RouteTrackResult TrackRouteLineage(
		ID3D11PixelShader* a_shader,
		const Sha1Result& a_sha,
		const std::shared_ptr<RouteCaptureRecordState>& a_record) noexcept;
	std::shared_ptr<RouteCaptureRecordState> TryReserveRouteBind(
		ID3D11PixelShader* a_shader) noexcept;
	void SetEnabled(bool enabled) noexcept;
	void Clear() noexcept;
	Stats GetStats() noexcept;
#ifdef FO4CS_SHADER_CATALOG_TESTING
	void FailNextAllocationForTesting() noexcept;
#endif
}
