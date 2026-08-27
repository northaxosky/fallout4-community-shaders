#pragma once

#include <cstddef>
#include <cstdint>

namespace cs::features::upscaling_anchors
{
	// NG shares AE's address space; OG remains unsupported.
	inline constexpr std::uint64_t kUnprovenOnOG = 0;

	// DrawWorld::Begin selects the world viewport before updating temporal jitter.
	inline constexpr std::uint64_t kDrawWorldBegin = 2318286;
	inline constexpr std::ptrdiff_t kDrawWorldBeginSetDynamicViewportCall = 0x26;
	inline constexpr std::ptrdiff_t kDrawWorldBeginUpdateTemporalDataCall = 0x38C;
	inline constexpr std::uint64_t kSetDynamicViewportAsDefault = 2277194;

	// DrawWorld::Forward brackets normal first-person alpha around RenderAlphaGeometry.
	inline constexpr std::uint64_t kDrawWorldForward[] = { 656535, 2318315, 2318315 };
	inline constexpr std::ptrdiff_t kFirstPersonAlphaCall[] = { 0x253, 0x53D, 0x53D };
	inline constexpr std::uint64_t kRenderAlphaGeometry[] = { 0, 2317903, 2317903 };

	// MainDrawWorldAndUI calls DrawWorld::Imagespace; scoping it excludes pause-only rendering.
	inline constexpr std::uint64_t kMainDrawWorldAndUI = 2228969;
	inline constexpr std::ptrdiff_t kMainDrawWorldAndUIImagespaceCall = 0x182;

	// DrawWorld::Imagespace runs the post-process effect chain, then hands the viewport back.
	inline constexpr std::uint64_t kDrawWorldImagespace = 2318322;
	// RenderEffectRange (+0x83) splits the effect chain into HDR and LDR passes.
	inline constexpr std::ptrdiff_t kDrawWorldImagespaceRenderEffectRangeCall = 0x83;
	// SetUseDynamicResolutionViewportAsDefaultViewport (+0xC5) follows the effect chain; the upscale resolves here.
	inline constexpr std::ptrdiff_t kDrawWorldImagespaceUpscaleCall = 0xC5;

	// BSDFComposite (deferred lighting composite) samples dynamic-resolution G-buffers.
	inline constexpr std::uint64_t kDeferredComposite = 2318313;
	inline constexpr std::ptrdiff_t kDeferredCompositeRenderPassCall = 0x915;

	// Lens-flare visibility read samples the main depth buffer.
	inline constexpr std::uint64_t kLensFlareRenderLensFlare = 2317547;

	// Screen-space reflection raytracing needs a reconstructed shader for scaled targets.
	inline constexpr std::uint64_t kSSLRRaytracingSetupTechnique = 2317302;
	inline constexpr std::ptrdiff_t kSSLRRaytracingBeginTechniqueCall = 0x1C;

	// VATS outline thickness derives from a pixel constant scaled by the dynamic ratio.
	inline constexpr std::uint64_t kVatsUpdateParams = 2317983;
	inline constexpr std::ptrdiff_t kVatsSetPixelConstantCall = 0x110;

	// LoadingMenu renders without dynamic resolution, so its jitter must be neutralized.
	inline constexpr std::uint64_t kLoadingMenuUpdateTemporalData = 2249225;
	inline constexpr std::ptrdiff_t kLoadingMenuUpdateTemporalDataCall = 0x275;

	// Render_PreUI drives vanilla dynamic resolution before imagespace geometry updates.
	inline constexpr std::uint64_t kRenderPreUI = 2318321;
	inline constexpr std::ptrdiff_t kRenderPreUIUpdateDynamicResolutionCall = 0x29F;
	inline constexpr std::uint64_t kUpdateDynamicResolution = 2277195;

	// Resource setup follows creation of RT4 and the remaining engine targets.
	inline constexpr std::uint64_t kBSShaderRenderTargetsCreate = 2318909;

	inline constexpr std::uint64_t kImageSpaceInitEffects = 2316625;

	inline constexpr std::uint64_t kForceViewportToRenderTargetDimensions = 2277193;

	// ResetWindow can run without BSShaderRenderTargets::Create.
	inline constexpr std::uint64_t kRendererResetWindow = 2276825;
}
