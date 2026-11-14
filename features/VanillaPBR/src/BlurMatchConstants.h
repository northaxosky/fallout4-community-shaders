#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace cs::features::vanilla_pbr
{
	inline constexpr std::uint32_t kBlurMatchMaxJobs = 8;
	inline constexpr std::uint32_t kBlurMatchMaxMips = 16;
	// Every cube and mip is measured on one face grid, so detail compares.
	inline constexpr std::uint32_t kBlurMatchGrid = 256;
	// Each thread measures a block; a group of 8x8 threads covers 64x64 texels.
	inline constexpr std::uint32_t kBlurMatchBlock = 8;
	inline constexpr std::uint32_t kBlurMatchGroupsPerSide = kBlurMatchGrid / (kBlurMatchBlock * 8);
	inline constexpr std::uint32_t kBlurMatchGroupsPerFace = kBlurMatchGroupsPerSide * kBlurMatchGroupsPerSide;
	// One matched-LOD row per authored slot; BlurMatchCS runs one thread each.
	inline constexpr std::uint32_t kBlurMatchMaxSlots = 64;
	// Authored slots, then one target per live stream.
	inline constexpr std::uint32_t kBlurMatchTargets = kBlurMatchMaxSlots + 2;
	// Matched LOD per mip, then the authored average RGB and the live luminance.
	inline constexpr std::uint32_t kBlurMatchTableWidth = kBlurMatchMaxMips + 4;

	template <std::uint32_t Value>
	inline constexpr auto kBlurMatchDecimal = [] {
		std::array<char, 11> text{};
		std::uint32_t digits = 1;
		for (auto rest = Value; rest >= 10; rest /= 10)
			++digits;
		for (auto rest = Value; digits > 0; rest /= 10)
			text[--digits] = static_cast<char>('0' + rest % 10);
		return text;
	}();

	// The blur-match shaders take their sizes from here alone.
	inline const std::vector<std::pair<const char*, const char*>> kBlurMatchShaderDefines{
		{ "BLUR_MATCH_MAX_JOBS", kBlurMatchDecimal<kBlurMatchMaxJobs>.data() },
		{ "BLUR_MATCH_MAX_MIPS", kBlurMatchDecimal<kBlurMatchMaxMips>.data() },
		{ "BLUR_MATCH_MAX_SLOTS", kBlurMatchDecimal<kBlurMatchMaxSlots>.data() },
		{ "BLUR_MATCH_GRID", kBlurMatchDecimal<kBlurMatchGrid>.data() },
		{ "BLUR_MATCH_BLOCK", kBlurMatchDecimal<kBlurMatchBlock>.data() },
		{ "BLUR_MATCH_GROUPS_PER_SIDE", kBlurMatchDecimal<kBlurMatchGroupsPerSide>.data() }
	};

	struct alignas(16) BlurDetailCB
	{
		// Target, mip, first face slice, unused.
		std::uint32_t Jobs[kBlurMatchMaxJobs][4]{};
	};
	static_assert(sizeof(BlurDetailCB) == 128);

	struct alignas(16) BlurMatchCB
	{
		std::uint32_t LiveTarget = 0;
		std::uint32_t LiveMips = 0;
		std::uint32_t AuthoredMips = 0;
		std::uint32_t SlotCount = 0;
		std::uint32_t SlotReady[2]{};
		std::uint32_t MatchPad[2]{};
	};
	static_assert(sizeof(BlurMatchCB) == 32);
}
