#include "Render/EngineCallSite.h"

#include "PCH.h"

#include "Log.h"
#include "Utils/HookOwner.h"

#include <cstring>
#include <format>
#include <utility>
#include <vector>

namespace cs::engine
{
	namespace
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
			if (a_bytes[0] != 0xE8)
				return {};
			const auto displacement = static_cast<std::int32_t>(
				static_cast<std::uint32_t>(a_bytes[1]) |
				(static_cast<std::uint32_t>(a_bytes[2]) << 8) |
				(static_cast<std::uint32_t>(a_bytes[3]) << 16) |
				(static_cast<std::uint32_t>(a_bytes[4]) << 24));
			const auto target = a_site + 5 + static_cast<std::intptr_t>(displacement);
			return { target == a_expectedTarget ? CallSiteKind::kExpected : CallSiteKind::kWrongTarget, target };
		}

		// COM call on the object in rcx: mov rax,[rcx]; call [rax+disp8].
		[[nodiscard]] constexpr bool IsContextVirtualCall(
			std::span<const std::uint8_t, 6> a_bytes,
			std::uint8_t a_vtableOffset) noexcept
		{
			return a_bytes[0] == 0x48 && a_bytes[1] == 0x8B && a_bytes[2] == 0x01 &&
			       a_bytes[3] == 0xFF && a_bytes[4] == 0x50 && a_bytes[5] == a_vtableOffset;
		}

		std::string RuntimeLabel()
		{
			return std::format(
				"Fallout 4 {}", REX::FModule::GetExecutingModule().GetFileVersion());
		}

		bool InsideText(std::uintptr_t a_site, std::size_t a_size) noexcept
		{
			const auto text = REX::FModule::GetExecutingModule().GetSection(".text");
			const auto begin = text.GetAddress();
			return a_site >= begin && a_site + a_size <= begin + text.GetSize();
		}

		std::uintptr_t RuntimeSite(const REL::VariantID& a_function, const RuntimeOffsets& a_offset)
		{
			return a_function.address() +
			       a_offset[static_cast<std::size_t>(REX::FModule::GetRuntimeIndex())];
		}

		template <std::size_t N>
		struct SiteBytes
		{
			std::uintptr_t site;
			std::array<std::uint8_t, N> bytes;
		};

		template <std::size_t N, class Target>
		std::expected<SiteBytes<N>, std::string> ReadSite(const SiteAnchor<Target>& a_anchor) noexcept
		{
			SiteBytes<N> read{ RuntimeSite(a_anchor.function, a_anchor.offset), {} };
			if (!InsideText(read.site, N)) {
				return std::unexpected(std::format(
					"{}: call site {:#x} is outside the {} executable code",
					a_anchor.name, read.site, RuntimeLabel()));
			}
			std::memcpy(read.bytes.data(), reinterpret_cast<const void*>(read.site), N);
			return read;
		}
	}

	std::expected<std::uintptr_t, std::string> ResolveCallSite(
		const CallSiteAnchor& a_anchor) noexcept
	{
		const auto read = ReadSite<5>(a_anchor);
		if (!read)
			return std::unexpected(read.error());
		const auto [site, bytes] = *read;
		const auto expected = a_anchor.target.address();
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
			{
				const auto owner = cs::util::ForeignOwner(classification.target);
				return std::unexpected(std::format(
					"{}: {} call at {:#x} targets {:#x}, expected {:#x}{}",
					a_anchor.name, RuntimeLabel(), site, classification.target, expected,
					owner ? std::format(" (redirected by {})", *owner) : std::string{}));
			}
		}
		std::unreachable();
	}

	std::expected<std::uintptr_t, std::string> ResolveCallSite(
		const ContextCallSiteAnchor& a_anchor) noexcept
	{
		const auto read = ReadSite<6>(a_anchor);
		if (!read)
			return std::unexpected(read.error());
		const auto& [site, bytes] = *read;
		if (!IsContextVirtualCall(bytes, a_anchor.target.offset)) {
			return std::unexpected(std::format(
				"{}: {} has no context call [rax+{:#x}] at {:#x} (bytes {:02X} {:02X} {:02X} {:02X} {:02X} {:02X})",
				a_anchor.name, RuntimeLabel(), a_anchor.target.offset, site,
				bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]));
		}
		return site;
	}

	std::expected<void, std::string> WriteVerifiedPatch(
		std::uintptr_t a_site,
		std::span<const std::uint8_t> a_patch) noexcept
	{
		const auto* site = reinterpret_cast<const void*>(a_site);
		const std::vector<std::uint8_t> original(
			static_cast<const std::uint8_t*>(site),
			static_cast<const std::uint8_t*>(site) + a_patch.size());
		const bool protectionRestored = REL::WriteSafe(a_site, a_patch.data(), a_patch.size());
		if (std::memcmp(site, a_patch.data(), a_patch.size()) != 0) {
			const bool restored = REL::WriteSafe(a_site, original.data(), original.size()) &&
			                      std::memcmp(site, original.data(), original.size()) == 0;
			if (!restored) {
				cs::log::Get("cs.render.enginecallsite")->error("patch at {:#x} could not be verified (rollback=false)", a_site);
				std::terminate();
			}
			FlushInstructionCache(GetCurrentProcess(), site, a_patch.size());
			return std::unexpected(std::format(
				"patch at {:#x} could not be verified (original restored)", a_site));
		}
		FlushInstructionCache(GetCurrentProcess(), site, a_patch.size());
		if (!protectionRestored) {
			cs::log::Get("cs.render.enginecallsite")
				->warn("patch at {:#x} is live but its page protection was not restored", a_site);
		}
		return {};
	}

	std::expected<void, std::string> RedirectContextCall(
		std::uintptr_t a_site,
		std::uintptr_t a_thunk) noexcept
	{
		try {
			const REL::ASM::CALL5 call(a_site, REL::GetTrampoline().allocate_branch5(a_thunk));
			if (const auto patched = WriteBranchPatch<6>(a_site, call); !patched)
				return std::unexpected(std::format("context call redirect: {}", patched.error()));
		} catch (const std::exception& e) {
			return std::unexpected(std::format("context call redirect at {:#x}: {}", a_site, e.what()));
		}
		return {};
	}
}
