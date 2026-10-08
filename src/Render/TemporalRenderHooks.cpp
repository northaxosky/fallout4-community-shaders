#include "Render/TemporalRendererInternals.h"

namespace cs::render
{
	using namespace renderer_detail;

	void TemporalRenderer::InstallTemporalHooks()
	{
		using namespace upscaling_anchors;

		const auto samplerStateTable = kSamplerStateTable.address();
		if (!samplerBias.Initialize(samplerStateTable)) {
			RejectInitialization("The sampler-state table address did not resolve");
			return;
		}

		struct CallHook
		{
			const cs::engine::CallSiteAnchor* anchor;
			void (*install)(std::uintptr_t);
			std::uintptr_t site = 0;
		};
		std::array callHooks{
			CallHook{ &kDrawWorldBeginSetDynamicViewport, &stl::write_thunk_call<DrawWorldBegin_SetDynamicViewport> },
			CallHook{ &kDrawWorldBeginUpdateTemporalData, &stl::write_thunk_call<Main_UpdateJitter> },
			CallHook{ &kFirstPersonAlphaRenderAlphaGeometry, &stl::write_thunk_call<DrawWorld_FirstPersonAlpha> },
			CallHook{ &kDrawWorldRenderUIResolve, &stl::write_thunk_call<DrawWorldRenderUI_Resolve> },
			CallHook{ &kDrawWorldRenderUIRenderEffectRange, &stl::write_thunk_call<DrawWorldRenderUI_RenderEffectRange> },
			CallHook{ &kDeferredCompositeRenderPass, &stl::write_thunk_call<DeferredComposite_RenderPass> },
			CallHook{ &kVatsSetPixelConstant, &stl::write_thunk_call<Vats_SetPixelConstant> },
			CallHook{ &kLoadingMenuUpdateTemporalData, &stl::write_thunk_call<LoadingMenu_UpdateTemporalData> },
			CallHook{ &kRenderPreUIDeferredPrePass, &stl::write_thunk_call<RenderPreUI_DeferredPrePass> },
			CallHook{ &kRenderPreUIForward, &stl::write_thunk_call<RenderPreUI_Forward> },
			CallHook{ &kRenderPreUIUpdateDynamicResolution, &stl::write_thunk_call<Main_UpdateDynamicResolution> }
		};
		for (auto& hook : callHooks) {
			const auto site = cs::engine::ResolveCallSite(*hook.anchor);
			if (!site) {
				RejectInitialization(site.error());
				return;
			}
			hook.site = *site;
		}

		const auto dataSection =
			REX::FModule::GetExecutingModule().GetSection(".data");
		_renderUiPathGate = cs::engine::RenderUIPathGate::Decode(
			cs::engine::RuntimeSite(RE::ID::DrawWorld::Imagespace, kDrawWorldRenderUIEffectsGateCompare),
			dataSection.GetAddress(),
			dataSection.GetSize(),
			kDrawWorldRenderUIEffectsGate.address());
		if (!_renderUiPathGate) {
			RejectInitialization(
				"DrawWorld::Render_UI effects-path gate instruction or target was not recognized");
			return;
		}

		for (const auto& hook : callHooks) {
			hook.install(hook.site);
		}
		// Lens-flare visibility read samples the main depth buffer.
		stl::detour_thunk<LensFlare_RenderLensFlare>(RE::ID::BSImagespaceShaderLensFlare::RenderLensFlare);
		stl::detour_thunk<BSImageSpace_Init_FXAA>(RE::ID::ImageSpaceManager::InitEffects);
		// ResetWindow can run without the render-target create callback.
		stl::detour_thunk<Renderer_ResetWindow>(RE::ID::BSGraphics::Renderer::ResetWindow);
		// Resource setup follows creation of the engine render targets.
		stl::detour_thunk<BSShaderRenderTargets_Create>(RE::ID::BSShaderRenderTargets::Create);
		// Own both the normal and pause-only Render_UI paths.
		stl::detour_thunk<DrawWorldRenderUI>(RE::ID::DrawWorld::Imagespace);
		// Pip-Boy and companion map render inside Render_UI before the resolve.
		stl::detour_thunk<Interface3D_RenderPrepassesAndMenus>(RE::ID::Interface3D::RenderPrepassesAndMenus);
		stl::detour_thunk<CompanionLocalMap_SetRenderFunc>(RE::ID::DrawWorld::SetCompanionLocalMapRenderFunc);
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
		if (upscaling->IsDrivingVendorUpscaler() &&
			upscaling->_srPublishedToFramebuffer.load(std::memory_order_acquire)) {
			return false;
		}
		return func(a_this);
	}

	void TemporalRenderer::DrawWorldBegin_SetDynamicViewport::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		bool a_enabled)
	{
		func(a_this, GetSingleton()->IsDrivingVendorUpscaler() ? true : a_enabled);
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
			const auto handoff = temporal::PlanPreUiHandoff(
				upscaling->_renderUiFullEffectsPath.load(
					std::memory_order_acquire),
				upscaling->ShouldUseFrameGenerationThisFrame(),
				upscaling->IsDrivingFrameState());
			if (handoff.captureFrameGenerationInputs) {
				upscaling->CaptureFrameGenerationInputs();
			}
			if (!handoff.driveSuperResolution) {
				if (handoff.ShouldCaptureHudlessColor(true)) {
					upscaling->CaptureHUDLessColor();
				}
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

			if (handoff.ShouldCaptureHudlessColor(upscaled)) {
				upscaling->CaptureHUDLessColor();
			}

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
			upscaling->dynamicResolution.OverrideRenderTargets({ cs::engine::RenderTarget::kRefractionNormal,
				cs::engine::RenderTarget::kMainTemp,
				cs::engine::RenderTarget::kMotionVectors,
				cs::engine::RenderTarget::kHdrImagespaceAux });
			upscaling->dynamicResolution.OverrideDepth(true);
			cs::engine::SetDynamicResolution(1.0f, 1.0f, false);

			// LDR effects render full-extent.
			func(a_this, 4, 13, 1, 1);
			upscaling->dynamicResolution.ResetDepth();
			upscaling->dynamicResolution.ResetRenderTargets({ cs::engine::RenderTarget::kMainTemp });

			cs::engine::SetDynamicResolution(widthRatio, heightRatio, true);
		} catch (...) {
			upscaling->dynamicResolution.ResetDepth();
			upscaling->dynamicResolution.ResetRenderTargets({ cs::engine::RenderTarget::kMainTemp });
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
		const bool resolveRequired = upscaling->IsDrivingVendorUpscaler();

		func(a_this);

		GuardedThunkBody("Upscaling Render_UI completion", [&] {
			if (!cs::engine::GetCapturedWorldCameraRecord(GetEngineFrame())) {
				render::TemporalPipeline::Get().SkipWorldFrame();
				return;
			}
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
					failure = recovered ? "The full-effects Render_UI path unexpectedly skipped +0xC5; spatial fallback was published and temporal rendering is disabled until restart." : "The full-effects Render_UI path unexpectedly skipped +0xC5 and spatial fallback failed; temporal rendering is disabled until restart.";
				} else {
					failure = recovered ? "The Gamma-only Render_UI path bypassed +0xC5; spatial fallback was published and temporal rendering is disabled until restart." : "The Gamma-only Render_UI path bypassed +0xC5 and spatial fallback failed; temporal rendering is disabled until restart.";
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

	void TemporalRenderer::Interface3D_RenderPrepassesAndMenus::thunk(RE::Interface3D::Renderer* a_this)
	{
		cs::engine::UnscaledRenderScope scope{
			GetSingleton()->IsDrivingVendorUpscaler() && cs::engine::RendersToPipboyTarget(*a_this)
		};
		func(a_this);
	}

	template <class Tag, class... Args>
	void TemporalRenderer::UnscaledRenderCallback<Tag, Args...>::Invoke(Args... a_args)
	{
		cs::engine::UnscaledRenderScope scope{ GetSingleton()->IsDrivingVendorUpscaler() };
		original.load(std::memory_order_acquire)(a_args...);
	}

	template <class Tag, class... Args>
	void TemporalRenderer::UnscaledRenderCallback<Tag, Args...>::thunk(Callback a_callback)
	{
		original.store(a_callback, std::memory_order_release);
		func(a_callback ? &Invoke : nullptr);
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
				using cs::engine::RenderTarget;
				upscaling->dynamicResolution.OverrideRenderTargets({ RenderTarget::kGbufferNormal,
					RenderTarget::kAmbientOcclusion,
					RenderTarget::kGbufferMetadata,
					RenderTarget::kGbufferMaterial,
					RenderTarget::kGbufferEmissive,
					RenderTarget::kDiffuseBufferA,
					RenderTarget::kSpecularBufferA,
					RenderTarget::kMain,
					RenderTarget::kSSLRBlurV,
					RenderTarget::kDiffuseBufferB,
					RenderTarget::kSpecularBufferB,
					RenderTarget::kAmbientOcclusionHalf });
				upscaling->dynamicResolution.OverrideDepth(true);
				cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
			});
		}

		func(a_pass, a_2, a_3);

		if (overrideActive) {
			GuardedThunkBody("Upscaling composite reset", [&] {
				upscaling->dynamicResolution.ResetRenderTargets({ cs::engine::RenderTarget::kMainTemp });
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
