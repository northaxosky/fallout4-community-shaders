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

	void TrackPixelShader(ID3D11PixelShader* shader, const Sha1Result& sha, bool alias = false) noexcept;
	bool TryGetPixelShaderSha1(ID3D11PixelShader* shader, Sha1Result& sha) noexcept;
	void SetEnabled(bool enabled) noexcept;
	void Clear() noexcept;
	Stats GetStats() noexcept;
}
