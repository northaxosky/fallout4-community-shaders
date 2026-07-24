#pragma once

#include "Sha1.h"

#include <cstdint>

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

	TrackResult TrackPixelShader(
		ID3D11PixelShader* shader,
		const Sha1Result& sha,
		bool alias = false) noexcept;
	bool TryGetPixelShader(ID3D11PixelShader* shader, Lookup& result) noexcept;
	void SetEnabled(bool enabled) noexcept;
	void Clear() noexcept;
	Stats GetStats() noexcept;
#ifdef FO4CS_SHADER_CATALOG_TESTING
	void FailNextAllocationForTesting() noexcept;
#endif
}
