#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace cs::engine
{
	using RuntimeOffsets = std::array<std::ptrdiff_t, 3>;

	// A call inside an engine function, per runtime, and what it must call.
	template <class Target>
	struct SiteAnchor
	{
		std::string_view name;
		REL::VariantID function;
		RuntimeOffsets offset{};
		Target target;
	};

	// Rel32 call to an engine function.
	using CallSiteAnchor = SiteAnchor<REL::VariantID>;

	// COM vtable call on the context; offset is the vtable byte offset.
	struct ContextVtableCall
	{
		std::uint8_t offset = 0;
	};
	using ContextCallSiteAnchor = SiteAnchor<ContextVtableCall>;

	// Returns the call-site address once it is proven to call the anchor's target.
	[[nodiscard]] std::expected<std::uintptr_t, std::string> ResolveCallSite(
		const CallSiteAnchor& a_anchor) noexcept;
	[[nodiscard]] std::expected<std::uintptr_t, std::string> ResolveCallSite(
		const ContextCallSiteAnchor& a_anchor) noexcept;

	// Verifies the write and restores the original bytes on mismatch.
	[[nodiscard]] std::expected<void, std::string> WriteVerifiedPatch(
		std::uintptr_t a_site,
		std::span<const std::uint8_t> a_patch) noexcept;

	// Overwrites SiteLength bytes at a_site with a_branch padded by NOPs.
	template <std::size_t SiteLength, class Branch>
	[[nodiscard]] std::expected<std::array<std::uint8_t, SiteLength>, std::string> WriteBranchPatch(
		std::uintptr_t a_site,
		const Branch& a_branch) noexcept
	{
		static_assert(sizeof(Branch) <= SiteLength);
		std::array<std::uint8_t, SiteLength> patch;
		patch.fill(REL::NOP);
		std::memcpy(patch.data(), std::addressof(a_branch), sizeof(Branch));
		if (auto written = WriteVerifiedPatch(a_site, patch); !written)
			return std::unexpected(std::move(written.error()));
		return patch;
	}

	// Replaces a resolved context call with a call to a same-signature thunk.
	[[nodiscard]] std::expected<void, std::string> RedirectContextCall(
		std::uintptr_t a_site,
		std::uintptr_t a_thunk) noexcept;
}