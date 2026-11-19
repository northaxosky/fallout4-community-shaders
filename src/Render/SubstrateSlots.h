#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs::render
{
	enum class SlotKind : std::uint8_t
	{
		kConstantBuffer,
		kTexture,
		kSampler,
		// Output slot; no reflected name, so it is never validated.
		kRenderTarget
	};

	enum SlotStage : std::uint8_t
	{
		kSlotVertex = 1 << 0,
		kSlotPixel = 1 << 1,
		kSlotCompute = 1 << 2,
		kSlotAnyStage = kSlotVertex | kSlotPixel | kSlotCompute
	};

	// Consecutive slots of one owner; exclusive runs reject unlisted names.
	struct SlotBlock
	{
		static constexpr std::size_t kMaxCount = 4;

		SlotKind kind;
		std::uint8_t stages;
		bool exclusive;
		std::uint32_t first;
		std::uint32_t count;
		std::array<std::string_view, kMaxCount> names;

		template <std::size_t N>
		constexpr SlotBlock(SlotKind a_kind, std::uint8_t a_stages, bool a_exclusive, std::uint32_t a_first,
			const std::string_view (&a_names)[N]) noexcept :
			kind(a_kind),
			stages(a_stages),
			exclusive(a_exclusive),
			first(a_first),
			count(static_cast<std::uint32_t>(N)),
			names{}
		{
			static_assert(N <= kMaxCount);
			for (std::size_t i = 0; i < N; ++i)
				names[i] = a_names[i];
		}

		constexpr SlotBlock(SlotKind a_kind, std::uint8_t a_stages, std::uint32_t a_first, std::uint32_t a_count = 1) noexcept :
			kind(a_kind),
			stages(a_stages),
			exclusive(false),
			first(a_first),
			count(a_count),
			names{}
		{}

		constexpr operator std::uint32_t() const noexcept { return first; }
		constexpr std::uint32_t last() const noexcept { return first + count - 1; }
	};

	// Host-owned, every stage.
	inline constexpr std::uint32_t kFrameDataSlot = 4;
	inline constexpr std::uint32_t kSharedDataSlot = 5;
	inline constexpr std::uint32_t kFeatureDataSlot = 6;
	inline constexpr std::uint32_t kFO4SharedDataSlot = 7;
	inline constexpr std::uint32_t kSubstrateBufferCount = 4;
	inline constexpr SlotBlock kSubstrateBuffers{ SlotKind::kConstantBuffer, kSlotAnyStage, true, kFrameDataSlot,
		{ "PerFrame", "SharedData", "FeatureData", "FO4SharedData" } };
	inline constexpr SlotBlock kCanonicalDepthSlot{ SlotKind::kTexture, kSlotAnyStage, true, 17, { "DepthTexture" } };
	inline constexpr SlotBlock kFullscreenDebugTextureSlot{ SlotKind::kTexture, kSlotAnyStage, true, 61, { "DebugTexture" } };

	// Pixel, host-owned.
	// Per-draw; TerrainBlending reuses b8 in a disjoint PS.
	inline constexpr SlotBlock kForwardDrawSlot{ SlotKind::kConstantBuffer, kSlotPixel, false, 8, { "PerShader" } };
	// Engine owns s0-s15; ShaderInjection binds this per draw, never per frame.
	inline constexpr SlotBlock kLinearClampSamplerSlot{ SlotKind::kSampler, kSlotPixel, false, 13, { "LinearSampler" } };
	// Stock Sky reads the world depth at the canonical slot under its own name.
	inline constexpr SlotBlock kSkyDepthSlot{ SlotKind::kTexture, kSlotPixel, true, 17, { "TexDepthSampler" } };

	// Pixel, by owner.
	inline constexpr SlotBlock kCloudShadowsSlot{ SlotKind::kTexture, kSlotPixel, true, 25, { "CloudShadowsTexture" } };
	inline constexpr SlotBlock kCloudLayerCubeSlot{ SlotKind::kTexture, kSlotPixel, false, 26, { "CloudLayerTexture" } };
	inline constexpr SlotBlock kCloudOverlayConstantsSlot{ SlotKind::kConstantBuffer, kSlotPixel, false, 8, { "Overlay" } };
	// Reflections, environment, then the cube water and SSLR name themselves.
	inline constexpr SlotBlock kDynamicCubemapsSlot{ SlotKind::kTexture, kSlotPixel, true, 30,
		{ "EnvReflectionsTexture", "EnvTexture", "" } };
	// Environment, reflections, then the box-mipped sharp twin.
	inline constexpr SlotBlock kDynamicCubemapsCompositionSlot{ SlotKind::kTexture, kSlotPixel, true, 39,
		{ "EnvTexture", "EnvReflectionsTexture", "SharpReflectionsTexture" } };
	inline constexpr SlotBlock kParallaxShadowSlot{ SlotKind::kTexture, kSlotPixel, true, 66, { "ParallaxShadowTexture" } };
	inline constexpr SlotBlock kLlfStrictSlot{ SlotKind::kConstantBuffer, kSlotPixel, false, 3, { "StrictLightData" } };
	inline constexpr SlotBlock kLlfRasterSlot{ SlotKind::kConstantBuffer, kSlotPixel, false, 11, { "LightLimitFixDraw" } };
	inline constexpr SlotBlock kLlfForwardSlot{ SlotKind::kTexture, kSlotPixel, true, 34,
		{ "Cones", "lights", "lightList", "lightGrid" } };
	inline constexpr SlotBlock kLlfLocalShadowSlot{ SlotKind::kTexture, kSlotPixel, true, 102,
		{ "LocalShadows", "LocalShadowMaps" } };
	// FO4: native composite inputs hold the low slots; keep main's t26-t29.
	inline constexpr SlotBlock kSsgiCompositionSlot{ SlotKind::kTexture, kSlotPixel, true, 26,
		{ "OcclusionTexture", "BounceLumaTexture", "BounceChromaTexture", "NormalTexture" } };
	inline constexpr SlotBlock kSsgiSpecularSlot{ SlotKind::kTexture, kSlotPixel, true, 38, { "SpecularTexture" } };
	inline constexpr SlotBlock kSssConsumerSlot{ SlotKind::kConstantBuffer, kSlotPixel, true, 9, { "Consumer" } };
	inline constexpr SlotBlock kScreenSpaceShadowsMaskSlot{ SlotKind::kTexture, kSlotPixel, true, 45,
		{ "ScreenSpaceShadowsTexture" } };
	inline constexpr SlotBlock kTerrainBlendingObjectDepthSlot{ SlotKind::kTexture, kSlotPixel, true, 55,
		{ "TerrainBlendingMaskTexture" } };
	inline constexpr SlotBlock kTerrainBlendingReplaySlot{ SlotKind::kConstantBuffer, kSlotPixel, false, 8,
		{ "TerrainBlendingDraw" } };
	inline constexpr SlotBlock kVanillaPbrLiveLODSlot{ SlotKind::kTexture, kSlotPixel, true, 42, { "LiveLODTexture" } };
	// Stock DFComposite's probe cube.
	inline constexpr SlotBlock kVanillaPbrAuthoredCubeSlot{ SlotKind::kTexture, kSlotPixel, false, 8, { "g_tIblProbeCube" } };
	inline constexpr SlotBlock kVolumetricShadowsMapSlot{ SlotKind::kTexture, kSlotPixel, true, 18, { "SharedShadowMap" } };
	inline constexpr SlotBlock kVolumetricShadowsLightsSlot{ SlotKind::kTexture, kSlotPixel, true, 98,
		{ "DirectionalShadowLights" } };
	inline constexpr SlotBlock kWaterCausticsSlot{ SlotKind::kTexture, kSlotPixel, true, 65, { "WaterCaustics" } };
	inline constexpr SlotBlock kWaterCausticsSamplerSlot{ SlotKind::kSampler, kSlotPixel, false, 14, { "SampColorSampler" } };

	// Pixel and compute, by owner.
	inline constexpr SlotBlock kIblSHSlot{ SlotKind::kTexture, kSlotPixel | kSlotCompute, true, 76,
		{ "EnvIBLTexture", "SkyIBLTexture" } };
	inline constexpr SlotBlock kSsgiConsumerSlot{ SlotKind::kConstantBuffer, kSlotPixel | kSlotCompute, true, 10, { "Consumer" } };
	// Probes, albedo, vertex AO (MRT4), shadow visibility.
	inline constexpr SlotBlock kSkylightingProbeSlot{ SlotKind::kTexture, kSlotPixel | kSlotCompute, true, 50,
		{ "SkylightingProbeArray", "AlbedoTexture", "VertexAOTexture", "ShadowVisibilityProbeArray" } };
	inline constexpr SlotBlock kSkylightingCoverageSlot{ SlotKind::kTexture, kSlotPixel | kSlotCompute, true, 56,
		{ "SkylightingProbeCoverage", "SkylightingZenithDepth" } };
	inline constexpr SlotBlock kTerrainShadowsHeightSlot{ SlotKind::kTexture, kSlotPixel | kSlotCompute, true, 60,
		{ "ShadowHeightTexture" } };

	// Compute, by owner.
	inline constexpr SlotBlock kLlfTiledSlot{ SlotKind::kTexture, kSlotCompute, false, 8, { "LightLimitFixLights" } };
	inline constexpr SlotBlock kSssConstantsSlot{ SlotKind::kConstantBuffer, kSlotCompute, 1 };

	// Render targets, by owner.
	inline constexpr SlotBlock kCloudCaptureTargetSlot{ SlotKind::kRenderTarget, kSlotPixel, 2, 2 };
	inline constexpr SlotBlock kParallaxShadowTargetSlot{ SlotKind::kRenderTarget, kSlotPixel, 6 };
	inline constexpr SlotBlock kSssProfileTargetSlot{ SlotKind::kRenderTarget, kSlotPixel, 7 };

	// Every named run; ValidateSubstrateSlots checks reflected names against it.
	inline constexpr std::array kSlotTable{
		&kSubstrateBuffers, &kCanonicalDepthSlot, &kFullscreenDebugTextureSlot, &kForwardDrawSlot,
		&kLinearClampSamplerSlot, &kSkyDepthSlot, &kCloudShadowsSlot, &kCloudLayerCubeSlot,
		&kCloudOverlayConstantsSlot, &kDynamicCubemapsSlot, &kDynamicCubemapsCompositionSlot,
		&kParallaxShadowSlot, &kLlfStrictSlot, &kLlfRasterSlot, &kLlfForwardSlot, &kLlfLocalShadowSlot,
		&kSsgiCompositionSlot, &kSsgiSpecularSlot, &kSssConsumerSlot, &kScreenSpaceShadowsMaskSlot,
		&kTerrainBlendingObjectDepthSlot, &kTerrainBlendingReplaySlot, &kVanillaPbrLiveLODSlot,
		&kVanillaPbrAuthoredCubeSlot, &kVolumetricShadowsMapSlot, &kVolumetricShadowsLightsSlot,
		&kWaterCausticsSlot, &kWaterCausticsSamplerSlot, &kIblSHSlot, &kSsgiConsumerSlot,
		&kSkylightingProbeSlot, &kSkylightingCoverageSlot, &kTerrainShadowsHeightSlot, &kLlfTiledSlot
	};
}
