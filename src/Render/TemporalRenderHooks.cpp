#include "RE/D/DrawWorld.h"
#include "Render/SharedData.h"
#include "Render/TemporalRendererInternals.h"

namespace cs::render
{
	using namespace renderer_detail;

	void TemporalRenderer::InstallTemporalHooks()
	{
		using namespace upscaling_anchors;

		struct CallHook
		{
			const cs::engine::CallSiteAnchor* anchor;
			void (*install)(std::uintptr_t);
			std::uintptr_t site = 0;
		};
		const auto firstPersonAlpha = FirstPersonAlphaRenderAlphaGeometry();
		std::array callHooks{
			CallHook{ &kDrawWorldBeginSetDynamicViewport, &stl::write_thunk_call<DrawWorldBegin_SetDynamicViewport> },
			CallHook{ &kDrawWorldBeginUpdateTemporalData, &stl::write_thunk_call<Main_UpdateJitter> },
			CallHook{ &firstPersonAlpha, &stl::write_thunk_call<DrawWorld_FirstPersonAlpha> },
			CallHook{ &kDrawWorldRenderUIResolve, &stl::write_thunk_call<DrawWorldRenderUI_Resolve> },
			CallHook{ &kDrawWorldRenderUIRenderEffectRange, &stl::write_thunk_call<DrawWorldRenderUI_RenderEffectRange> },
			CallHook{ &kDeferredCompositeRenderPass, &stl::write_thunk_call<DeferredComposite_RenderPass> },
			CallHook{ &kDeferredCompositeSSLRBegin, &stl::write_thunk_call<DeferredCompositeSSLR_Begin> },
			CallHook{ &kDeferredCompositeSSLRBlurHAcquire, &stl::write_thunk_call<DeferredCompositeSSLR_AcquireBlurH> },
			CallHook{ &kDeferredCompositeSSLRBlurHRelease, &stl::write_thunk_call<DeferredCompositeSSLR_ReleaseBlurH> },
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

		for (const auto& hook : callHooks) {
			hook.install(hook.site);
		}
		// Lens-flare visibility read samples the main depth buffer.
		stl::detour_thunk<LensFlare_RenderLensFlare>(RE::ID::BSImagespaceShaderLensFlare::RenderLensFlare);
		stl::detour_thunk<BSImageSpace_Init_FXAA>(RE::ID::ImageSpaceManager::InitEffects);
		// The create callback runs before WindowSizeChanged repopulates RT0.
		stl::detour_thunk<Renderer_WindowSizeChanged>(RE::ID::BSGraphics::Renderer::WindowSizeChanged);
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

	void TemporalRenderer::InitializeTemporalState()
	{
		SetTemporalEnabled(false);
	}

	void TemporalRenderer::BSShaderRenderTargets_Create::thunk(void* a_this)
	{
		func(a_this);

		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling resource setup", [&] {
			if (upscaling->_windowSizeChanging) {
				upscaling->_windowRebuildDeferred = true;
				L->info(
					"Render-target create deferred until WindowSizeChanged returns (RT0 view {})",
					cs::engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer) ? "present" : "null");
				return;
			}
			upscaling->RefreshEngineResources();
		});
	}

	void TemporalRenderer::RefreshEngineResources()
	{
		if (_resourcesReady.load(std::memory_order_acquire)) {
			InvalidateEngineDerivedResources();
		} else {
			SetupResources();
		}
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

		GuardedThunkBody("Upscaling pre-UI seam", [] {
			GetSingleton()->FinishPreUiSeam();
		});
	}

	void TemporalRenderer::DrawWorldRenderUI_RenderEffectRange::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		std::uint32_t a_first,
		std::uint32_t a_last,
		std::uint32_t a_4,
		std::uint32_t a_5)
	{
		GuardedThunkBody("Upscaling pre-tonemap seam", [] {
			GetSingleton()->ResolveSceneBeforeImagespace();
		});
		func(a_this, a_first, a_last, a_4, a_5);
		GetSingleton()->_vatsMaskSwap.Restore();
	}

	void TemporalRenderer::DrawWorldRenderUI::thunk(void* a_this)
	{
		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling scene history reset", [] {
			if (IsHistoryResetFrame()) {
				TemporalPipeline::Get().RequestSuperResolutionReset();
				TemporalPipeline::Get().RequestFrameGenerationReset();
			}
		});
		const bool fullEffectsPath = RE::DrawWorld::GetEndOfFrameImagespaceEffectsEnabled();
		upscaling->_renderUiFullEffectsPath.store(
			fullEffectsPath, std::memory_order_release);
		upscaling->_resolveSeamSeen.store(false, std::memory_order_release);
		upscaling->_imagespaceScope = true;
		const REX::TScopeExit clearScope{ [upscaling]() noexcept {
			upscaling->_imagespaceScope = false;
			upscaling->_chainTriangle.Restore();
			upscaling->_vatsMaskSwap.Restore();
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
			if (resolveRequired && fullEffectsPath && !resolveSeen) {
				upscaling->_missedResolveFrames.fetch_add(
					1, std::memory_order_relaxed);
				upscaling->ScheduleNativeSuperResolutionFallback();
				render::TemporalPipeline::Get().PostFailure(
					render::temporal::FailureDomain::kEngine,
					"The full-effects Render_UI path skipped the pre-tonemap seam; temporal rendering is disabled until restart.");
			} else if (!fullEffectsPath && resolveSeen) {
				render::TemporalPipeline::Get().PostFailure(
					render::temporal::FailureDomain::kEngine,
					"Render_UI effects-path gate selected Gamma-only processing but the pre-tonemap seam executed.");
			}
		});

		GuardedThunkBody("Upscaling Render_UI temporal cleanup", [] {
			SetTemporalEnabled(false);
		});

		// Main_UpdateDynamicResolution republishes the scale on the next world frame.
		GuardedThunkBody("Upscaling Render_UI ratio release", [&] {
			if (!upscaling->IsDrivingFrameState()) {
				return;
			}
			if (auto* state = cs::engine::GetGraphicsState()) {
				state->offsetX = 0.0f;
				state->offsetY = 0.0f;
			}
			cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
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

		if (upscaling->IsDrivingFrameState()) {
			GuardedThunkBody("Upscaling composite override", [&] {
				using RE::BSGraphics::RenderTargetID;
				// kSSRBlurred1 is not copied: the SSLR proxy pass already left it in the proxy.
				upscaling->dynamicResolution.BeginProxyPass({ RenderTargetID::kGBufferNormal,
					RenderTargetID::kAmbientOcclusion,
					RenderTargetID::kGBufferEnvironment,
					RenderTargetID::kGBufferAmbient,
					RenderTargetID::kGBufferEmissive,
					RenderTargetID::kDiffuseLight,
					RenderTargetID::kDiffuseSpecular,
					RenderTargetID::kMainCopy,
					RenderTargetID::kDiffuseLightTiled,
					RenderTargetID::kDiffuseSpecularTiled,
					RenderTargetID::kAmbientOcclusionHalf });
			});
		}

		func(a_pass, a_2, a_3);

		GuardedThunkBody("Upscaling composite reset", [&] {
			upscaling->dynamicResolution.EndProxyPass({ RE::BSGraphics::RenderTargetID::kMain });
		});
	}

	void TemporalRenderer::DeferredCompositeSSLR_Begin::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		std::int32_t a_slot,
		RE::BSGraphics::RenderTargetID a_logicalID,
		RE::BSGraphics::SetRenderTargetMode a_mode)
	{
		auto* upscaling = GetSingleton();
		if (upscaling->IsDrivingFrameState()) {
			GuardedThunkBody("Upscaling SSLR override", [&] {
				using RE::BSGraphics::RenderTargetID;
				// Prepass reads normals and metadata; the ray pass reads the first composite.
				upscaling->dynamicResolution.BeginProxyPass({ RenderTargetID::kGBufferNormal,
																RenderTargetID::kGBufferEnvironment,
																RenderTargetID::kMainCopy },
					cs::features::DynamicResolution::ProxyTriangle::kUnit);
			});
		}
		func(a_this, a_slot, a_logicalID, a_mode);
	}

	void TemporalRenderer::DeferredCompositeSSLR_AcquireBlurH::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		RE::BSGraphics::RenderTargetID a_logicalID)
	{
		func(a_this, a_logicalID);
		GuardedThunkBody("Upscaling SSLR BlurH override", [&] {
			GetSingleton()->dynamicResolution.ProxyAcquiredRenderTarget(RE::BSGraphics::RenderTargetID::kSSRBlurred0);
		});
	}

	void TemporalRenderer::DeferredCompositeSSLR_ReleaseBlurH::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		RE::BSGraphics::RenderTargetID a_logicalID)
	{
		// BlurH must be restored while its platform slot is still mapped.
		GuardedThunkBody("Upscaling SSLR reset", [&] {
			using RE::BSGraphics::RenderTargetID;
			GetSingleton()->dynamicResolution.ReleaseAcquiredRenderTarget(RenderTargetID::kSSRBlurred0);
			// Water samples the ray result and blur in the engine's region layout.
			GetSingleton()->dynamicResolution.EndProxyPass({ RenderTargetID::kSSR, RenderTargetID::kSSRBlurred1 });
		});
		func(a_this, a_logicalID);
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
			const ScopedMainViewMipBias mipBias(upscaling->GetMipBias());
			func(a_this);
		} catch (...) {
			upscaling->QuarantineAfterException("Upscaling mip bias scope (deferred prepass)");
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
			const ScopedMainViewMipBias mipBias(upscaling->GetMipBias());
			func(a_this);
		} catch (...) {
			upscaling->QuarantineAfterException("Upscaling mip bias scope (forward)");
		}
	}

	void TemporalRenderer::BSImageSpace_Init_FXAA::thunk(RE::ImageSpaceManager* a_this)
	{
		func(a_this);

		GuardedThunkBody("Upscaling FXAA disable", [] {
			if (auto* effect = cs::engine::GetImageSpaceEffect(
					RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_SHADER_FXAA)) {
				effect->isActive = false;
			}
		});
	}

	void TemporalRenderer::Renderer_WindowSizeChanged::thunk(RE::BSGraphics::Renderer* a_this, std::uint32_t a_arg)
	{
		auto* upscaling = GetSingleton();
		upscaling->_windowSizeChanging = true;
		func(a_this, a_arg);
		upscaling->_windowSizeChanging = false;

		GuardedThunkBody("Upscaling window-size rebuild", [&] {
			if (std::exchange(upscaling->_windowRebuildDeferred, false)) {
				upscaling->RefreshEngineResources();
				upscaling->_windowRebuildHandled = true;
			}
		});
	}

	void TemporalRenderer::Renderer_ResetWindow::thunk(RE::BSGraphics::Renderer* a_this, std::uint32_t a_arg)
	{
		func(a_this, a_arg);

		GuardedThunkBody("Upscaling reset-window invalidation", [] {
			auto* upscaling = GetSingleton();
			// ResetWindow follows WindowSizeChanged, which already rebuilt.
			if (!std::exchange(upscaling->_windowRebuildHandled, false)) {
				upscaling->RefreshEngineResources();
			}
		});
	}

}
