#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace cs::engine
{
	// Native names whose stock imagespace VS is the shared fullscreen passthrough.
	inline constexpr std::array<std::string_view, 93> kImageSpacePassthroughVertexNames{
		"ISAO",
		"ISAOGauss",
		"ISAlphaBlend",
		"ISBlur11",
		"ISBlur13",
		"ISBlur15",
		"ISBlur3",
		"ISBlur5",
		"ISBlur7",
		"ISBlur9",
		"ISBokehBlurHoriz",
		"ISBokehBlurVert",
		"ISBrightPassBlur11",
		"ISBrightPassBlur13",
		"ISBrightPassBlur15",
		"ISBrightPassBlur3",
		"ISBrightPassBlur5",
		"ISBrightPassBlur7",
		"ISBrightPassBlur9",
		"ISCopy",
		"ISCopyDownsampleDepth",
		"ISCopyGreyScale",
		"ISCopyNormals",
		"ISCopyShadowMapToArray",
		"ISCopyStencil",
		"ISCopyVisAlpha",
		"ISCopyWaterMask",
		"ISDepthOfField",
		"ISDepthOfFieldFogged",
		"ISDepthOfFieldSplitScreen",
		"ISDistantBlur",
		"ISDistantBlurFogged",
		"ISDoubleVision",
		"ISFXAA",
		"ISFullScreenColor",
		"ISGamma",
		"ISGammaLUT",
		"ISGammaLinearize",
		"ISHDRDownSample16",
		"ISHDRDownSample16LightAdapt",
		"ISHDRDownSample16Lum",
		"ISHDRDownSample16LumClamp",
		"ISHDRDownSample4",
		"ISHDRDownSample4LightAdapt",
		"ISHDRDownSample4LumClamp",
		"ISHDRDownSample4RGB2Lum",
		"ISHDRTonemapBlendCinematic",
		"ISHDRTonemapBlendCinematicFade",
		"ISLocalMap",
		"ISLocalMapCompanion",
		"ISModMenu",
		"ISModMenuGlow",
		"ISMotionBlur",
		"ISNonHDRBlur11",
		"ISNonHDRBlur13",
		"ISNonHDRBlur15",
		"ISNonHDRBlur3",
		"ISNonHDRBlur5",
		"ISNonHDRBlur7",
		"ISNonHDRBlur9",
		"ISPipboyScreen",
		"ISRadialBlur",
		"ISRadialBlurHigh",
		"ISRadialBlurMedium",
		"ISRefraction",
		"ISSAOBlurH",
		"ISSAOBlurV",
		"ISSAOCameraZ",
		"ISSAOMinify",
		"ISSAORawAO",
		"ISSAORawAOEditor",
		"ISSSLRPrepass",
		"ISSSLRRaytracing",
		"ISSunbeams",
		"ISTemporalAA",
		"ISTemporalAAPipboy",
		"ISTemporalAAPowerArmorPipboy",
		"ISUpsampleDynamicResolution",
		"ISVLSApplication",
		"ISVLSComposite",
		"ISVLSCoord",
		"ISVLSSliceInterp",
		"ISVLSSliceScatterInterp",
		"ISVLSSliceScatterRay",
		"ISVLSSliceStencil",
		"ISVLSSpotLight",
		"ISVatsTarget",
		"ISVatsTargetDebug",
		"ISWaterBlendHeightmaps",
		"ISWaterDisplacementClearSimulation",
		"ISWaterDisplacementNormals",
		"ISWaterDisplacementRainRipple",
		"ISWaterDisplacementTexOffset",
	};

	static_assert(std::ranges::is_sorted(kImageSpacePassthroughVertexNames));

	// The only passthrough route whose output signature widens with TEXCOORD1.
	inline constexpr std::string_view kImageSpacePassthroughTexCoord1Name = "ISFXAA";

	// OG passthrough routes whose VS also reads the base GeometryOffset constant.
	inline constexpr std::array<std::string_view, 19> kImageSpacePassthroughGeometryOffsetNames{
		"ISAO",
		"ISAOGauss",
		"ISFullScreenColor",
		"ISLocalMap",
		"ISLocalMapCompanion",
		"ISModMenu",
		"ISModMenuGlow",
		"ISMotionBlur",
		"ISPipboyScreen",
		"ISSAOBlurH",
		"ISSAOBlurV",
		"ISSAOCameraZ",
		"ISSAOMinify",
		"ISSAORawAO",
		"ISSAORawAOEditor",
		"ISSunbeams",
		"ISUpsampleDynamicResolution",
		"ISVatsTarget",
		"ISVatsTargetDebug",
	};

	static_assert(std::ranges::is_sorted(kImageSpacePassthroughGeometryOffsetNames));

	// Position-only quads sharing the passthrough source; Markers also packs a TEXCOORD0.
	inline constexpr std::array<std::string_view, 2> kImageSpaceXYQuadVertexNames{
		"ISHUDGlassMarkers",
		"LensFlareVis",
	};

	static_assert(std::ranges::is_sorted(kImageSpaceXYQuadVertexNames));

	inline constexpr std::string_view kImageSpaceXYQuadPackedName = "ISHUDGlassMarkers";

	[[nodiscard]] constexpr bool IsImageSpacePassthroughVertex(std::string_view a_nativeName) noexcept
	{
		return std::ranges::binary_search(kImageSpacePassthroughVertexNames, a_nativeName);
	}

	[[nodiscard]] constexpr bool IsImageSpacePassthroughGeometryOffsetVertex(std::string_view a_nativeName) noexcept
	{
		return std::ranges::binary_search(kImageSpacePassthroughGeometryOffsetNames, a_nativeName);
	}

	[[nodiscard]] constexpr bool IsImageSpaceXYQuadVertex(std::string_view a_nativeName) noexcept
	{
		return std::ranges::binary_search(kImageSpaceXYQuadVertexNames, a_nativeName);
	}
}
