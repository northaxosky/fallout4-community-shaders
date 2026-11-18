#pragma once

#include <cstdint>

namespace cs::render
{
	inline constexpr std::uint32_t kFrameDataSlot = 4;
	inline constexpr std::uint32_t kSharedDataSlot = 5;
	inline constexpr std::uint32_t kFeatureDataSlot = 6;
	inline constexpr std::uint32_t kFO4SharedDataSlot = 7;
	inline constexpr std::uint32_t kSubstrateBufferCount = 4;
	// Per-draw; TerrainBlending reuses b8 in a disjoint PS.
	inline constexpr std::uint32_t kForwardDrawSlot = 8;
	inline constexpr std::uint32_t kCanonicalDepthSlot = 17;
	// Engine owns s0-s15; ShaderInjection binds this per draw, never per frame.
	inline constexpr std::uint32_t kLinearClampSamplerSlot = 13;
	inline constexpr std::uint32_t kFullscreenDebugTextureSlot = 61;
	inline constexpr std::uint32_t kPrepassAuxSlot = 66;
	inline constexpr std::uint32_t kTruePBRF0Slot = 67;
	inline constexpr std::uint32_t kTruePBRRecordTableSlot = 68;
	// Contiguous, so one bind covers the four views.
	inline constexpr std::uint32_t kGBufferAlbedoSlot = 69;    // MRT0
	inline constexpr std::uint32_t kGBufferNormalSlot = 70;    // MRT1
	inline constexpr std::uint32_t kGBufferMaterialSlot = 71;  // MRT3: gloss, class
	inline constexpr std::uint32_t kGBufferEmissiveSlot = 72;  // MRT4
	inline constexpr std::uint32_t kGBufferViewCount = 4;
	static_assert(kGBufferEmissiveSlot == kGBufferAlbedoSlot + kGBufferViewCount - 1);
}
