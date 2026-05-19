#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace cs::features::catalog
{
	struct Sha1Result
	{
		std::array<uint8_t, 20> bytes;
	};

	// One-time provider open; safe to call from any thread, idempotent.
	void Sha1InitOnce();

	// Hot-path callable. Uses the cached BCrypt provider handle.
	// Returns zero-initialized bytes on failure (caller treats that as a non-fatal hash miss).
	Sha1Result Sha1Compute(const void* data, std::size_t len) noexcept;

	// 40-char lowercase hex. Allocates; call from writer thread only.
	std::string Sha1ToHex(const Sha1Result& r);
}
