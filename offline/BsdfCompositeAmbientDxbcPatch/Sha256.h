#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace fo4cs::offline::hash
{
	// Deterministic local fault points for the host hash routine; production passes kNone.
	enum class Sha256FaultPoint
	{
		kNone,
		kHashObjectAllocation,
		kCryptoFailure
	};

	// Carries only a stage literal and a provider status, so reporting it never allocates.
	struct HashUnavailable
	{
		const char* stage = "";
		std::int32_t status = 0;
	};

	using Sha256Result = std::expected<std::string, HashUnavailable>;

	// Every provider failure is an explicit status result; only allocation failure throws.
	Sha256Result Sha256Hex(
		std::string_view a_bytes,
		Sha256FaultPoint a_fault = Sha256FaultPoint::kNone);
}
