#include "Render/TemporalRendererInternals.h"

namespace cs::render
{
	using namespace renderer_detail;

	void TemporalRenderer::InstallTemporalHooks()
	{
		using namespace upscaling_anchors;

		if (REX::FModule::IsRuntimeOG()) {
			RejectInitialization(
				"DrawWorld::Render_UI and its effects-path gate are unproven on the OG runtime");
			return;
		}

		const auto anchor = [](std::uint64_t a_id) {
			return REL::ID({ kUnprovenOnOG, a_id, a_id });
		};
		const auto tupleTarget = [](const std::uint64_t (&a_ids)[3]) {
			return REL::ID({ a_ids[0], a_ids[1], a_ids[2] }).address();
		};

		const auto viewportSite =
			anchor(kDrawWorldBegin).address() + kDrawWorldBeginSetDynamicViewportCall;
		const auto viewportTarget = anchor(kSetDynamicViewportAsDefault).address();
		const auto renderUiTarget = anchor(kDrawWorldRenderUI).address();
		const auto imagespaceUpscaleSite =
			renderUiTarget + kDrawWorldRenderUIResolveCall;
		const auto effectsGateCompareSite =
			renderUiTarget + kDrawWorldRenderUIEffectsGateCompare;
		const auto dynamicResolutionSite =
			anchor(kRenderPreUI).address() + kRenderPreUIUpdateDynamicResolutionCall;
		const auto dynamicResolutionTarget = anchor(kUpdateDynamicResolution).address();
		const auto samplerStateTable = anchor(kSamplerStateTable).address();
		if (!samplerBias.Initialize(samplerStateTable)) {
			RejectInitialization("The sampler-state table address did not resolve");
			return;
		}
		const auto runtimeIndex =
			static_cast<std::size_t>(REX::FModule::GetRuntimeIndex());
		const auto firstPersonAlphaSite =
			REL::ID({
				kFirstPersonAlphaAnchor[0],
				kFirstPersonAlphaAnchor[1],
				kFirstPersonAlphaAnchor[2] })
				.address() +
			kFirstPersonAlphaCall[runtimeIndex];
		const auto firstPersonAlphaTarget = kRenderAlphaGeometry[runtimeIndex]
			? REL::ID({
				  kRenderAlphaGeometry[0],
				  kRenderAlphaGeometry[1],
				  kRenderAlphaGeometry[2] })
				  .address()
			: 0;
		const auto renderEffectRangeSite =
			renderUiTarget + kDrawWorldRenderUIRenderEffectRangeCall;
		const auto deferredCompositeSite =
			anchor(kDeferredComposite).address() + kDeferredCompositeRenderPassCall;
		const auto sslrSite =
			anchor(kSSLRRaytracingSetupTechnique).address() + kSSLRRaytracingBeginTechniqueCall;
		const auto vatsSite =
			anchor(kVatsUpdateParams).address() + kVatsSetPixelConstantCall;
		const auto loadingMenuSite =
			anchor(kLoadingMenuUpdateTemporalData).address() + kLoadingMenuUpdateTemporalDataCall;
		const auto deferredPrePassSite =
			anchor(kRenderPreUI).address() + kRenderPreUIDeferredPrePassCall;
		const auto forwardSite =
			anchor(kRenderPreUI).address() + kRenderPreUIForwardCall;
		if (!IsCallSiteTargeting(viewportSite, viewportTarget)) {
			RejectInitialization("World viewport selection site did not contain the expected call");
			return;
		}
		if (!IsCallSiteTargeting(imagespaceUpscaleSite, viewportTarget)) {
			RejectInitialization("DrawWorld::Render_UI viewport handoff did not contain the expected call");
			return;
		}
		const auto dataSection =
			REX::FModule::GetExecutingModule().GetSection(".data");
		const auto expectedEffectsGate = REX::FModule::IsRuntimeAE()
			? REL::ID({ 0, 0, kDrawWorldRenderUIEffectsGateAE }).address()
			: 0;
		_renderUiPathGate = cs::engine::RenderUIPathGate::Decode(
			effectsGateCompareSite,
			dataSection.GetAddress(),
			dataSection.GetSize(),
			expectedEffectsGate);
		if (!_renderUiPathGate) {
			RejectInitialization(
				"DrawWorld::Render_UI effects-path gate instruction or target was not recognized");
			return;
		}
		if (!IsCallSiteTargeting(dynamicResolutionSite, dynamicResolutionTarget)) {
			RejectInitialization("Dynamic-resolution site did not contain the expected call");
			return;
		}
		if (!firstPersonAlphaTarget ||
			!IsCallSiteTargeting(firstPersonAlphaSite, firstPersonAlphaTarget)) {
			RejectInitialization("First-person alpha site did not contain the expected RenderAlphaGeometry call");
			return;
		}
		if (!IsCallSiteTargeting(
				renderEffectRangeSite,
				tupleTarget(kImageSpaceManagerRenderEffectRange))) {
			RejectInitialization("Imagespace effect-range site did not contain the expected RenderEffectRange call");
			return;
		}
		if (!IsCallSiteTargeting(
				deferredCompositeSite,
				tupleTarget(kBSBatchRendererRenderPassImmediately))) {
			RejectInitialization("Deferred composite site did not contain the expected RenderPassImmediately call");
			return;
		}
		if (!IsCallSiteTargeting(sslrSite, tupleTarget(kBSShaderBeginTechnique))) {
			RejectInitialization("SSLR raytracing site did not contain the expected BeginTechnique call");
			return;
		}
		if (!IsCallSiteTargeting(vatsSite, tupleTarget(kImageSpaceShaderParamSetPixelConstant))) {
			RejectInitialization("VATS parameter site did not contain the expected SetPixelConstant call");
			return;
		}
		if (!IsCallSiteTargeting(
				loadingMenuSite,
				tupleTarget(kBSGraphicsStateUpdateTemporalData))) {
			RejectInitialization("Loading-menu site did not contain the expected UpdateTemporalData call");
			return;
		}
		if (!IsCallSiteTargeting(
				deferredPrePassSite,
				tupleTarget(kDrawWorldDeferredPrePass))) {
			RejectInitialization("Render_PreUI site did not contain the expected DeferredPrePass call");
			return;
		}
		if (!IsCallSiteTargeting(forwardSite, tupleTarget(kRenderPreUIForwardTarget))) {
			RejectInitialization("Render_PreUI site did not contain the expected Forward call");
			return;
		}

		stl::write_thunk_call<DrawWorldBegin_SetDynamicViewport>(viewportSite);
		stl::write_thunk_call<Main_UpdateJitter>(
			anchor(kDrawWorldBegin).address() + kDrawWorldBeginUpdateTemporalDataCall);
		stl::write_thunk_call<DrawWorld_FirstPersonAlpha>(firstPersonAlphaSite);
		stl::write_thunk_call<DrawWorldRenderUI_Resolve>(imagespaceUpscaleSite);
		// Split the imagespace effect chain so HDR effects stay at render resolution.
		stl::write_thunk_call<DrawWorldRenderUI_RenderEffectRange>(renderEffectRangeSite);
		// Keep dynamic-resolution G-buffers valid through the deferred lighting composite.
		stl::write_thunk_call<DeferredComposite_RenderPass>(deferredCompositeSite);
		// Reconstructed screen-space reflection shader resolves scaled targets.
		stl::write_thunk_call<SSLRRaytracing_BeginTechnique>(sslrSite);
		// Scale the VATS outline thickness constant by the dynamic ratio.
		stl::write_thunk_call<Vats_SetPixelConstant>(vatsSite);
		// LoadingMenu renders full-extent, so neutralize its jitter and ratios.
		stl::write_thunk_call<LoadingMenu_UpdateTemporalData>(loadingMenuSite);
		// Bias the global sampler table around the material passes to match the render resolution.
		stl::write_thunk_call<RenderPreUI_DeferredPrePass>(deferredPrePassSite);
		stl::write_thunk_call<RenderPreUI_Forward>(forwardSite);

		stl::detour_thunk<LensFlare_RenderLensFlare>(anchor(kLensFlareRenderLensFlare));
		stl::detour_thunk<BSImageSpace_Init_FXAA>(anchor(kImageSpaceInitEffects));
		// ResetWindow can run without the render-target create callback.
		stl::detour_thunk<Renderer_ResetWindow>(anchor(kRendererResetWindow));
		stl::detour_thunk<BSShaderRenderTargets_Create>(anchor(kBSShaderRenderTargetsCreate));
		// Own both the normal and pause-only Render_UI paths.
		stl::detour_thunk<DrawWorldRenderUI>(anchor(kDrawWorldRenderUI));
		// Publish the scale and refresh proxies after the native dynamic-resolution update.
		stl::write_thunk_call<Main_UpdateDynamicResolution>(dynamicResolutionSite);
		stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(
			RE::VTABLE::ImageSpaceEffectTemporalAA[0]);
		_hooksInstalled.store(true, std::memory_order_release);
		L->info("Installed hooks");
	}

	void TemporalRenderer::InstallTemporalMenuListener()
	{
		SetTemporalEnabled(false);
		if (!MenuOpenCloseEventHandler::Register()) {
			L->warn("Loading-menu reset listener was not registered");
		}
	}

	void TemporalRenderer::BSShaderRenderTargets_Create::thunk(void* a_this)
	{
		func(a_this);

		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling resource setup", [&] {
			if (upscaling->_resourcesReady.load(std::memory_order_acquire)) {
				upscaling->InvalidateEngineDerivedResources();
			} else {
				upscaling->SetupResources();
			}
		});
	}

	bool TemporalRenderer::ImageSpaceEffectTemporalAA_IsActive::thunk(
		RE::ImageSpaceEffectTemporalAA* a_this)
	{
		auto* upscaling = GetSingleton();
		const auto method = upscaling->GetUpscaleMethod();
		if (upscaling->IsDrivingFrameState() &&
			upscaling->_srPublishedToFramebuffer.load(std::memory_order_acquire) &&
			IsExternalUpscaler(method)) {
			return false;
		}
		return func(a_this);
	}

	void TemporalRenderer::DrawWorldBegin_SetDynamicViewport::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		bool a_enabled)
	{
		auto* upscaling = GetSingleton();
		const auto method = upscaling->GetUpscaleMethod();
		const bool vendorActive =
			upscaling->IsDrivingFrameState() &&
			IsExternalUpscaler(method);
		func(a_this, vendorActive ? true : a_enabled);
	}

	void TemporalRenderer::Main_UpdateDynamicResolution::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		RE::NiPoint3* a_2,
		RE::NiPoint3* a_3,
		RE::NiPoint3* a_4,
		RE::NiPoint3* a_5)
	{
		auto* upscaling = GetSingleton();

		func(a_this, a_2, a_3, a_4, a_5);

		GuardedThunkBody("Upscaling dynamic-resolution publish", [&] {
			if (!upscaling->IsDrivingFrameState()) {
				return;
			}
			upscaling->PublishDynamicResolution();
		});
	}

	void TemporalRenderer::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
	{
		auto* upscaling = GetSingleton();
		upscaling->_spatialFallbackThisFrame.store(
			false, std::memory_order_release);
		upscaling->_renderUiRecoverySourceWidth.store(
			0, std::memory_order_relaxed);
		upscaling->_renderUiRecoverySourceHeight.store(
			0, std::memory_order_relaxed);
		upscaling->_renderUiRecoveryPassthrough.store(
			false, std::memory_order_release);
		if (upscaling->_nativeSuperResolutionFallbackPending.exchange(
				false, std::memory_order_acq_rel)) {
			render::TemporalPipeline::Get().FailSuperResolutionToNative(
				"External super resolution failed after render-state commitment; native TAA is active until restart.");
			upscaling->RestoreNativeFrameState();
		}
		upscaling->FinalizeQuarantineAtFrameBoundary();

		GuardedThunkBody("Upscaling frame-start", [&] {
			if (!upscaling->IsDrivingFrameState()) {
				upscaling->RestoreNativeFrameStateOnce();
				return;
			}
			upscaling->ConfigureTAA();
		});

		func(a_state);

		GuardedThunkBody("Upscaling ConfigureUpscaling", [&] {
			if (!upscaling->IsDrivingFrameState()) {
				return;
			}
			upscaling->ConfigureUpscaling();
		});
	}

	void TemporalRenderer::DrawWorld_FirstPersonAlpha::thunk(RE::BSShaderAccumulator* a_accumulator)
	{
		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling first-person alpha pre-stage", [upscaling] {
			upscaling->PrepareFirstPersonAlphaInputs();
		});

		func(a_accumulator);

		GuardedThunkBody("Upscaling first-person alpha post-stage", [upscaling] {
			upscaling->FinishFirstPersonAlphaInputs();
		});
	}

	void TemporalRenderer::DrawWorldRenderUI_Resolve::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		bool a_enabled)
	{
		func(a_this, a_enabled);

		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling resolve", [&] {
			if (!upscaling->_imagespaceScope) {
				return;
			}
			upscaling->_resolveSeamSeen.store(true, std::memory_order_release);
			if (upscaling->IsFrameGenerationDx12PathActive()) {
				upscaling->CaptureFrameGenerationInputs();
			}
			if (!upscaling->IsDrivingFrameState()) {
				return;
			}

			const auto upscaleMethod = upscaling->GetUpscaleMethod();
			bool upscaled = true;
			if (IsExternalUpscaler(upscaleMethod)) {
				upscaled = upscaling->PerformUpscaling();
			}
			// Freeze previews after temporal inputs and RT0 publication settle.
			upscaling->CaptureSelectedDebugSnapshot();

			// Jitter must not leak into UI regardless of whether the resolve succeeded.
			if (auto* state = cs::engine::GetGraphicsState()) {
				state->offsetX = 0.0f;
				state->offsetY = 0.0f;
			}

			if (upscaled) {
				// Save the render scale, then neutralize ratios so UI and post-processing run full-extent.
				if (auto* renderTargetManager = cs::engine::GetRenderTargetManager()) {
					upscaling->_savedDynamicWidthRatio = renderTargetManager->GetDynamicWidthRatio();
					upscaling->_savedDynamicHeightRatio = renderTargetManager->GetDynamicHeightRatio();
				}
				cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
				upscaling->_imagespaceRatiosNeutralized = true;
			}
			// A false result means both the provider and explicit spatial recovery failed.

			upscaling->CaptureHUDLessColor();

			SetTemporalEnabled(upscaleMethod == UpscaleMethod::kTAA);
		});
	}

	void TemporalRenderer::DrawWorldRenderUI_RenderEffectRange::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		std::uint32_t a_first,
		std::uint32_t a_last,
		std::uint32_t a_4,
		std::uint32_t a_5)
	{
		auto* upscaling = GetSingleton();

		float widthRatio = 1.0f;
		float heightRatio = 1.0f;
		bool doSplit = false;
		if (upscaling->IsDrivingFrameState() && upscaling->dynamicResolution.HasProxies()) {
			if (auto* renderTargetManager = cs::engine::GetRenderTargetManager()) {
				widthRatio = renderTargetManager->GetDynamicWidthRatio();
				heightRatio = renderTargetManager->GetDynamicHeightRatio();
				doSplit = widthRatio != 1.0f || heightRatio != 1.0f;
			}
		}

		if (!doSplit) {
			func(a_this, a_first, a_last, a_4, a_5);
			return;
		}

		auto* state = cs::engine::GetGraphicsState();
		const float savedOffsetX = state ? state->offsetX : 0.0f;
		const float savedOffsetY = state ? state->offsetY : 0.0f;

		try {
			// HDR effects render against the render-resolution proxies.
			func(a_this, 0, 3, 1, 1);
			upscaling->dynamicResolution.OverrideRenderTargets({ 1, 4, 29, 16 });
			upscaling->dynamicResolution.OverrideDepth(true);
			cs::engine::SetDynamicResolution(1.0f, 1.0f, false);

			// LDR effects render full-extent.
			func(a_this, 4, 13, 1, 1);
			upscaling->dynamicResolution.ResetDepth();
			upscaling->dynamicResolution.ResetRenderTargets({ 4 });

			cs::engine::SetDynamicResolution(widthRatio, heightRatio, true);
		} catch (...) {
			upscaling->dynamicResolution.ResetDepth();
			upscaling->dynamicResolution.ResetRenderTargets({ 4 });
			cs::engine::SetDynamicResolution(widthRatio, heightRatio, true);
			upscaling->QuarantineAfterException("Upscaling imagespace effect split");
		}

		if (state) {
			state->offsetX = savedOffsetX;
			state->offsetY = savedOffsetY;
		}
	}

	void TemporalRenderer::DrawWorldRenderUI::thunk(void* a_this)
	{
		auto* upscaling = GetSingleton();
		const bool fullEffectsPath =
			upscaling->_renderUiPathGate &&
			upscaling->_renderUiPathGate->TakesFullEffectsPath();
		upscaling->_renderUiFullEffectsPath.store(
			fullEffectsPath, std::memory_order_release);
		upscaling->_resolveSeamSeen.store(false, std::memory_order_release);
		upscaling->_imagespaceScope = true;
		const REX::TScopeExit clearScope{ [upscaling]() noexcept {
			upscaling->_imagespaceScope = false;
		} };
		const auto method = upscaling->GetUpscaleMethod();
		const bool resolveRequired =
			upscaling->IsDrivingFrameState() &&
			IsExternalUpscaler(method);

		func(a_this);

		GuardedThunkBody("Upscaling Render_UI completion", [&] {
			const bool resolveSeen =
				upscaling->_resolveSeamSeen.load(std::memory_order_acquire);
			if (resolveRequired && !resolveSeen) {
				upscaling->_missedResolveFrames.fetch_add(
					1, std::memory_order_relaxed);
				if (!fullEffectsPath) {
					upscaling->_gammaOnlyRecoveryFrames.fetch_add(
						1, std::memory_order_relaxed);
				}
				const bool recovered =
					upscaling->RecoverMissedResolveAtRenderUIReturn();
				const char* failure = nullptr;
				if (fullEffectsPath) {
					failure = recovered
						? "The full-effects Render_UI path unexpectedly skipped +0xC5; spatial fallback was published and native TAA begins next frame."
						: "The full-effects Render_UI path unexpectedly skipped +0xC5 and spatial fallback failed; native TAA begins next frame.";
				} else {
					failure = recovered
						? "The Gamma-only Render_UI path bypassed +0xC5; spatial fallback was published and native TAA begins next frame."
						: "The Gamma-only Render_UI path bypassed +0xC5 and spatial fallback failed; native TAA begins next frame.";
				}
				render::TemporalPipeline::Get().PostFailure(
					render::temporal::FailureDomain::kEngine,
					failure);
				if (recovered) {
					L->warn(
						"spatial fallback published at Render_UI return after the {} path bypassed +0xC5.",
						fullEffectsPath ? "full-effects" : "Gamma-only");
				}
			} else if (!fullEffectsPath && resolveSeen) {
				render::TemporalPipeline::Get().PostFailure(
					render::temporal::FailureDomain::kEngine,
					"Render_UI effects-path gate selected Gamma-only processing but the +0xC5 seam executed.");
			}
		});

		GuardedThunkBody("Upscaling Render_UI temporal cleanup", [] {
			SetTemporalEnabled(false);
		});

		GuardedThunkBody("Upscaling Render_UI ratio restore", [&] {
			if (!upscaling->_imagespaceRatiosNeutralized) {
				return;
			}
			upscaling->_imagespaceRatiosNeutralized = false;
			const bool activated = upscaling->_savedDynamicWidthRatio != 1.0f ||
				upscaling->_savedDynamicHeightRatio != 1.0f;
			cs::engine::SetDynamicResolution(
				upscaling->_savedDynamicWidthRatio,
				upscaling->_savedDynamicHeightRatio,
				activated);
		});
	}

	void TemporalRenderer::DeferredComposite_RenderPass::thunk(void* a_pass, std::uint32_t a_2, bool a_3)
	{
		auto* upscaling = GetSingleton();

		float widthRatio = 1.0f;
		float heightRatio = 1.0f;
		bool overrideActive = false;
		if (upscaling->IsDrivingFrameState() && upscaling->dynamicResolution.HasProxies()) {
			if (auto* renderTargetManager = cs::engine::GetRenderTargetManager()) {
				widthRatio = renderTargetManager->GetDynamicWidthRatio();
				heightRatio = renderTargetManager->GetDynamicHeightRatio();
				overrideActive = widthRatio != 1.0f || heightRatio != 1.0f;
			}
		}

		if (overrideActive) {
			GuardedThunkBody("Upscaling composite override", [&] {
				upscaling->dynamicResolution.OverrideRenderTargets(
					{ 20, 25, 57, 24, 23, 58, 59, 3, 9, 60, 61, 28 });
				upscaling->dynamicResolution.OverrideDepth(true);
				cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
			});
		}

		func(a_pass, a_2, a_3);

		if (overrideActive) {
			GuardedThunkBody("Upscaling composite reset", [&] {
				upscaling->dynamicResolution.ResetRenderTargets({ 4 });
				upscaling->dynamicResolution.ResetDepth();
				if (upscaling->dynamicResolution.HasProxies()) {
					cs::engine::SetDynamicResolution(widthRatio, heightRatio, true);
				}
			});
		}
	}

	void TemporalRenderer::LensFlare_RenderLensFlare::thunk(RE::NiCamera* a_camera)
	{
		auto* upscaling = GetSingleton();

		bool overrideActive = false;
		if (upscaling->IsDrivingFrameState() && upscaling->dynamicResolution.HasProxies()) {
			if (auto* renderTargetManager = cs::engine::GetRenderTargetManager()) {
				overrideActive = renderTargetManager->GetDynamicWidthRatio() != 1.0f ||
					renderTargetManager->GetDynamicHeightRatio() != 1.0f;
			}
		}

		if (overrideActive) {
			GuardedThunkBody("Upscaling lens-flare depth override", [&] {
				upscaling->dynamicResolution.OverrideDepth(true);
			});
		}

		func(a_camera);

		if (overrideActive) {
			GuardedThunkBody("Upscaling lens-flare depth reset", [&] {
				upscaling->dynamicResolution.ResetDepth();
			});
		}
	}

	void TemporalRenderer::SSLRRaytracing_BeginTechnique::thunk(
		void* a_shader,
		std::uint32_t a_2,
		std::uint32_t a_3,
		std::uint32_t a_4,
		std::uint32_t a_5)
	{
		func(a_shader, a_2, a_3, a_4, a_5);

		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling SSLR shader patch", [&] {
			if (upscaling->IsDrivingFrameState() && upscaling->IsUpscalingActive()) {
				upscaling->PatchSSRShader();
			}
		});
	}

	void TemporalRenderer::Vats_SetPixelConstant::thunk(
		void* a_param,
		int a_row,
		float a_x,
		float a_y,
		float a_z,
		float a_w)
	{
		auto* upscaling = GetSingleton();
		if (upscaling->IsDrivingFrameState() && upscaling->IsUpscalingActive()) {
			func(
				a_param,
				a_row,
				a_x * upscaling->_savedDynamicHeightRatio,
				a_y * upscaling->_savedDynamicWidthRatio,
				a_z,
				a_w);
			return;
		}
		func(a_param, a_row, a_x, a_y, a_z, a_w);
	}

	void TemporalRenderer::LoadingMenu_UpdateTemporalData::thunk(RE::BSGraphics::State* a_state)
	{
		func(a_state);

		GuardedThunkBody("Upscaling loading-menu reset", [] {
			cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
		});
	}

	void TemporalRenderer::RenderPreUI_DeferredPrePass::thunk(void* a_this)
	{
		auto* upscaling = GetSingleton();
		if (!upscaling->IsDrivingFrameState()) {
			func(a_this);
			return;
		}
		try {
			upscaling->samplerBias.Override();
			func(a_this);
			upscaling->samplerBias.Reset();
		} catch (...) {
			upscaling->samplerBias.Reset();
			upscaling->QuarantineAfterException("Upscaling sampler override (deferred prepass)");
		}
	}

	void TemporalRenderer::RenderPreUI_Forward::thunk(void* a_this)
	{
		auto* upscaling = GetSingleton();
		if (!upscaling->IsDrivingFrameState()) {
			func(a_this);
			return;
		}
		try {
			upscaling->samplerBias.Override();
			func(a_this);
			upscaling->samplerBias.Reset();
		} catch (...) {
			upscaling->samplerBias.Reset();
			upscaling->QuarantineAfterException("Upscaling sampler override (forward)");
		}
	}

	void TemporalRenderer::BSImageSpace_Init_FXAA::thunk(RE::ImageSpaceManager* a_this)
	{
		func(a_this);

		GuardedThunkBody("Upscaling FXAA disable", [] {
			auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
			if (!imageSpaceManager) {
				return;
			}
			constexpr auto fxaa = static_cast<std::uint32_t>(
				RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_SHADER_FXAA);
			if (fxaa < imageSpaceManager->effectList.size()) {
				if (auto* effect = imageSpaceManager->effectList[fxaa]) {
					effect->isActive = false;
				}
			}
		});
	}

	void TemporalRenderer::Renderer_ResetWindow::thunk(RE::BSGraphics::Renderer* a_this, std::uint32_t a_arg)
	{
		func(a_this, a_arg);

		GuardedThunkBody("Upscaling reset-window invalidation", [] {
			GetSingleton()->InvalidateEngineDerivedResources();
		});
	}

	RE::BSEventNotifyControl TemporalRenderer::MenuOpenCloseEventHandler::ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (a_event.menuName == RE::LoadingMenu::MENU_NAME && !a_event.opening) {
			render::TemporalPipeline::Get().RequestSuperResolutionReset();
			render::TemporalPipeline::Get().RequestFrameGenerationReset();
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	bool TemporalRenderer::MenuOpenCloseEventHandler::Register()
	{
		static MenuOpenCloseEventHandler handler;
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		ui->RegisterSink<RE::MenuOpenCloseEvent>(&handler);
		return true;
	}
}
