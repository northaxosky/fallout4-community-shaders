#pragma once

#include <cstddef>
#include <cstdint>

namespace cs::features::upscaling_anchors
{
	// DrawWorld::Begin selects the world viewport before updating temporal jitter.
		inline constexpr std::uint64_t kDrawWorldBegin[] = { 502840, 2318286, 2318286 };
		inline constexpr std::ptrdiff_t kDrawWorldBeginSetDynamicViewportCall = 0x26;
		inline constexpr std::ptrdiff_t kDrawWorldBeginUpdateTemporalDataCall[] = { 0x3C1, 0x38C, 0x38C };
		inline constexpr std::uint64_t kSetDynamicViewportAsDefault[] = { 676851, 2277194, 2277194 };

		// The first-person alpha call site around RenderAlphaGeometry. OG factors this work into
		// DrawWorld::ForwardAlphaImpl, which NG/AE inlined into DrawWorld::Forward; shared landmarks sit
		// at a constant +0x2EA displacement, so the offset is common and only the anchor differs.
		inline constexpr std::uint64_t kFirstPersonAlphaAnchor[] = { 338205, 2318315, 2318315 };
		inline constexpr std::ptrdiff_t kFirstPersonAlphaCall[] = { 0x253, 0x53D, 0x53D };
		inline constexpr std::uint64_t kRenderAlphaGeometry[] = { 1445970, 2317903, 2317903 };

		// DrawWorld::Render_UI is the live post-process and UI wrapper.
		inline constexpr std::uint64_t kDrawWorldRenderUI[] = { 587723, 2318322, 2318322 };
		// cmp byte ptr [rip+disp32], 0 selects full effects or Gamma-only processing.
		inline constexpr std::ptrdiff_t kDrawWorldRenderUIEffectsGateCompare[] = { 0x55, 0x47, 0x47 };
		inline constexpr std::uint64_t kDrawWorldRenderUIEffectsGate[] = { 1327954, 4784523, 4784523 };
		// RenderEffectRange splits the effect chain into HDR and LDR passes.
		inline constexpr std::ptrdiff_t kDrawWorldRenderUIRenderEffectRangeCall[] = { 0x9F, 0x83, 0x83 };
		inline constexpr std::uint64_t kImageSpaceManagerRenderEffectRange[] = { 459505, 2316593, 2316593 };
		// SetUseDynamicResolutionViewportAsDefaultViewport follows the effect chain; the upscale resolves here.
		inline constexpr std::ptrdiff_t kDrawWorldRenderUIResolveCall[] = { 0xE1, 0xC5, 0xC5 };

		// BSDFComposite (deferred lighting composite) samples dynamic-resolution G-buffers.
		inline constexpr std::uint64_t kDeferredComposite[] = { 728427, 2318313, 2318313 };
		inline constexpr std::ptrdiff_t kDeferredCompositeRenderPassCall[] = { 0x8DC, 0x915, 0x915 };
		inline constexpr std::uint64_t kBSBatchRendererRenderPassImmediately[] = { 244233, 2318699, 2318699 };

		// Lens-flare visibility read samples the main depth buffer.
		inline constexpr std::uint64_t kLensFlareRenderLensFlare[] = { 676108, 2317547, 2317547 };

		// Screen-space reflection raytracing needs a reconstructed shader for scaled targets.
		inline constexpr std::uint64_t kSSLRRaytracingSetupTechnique[] = { 395020, 2317302, 2317302 };
		inline constexpr std::ptrdiff_t kSSLRRaytracingBeginTechniqueCall = 0x1C;
		inline constexpr std::uint64_t kBSShaderBeginTechnique[] = { 1041640, 2318876, 2318876 };

		// VATS outline thickness derives from a pixel constant scaled by the dynamic ratio.
		inline constexpr std::uint64_t kVatsUpdateParams[] = { 1042583, 2317983, 2317983 };
		inline constexpr std::ptrdiff_t kVatsSetPixelConstantCall[] = { 0xBB, 0x110, 0x110 };
		inline constexpr std::uint64_t kImageSpaceShaderParamSetPixelConstant[] = { 959652, 2317840, 2317840 };

		// LoadingMenu renders without dynamic resolution, so its jitter must be neutralized.
		inline constexpr std::uint64_t kLoadingMenuUpdateTemporalData[] = { 135719, 2249225, 2249225 };
		inline constexpr std::ptrdiff_t kLoadingMenuUpdateTemporalDataCall[] = { 0x2BD, 0x275, 0x275 };
		inline constexpr std::uint64_t kBSGraphicsStateUpdateTemporalData[] = { 376068, 2277095, 2277095 };

		// Render_PreUI drives vanilla dynamic resolution before imagespace geometry updates.
		inline constexpr std::uint64_t kRenderPreUI[] = { 984743, 2318321, 2318321 };
		inline constexpr std::ptrdiff_t kRenderPreUIUpdateDynamicResolutionCall[] = { 0x14B, 0x29F, 0x29F };
		inline constexpr std::uint64_t kUpdateDynamicResolution[] = { 1115215, 2277195, 2277195 };
		// Deferred-prepass and forward passes sample materials; bracket them with the biased sampler table.
		inline constexpr std::ptrdiff_t kRenderPreUIDeferredPrePassCall[] = { 0x17F, 0x2E3, 0x2E3 };
		inline constexpr std::uint64_t kDrawWorldDeferredPrePass[] = { 56596, 2318301, 2318301 };
		inline constexpr std::ptrdiff_t kRenderPreUIForwardCall[] = { 0x1C9, 0x3A6, 0x3A6 };
		inline constexpr std::uint64_t kRenderPreUIForwardTarget[] = { 656535, 2318315, 2318315 };
		inline constexpr std::uint64_t kSamplerStateTable[] = { 44312, 2704455, 2704455 };

	// Resource setup follows creation of RT4 and the remaining engine targets.
	inline constexpr std::uint64_t kBSShaderRenderTargetsCreate[] = { 1118299, 2318909, 2318909 };

	inline constexpr std::uint64_t kImageSpaceInitEffects[] = { 889489, 2316625, 2316625 };

	inline constexpr std::uint64_t kForceViewportToRenderTargetDimensions[] = { 1208720, 2277193, 2277193 };

	// ResetWindow can run without BSShaderRenderTargets::Create.
	inline constexpr std::uint64_t kRendererResetWindow[] = { 796949, 2276825, 2276825 };
}
