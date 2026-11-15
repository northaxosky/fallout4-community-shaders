#pragma once

#include "Format/PbrMaterial.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Engine-free: callers hand over the bytes after Part 1.
namespace cs::features::true_pbr
{
	inline constexpr std::uint32_t kBgsmVersion = 100;

	[[nodiscard]] std::optional<std::uint32_t> ReadBgsmVersion(std::span<const std::uint8_t> a_file);

	struct DecodedPart2
	{
		PbrMaterial material;
		std::vector<std::string> unknownTags;
	};

	[[nodiscard]] std::expected<DecodedPart2, std::string> DecodePart2(std::span<const std::uint8_t> a_part2);
}
