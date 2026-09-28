#pragma once

#include "Render/CallSiteDecode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace cs::engine
{
	using RuntimeOffsets = std::array<std::ptrdiff_t, 3>;

	// Rel32 call inside an engine function, per runtime.
	struct CallSiteAnchor
	{
		std::string_view name;
		REL::ID function;
		RuntimeOffsets offset{};
		REL::ID target;
	};

	[[nodiscard]] std::uintptr_t RuntimeSite(const REL::ID& a_function, const RuntimeOffsets& a_offset);

	// Returns the call-site address once it is proven to call the anchor's target.
	[[nodiscard]] std::expected<std::uintptr_t, std::string> ResolveCallSite(
		const CallSiteAnchor& a_anchor) noexcept;
}