#pragma once

#include "Format/PbrMaterial.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

// BGSM version 100: a v2 body (Part 1) then tagged blocks to end of file (Part 2).
// Layout: notes/truepbr/bgsm-v100-schema.md. Engine-free; callers pass Part 2.
namespace cs::features::true_pbr
{
	inline constexpr std::uint32_t kBgsmVersion = 100;

	// The version of a BGSM header; nullopt without the magic.
	[[nodiscard]] std::optional<std::uint32_t> ReadBgsmVersion(std::span<const std::uint8_t> a_file);

	struct DecodedPart2
	{
		PbrMaterial material;
		std::vector<std::string> unknownTags;
	};

	// Reads the blocks and decodes `CSPB`, `CSTS` and `CSFZ`; the error names the first defect.
	[[nodiscard]] std::expected<DecodedPart2, std::string> DecodePart2(std::span<const std::uint8_t> a_part2);
}
