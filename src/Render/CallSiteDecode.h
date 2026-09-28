#pragma once

#include <cstdint>
#include <span>

namespace cs::engine
{
	enum class CallSiteKind : std::uint8_t
	{
		kExpected,
		kNotACall,
		kWrongTarget
	};

	struct CallSiteClassification
	{
		CallSiteKind kind = CallSiteKind::kNotACall;
		std::uintptr_t target = 0;
	};

	[[nodiscard]] constexpr CallSiteClassification ClassifyCallSite(
		std::span<const std::uint8_t, 5> a_bytes,
		std::uintptr_t a_site,
		std::uintptr_t a_expectedTarget) noexcept
	{
		if (a_bytes[0] != 0xE8) {
			return {};
		}
		const auto displacement = static_cast<std::int32_t>(
			static_cast<std::uint32_t>(a_bytes[1]) |
			(static_cast<std::uint32_t>(a_bytes[2]) << 8) |
			(static_cast<std::uint32_t>(a_bytes[3]) << 16) |
			(static_cast<std::uint32_t>(a_bytes[4]) << 24));
		const auto target = a_site + 5 + static_cast<std::intptr_t>(displacement);
		return { target == a_expectedTarget ? CallSiteKind::kExpected : CallSiteKind::kWrongTarget, target };
	}
}