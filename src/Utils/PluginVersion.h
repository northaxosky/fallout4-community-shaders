#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace cs::util
{
	// Parses the PE export table without executing the image; nullopt if malformed.
	[[nodiscard]] std::optional<std::uint32_t> ParsePluginVersion(std::span<const std::byte> a_image) noexcept;

	[[nodiscard]] std::optional<std::uint32_t> ReadPluginVersion(const std::filesystem::path& a_dll) noexcept;
}
