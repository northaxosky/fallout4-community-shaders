#include "Render/EngineCallSite.h"

#include "PCH.h"

#include <cstring>
#include <format>
#include <utility>

namespace cs::engine
{
	namespace
	{
		std::string RuntimeLabel()
		{
			return std::format(
				"Fallout 4 {}", REX::FModule::GetExecutingModule().GetFileVersion());
		}
	}

	std::uintptr_t RuntimeSite(const REL::ID& a_function, const RuntimeOffsets& a_offset)
	{
		return a_function.address() +
		       a_offset[static_cast<std::size_t>(REX::FModule::GetRuntimeIndex())];
	}

	std::expected<std::uintptr_t, std::string> ResolveCallSite(
		const CallSiteAnchor& a_anchor) noexcept
	{
		const auto site = RuntimeSite(a_anchor.function, a_anchor.offset);
		const auto expected = a_anchor.target.address();
		const auto text = REX::FModule::GetExecutingModule().GetSection(".text");
		const auto textBegin = text.GetAddress();
		if (site < textBegin || site + 5 > textBegin + text.GetSize()) {
			return std::unexpected(std::format(
				"{}: call site {:#x} is outside the {} executable code",
				a_anchor.name, site, RuntimeLabel()));
		}

		std::array<std::uint8_t, 5> bytes{};
		std::memcpy(bytes.data(), reinterpret_cast<const void*>(site), bytes.size());
		const auto classification = ClassifyCallSite(bytes, site, expected);
		switch (classification.kind) {
		case CallSiteKind::kExpected:
			return site;
		case CallSiteKind::kNotACall:
			return std::unexpected(std::format(
				"{}: {} has no rel32 call at {:#x} (bytes {:02X} {:02X} {:02X} {:02X} {:02X})",
				a_anchor.name, RuntimeLabel(), site,
				bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]));
		case CallSiteKind::kWrongTarget:
			return std::unexpected(std::format(
				"{}: {} call at {:#x} targets {:#x}, expected {:#x}",
				a_anchor.name, RuntimeLabel(), site, classification.target, expected));
		}
		std::unreachable();
	}
}
