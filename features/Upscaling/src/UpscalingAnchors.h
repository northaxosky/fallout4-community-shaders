#pragma once

#include "Render/EngineCallSite.h"

namespace cs::features::upscaling_anchors
{
	using engine::CallSiteAnchor;

	// DrawWorld::Begin selects the world viewport before updating temporal jitter.
	inline constexpr CallSiteAnchor kDrawWorldBeginSetDynamicViewport{
		.name = "DrawWorld::Begin -> SetUseDynamicResolutionViewportAsDefaultViewport",
		.function = RE::ID::DrawWorld::Begin,
		.offset = { 0x26, 0x26, 0x26 },
		.target = RE::ID::BSGraphics::RenderTargetManager::SetUseDynamicResolutionViewportAsDefaultViewport
	};
	inline constexpr CallSiteAnchor kDrawWorldBeginUpdateTemporalData{
		.name = "DrawWorld::Begin -> BSGraphics::State::UpdateTemporalData",
		.function = RE::ID::DrawWorld::Begin,
		.offset = { 0x3C1, 0x38C, 0x38C },
		.target = RE::ID::BSGraphics::State::UpdateTemporalData
	};

	// OG splits first-person alpha into ForwardAlphaImpl; NG/AE inline it.
	inline constexpr CallSiteAnchor kFirstPersonAlphaRenderAlphaGeometry{
		.name = "DrawWorld first-person alpha -> RenderAlphaGeometry",
		.function = REL::VariantID{ 338205, 2318315 },
		.offset = { 0x253, 0x53D, 0x53D },
		.target = RE::ID::BSShaderAccumulator::RenderAlphaGeometry
	};

	// Selects full effects or gamma-only processing.
	inline constexpr engine::RuntimeOffsets kDrawWorldRenderUIEffectsGateCompare{ 0x55, 0x47, 0x47 };
	inline constexpr REL::VariantID kDrawWorldRenderUIEffectsGate{ 1327954, 2677855, 4784523 };
	// RenderEffectRange splits the effect chain into HDR and LDR passes.
	inline constexpr CallSiteAnchor kDrawWorldRenderUIRenderEffectRange{
		.name = "DrawWorld::Render_UI -> ImageSpaceManager::RenderEffectRange",
		.function = RE::ID::DrawWorld::Imagespace,
		.offset = { 0x9F, 0x83, 0x83 },
		.target = RE::ID::ImageSpaceManager::RenderEffectRange
	};
	// Upscale resolves after the effect chain.
	inline constexpr CallSiteAnchor kDrawWorldRenderUIResolve{
		.name = "DrawWorld::Render_UI -> SetUseDynamicResolutionViewportAsDefaultViewport",
		.function = RE::ID::DrawWorld::Imagespace,
		.offset = { 0xE1, 0xC5, 0xC5 },
		.target = RE::ID::BSGraphics::RenderTargetManager::SetUseDynamicResolutionViewportAsDefaultViewport
	};

	// BSDFComposite samples dynamic-resolution G-buffers.
	inline constexpr CallSiteAnchor kDeferredCompositeRenderPass{
		.name = "DrawWorld::DeferredComposite -> BSBatchRenderer::RenderPassImmediately",
		.function = RE::ID::DrawWorld::DeferredComposite,
		.offset = { 0x8DC, 0x915, 0x915 },
		.target = RE::ID::BSBatchRenderer::RenderPassImmediately
	};

	// The stock SSLR chain between these sites assumes TexCoord spans [0, 1].
	inline constexpr CallSiteAnchor kDeferredCompositeSSLRBegin{
		.name = "DrawWorld::DeferredComposite -> RenderTargetManager::SetCurrentRenderTarget (SSLR ray start)",
		.function = RE::ID::DrawWorld::DeferredComposite,
		.offset = { 0x3D9, 0x3EE, 0x3EE },
		.target = RE::ID::BSGraphics::RenderTargetManager::SetCurrentRenderTarget
	};
	// BlurH writes a pooled target that only exists after this acquire.
	inline constexpr CallSiteAnchor kDeferredCompositeSSLRBlurHAcquire{
		.name = "DrawWorld::DeferredComposite -> RenderTargetManager::AcquireRenderTarget (SSLR BlurH)",
		.function = RE::ID::DrawWorld::DeferredComposite,
		.offset = { 0x5FB, 0x60E, 0x60E },
		.target = RE::ID::BSGraphics::RenderTargetManager::AcquireRenderTarget
	};
	// BlurV is done; the pooled target is still mapped until this release.
	inline constexpr CallSiteAnchor kDeferredCompositeSSLRBlurHRelease{
		.name = "DrawWorld::DeferredComposite -> RenderTargetManager::ReleaseRenderTarget (SSLR BlurH)",
		.function = RE::ID::DrawWorld::DeferredComposite,
		.offset = { 0x6BA, 0x6C3, 0x6C3 },
		.target = RE::ID::BSGraphics::RenderTargetManager::ReleaseRenderTarget
	};

	// VATS outline thickness scales with the dynamic ratio.
	inline constexpr CallSiteAnchor kVatsSetPixelConstant{
		.name = "VATS UpdateParams -> ImageSpaceShaderParam::SetPixelConstant",
		.function = RE::ID::ImageSpaceEffectVatsTarget::UpdateParams,
		.offset = { 0xBB, 0x110, 0x110 },
		.target = RE::ID::ImageSpaceShaderParam::SetPixelConstant
	};

	// LoadingMenu renders unscaled; neutralize its jitter.
	inline constexpr CallSiteAnchor kLoadingMenuUpdateTemporalData{
		.name = "LoadingMenu -> BSGraphics::State::UpdateTemporalData",
		.function = RE::ID::LoadingMenu::Render,
		.offset = { 0x2BD, 0x275, 0x275 },
		.target = RE::ID::BSGraphics::State::UpdateTemporalData
	};

	// Render_PreUI drives vanilla dynamic resolution.
	inline constexpr CallSiteAnchor kRenderPreUIUpdateDynamicResolution{
		.name = "DrawWorld::Render_PreUI -> UpdateDynamicResolution",
		.function = RE::ID::DrawWorld::Render_PreUI,
		.offset = { 0x14B, 0x29F, 0x29F },
		.target = RE::ID::BSGraphics::RenderTargetManager::UpdateDynamicResolution
	};
	// Material passes use the biased sampler table.
	inline constexpr CallSiteAnchor kRenderPreUIDeferredPrePass{
		.name = "DrawWorld::Render_PreUI -> DrawWorld::DeferredPrePass",
		.function = RE::ID::DrawWorld::Render_PreUI,
		.offset = { 0x17F, 0x2E3, 0x2E3 },
		.target = RE::ID::DrawWorld::DeferredPrePass
	};
	inline constexpr CallSiteAnchor kRenderPreUIForward{
		.name = "DrawWorld::Render_PreUI -> DrawWorld::Forward",
		.function = RE::ID::DrawWorld::Render_PreUI,
		.offset = { 0x1C9, 0x3A6, 0x3A6 },
		.target = RE::ID::DrawWorld::Forward
	};
	inline constexpr REL::VariantID kSamplerStateTable{ 44312, 2704455 };
}
