#pragma once

#include "Render/EngineCallSite.h"

namespace cs::features::upscaling_anchors
{
	using engine::CallSiteAnchor;

	inline constexpr REL::ID kDrawWorldBegin{ 502840, 2318286, 2318286 };
	inline constexpr REL::ID kDrawWorldRenderUI{ 587723, 2318322, 2318322 };
	inline constexpr REL::ID kRenderPreUI{ 984743, 2318321, 2318321 };
	inline constexpr REL::ID kSetDynamicViewportAsDefault{ 676851, 2277194, 2277194 };
	inline constexpr REL::ID kUpdateTemporalData{ 376068, 2277095, 2277095 };

	// DrawWorld::Begin selects the world viewport before updating temporal jitter.
	inline constexpr CallSiteAnchor kDrawWorldBeginSetDynamicViewport{
		.name = "DrawWorld::Begin -> SetUseDynamicResolutionViewportAsDefaultViewport",
		.function = kDrawWorldBegin,
		.offset = { 0x26, 0x26, 0x26 },
		.target = kSetDynamicViewportAsDefault
	};
	inline constexpr CallSiteAnchor kDrawWorldBeginUpdateTemporalData{
		.name = "DrawWorld::Begin -> BSGraphics::State::UpdateTemporalData",
		.function = kDrawWorldBegin,
		.offset = { 0x3C1, 0x38C, 0x38C },
		.target = kUpdateTemporalData
	};

	// OG splits first-person alpha into ForwardAlphaImpl; NG/AE inline it.
	inline constexpr CallSiteAnchor kFirstPersonAlphaRenderAlphaGeometry{
		.name = "DrawWorld first-person alpha -> RenderAlphaGeometry",
		.function = REL::ID({ 338205, 2318315, 2318315 }),
		.offset = { 0x253, 0x53D, 0x53D },
		.target = REL::ID({ 1445970, 2317903, 2317903 })
	};

	// Selects full effects or gamma-only processing.
	inline constexpr engine::RuntimeOffsets kDrawWorldRenderUIEffectsGateCompare{ 0x55, 0x47, 0x47 };
	inline constexpr REL::ID kDrawWorldRenderUIEffectsGate{ 1327954, 2677855, 4784523 };
	// RenderEffectRange splits the effect chain into HDR and LDR passes.
	inline constexpr CallSiteAnchor kDrawWorldRenderUIRenderEffectRange{
		.name = "DrawWorld::Render_UI -> ImageSpaceManager::RenderEffectRange",
		.function = kDrawWorldRenderUI,
		.offset = { 0x9F, 0x83, 0x83 },
		.target = REL::ID({ 459505, 2316593, 2316593 })
	};
	// Upscale resolves after the effect chain.
	inline constexpr CallSiteAnchor kDrawWorldRenderUIResolve{
		.name = "DrawWorld::Render_UI -> SetUseDynamicResolutionViewportAsDefaultViewport",
		.function = kDrawWorldRenderUI,
		.offset = { 0xE1, 0xC5, 0xC5 },
		.target = kSetDynamicViewportAsDefault
	};

	// BSDFComposite samples dynamic-resolution G-buffers.
	inline constexpr CallSiteAnchor kDeferredCompositeRenderPass{
		.name = "DrawWorld::DeferredComposite -> BSBatchRenderer::RenderPassImmediately",
		.function = REL::ID({ 728427, 2318313, 2318313 }),
		.offset = { 0x8DC, 0x915, 0x915 },
		.target = REL::ID({ 244233, 2318699, 2318699 })
	};

	// Lens-flare visibility read samples the main depth buffer.
	inline constexpr REL::ID kLensFlareRenderLensFlare{ 676108, 2317547, 2317547 };

	// VATS outline thickness scales with the dynamic ratio.
	inline constexpr CallSiteAnchor kVatsSetPixelConstant{
		.name = "VATS UpdateParams -> ImageSpaceShaderParam::SetPixelConstant",
		.function = REL::ID({ 1042583, 2317983, 2317983 }),
		.offset = { 0xBB, 0x110, 0x110 },
		.target = REL::ID({ 959652, 2317840, 2317840 })
	};

	// LoadingMenu renders unscaled; neutralize its jitter.
	inline constexpr CallSiteAnchor kLoadingMenuUpdateTemporalData{
		.name = "LoadingMenu -> BSGraphics::State::UpdateTemporalData",
		.function = REL::ID({ 135719, 2249225, 2249225 }),
		.offset = { 0x2BD, 0x275, 0x275 },
		.target = kUpdateTemporalData
	};

	// Render_PreUI drives vanilla dynamic resolution.
	inline constexpr CallSiteAnchor kRenderPreUIUpdateDynamicResolution{
		.name = "DrawWorld::Render_PreUI -> UpdateDynamicResolution",
		.function = kRenderPreUI,
		.offset = { 0x14B, 0x29F, 0x29F },
		.target = REL::ID({ 1115215, 2277195, 2277195 })
	};
	// Material passes use the biased sampler table.
	inline constexpr CallSiteAnchor kRenderPreUIDeferredPrePass{
		.name = "DrawWorld::Render_PreUI -> DrawWorld::DeferredPrePass",
		.function = kRenderPreUI,
		.offset = { 0x17F, 0x2E3, 0x2E3 },
		.target = REL::ID({ 56596, 2318301, 2318301 })
	};
	inline constexpr CallSiteAnchor kRenderPreUIForward{
		.name = "DrawWorld::Render_PreUI -> DrawWorld::Forward",
		.function = kRenderPreUI,
		.offset = { 0x1C9, 0x3A6, 0x3A6 },
		.target = REL::ID({ 656535, 2318315, 2318315 })
	};
	inline constexpr REL::ID kSamplerStateTable{ 44312, 2704455, 2704455 };

	// Resource setup follows creation of the engine render targets.
	inline constexpr REL::ID kBSShaderRenderTargetsCreate{ 1118299, 2318909, 2318909 };

	inline constexpr REL::ID kImageSpaceInitEffects{ 889489, 2316625, 2316625 };

	inline constexpr REL::ID kForceViewportToRenderTargetDimensions{ 1208720, 2277193, 2277193 };

	// ResetWindow can run without BSShaderRenderTargets::Create.
	inline constexpr REL::ID kRendererResetWindow{ 796949, 2276825, 2276825 };
}