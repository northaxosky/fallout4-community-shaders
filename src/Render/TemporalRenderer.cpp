#include "Render/TemporalRendererInternals.h"

namespace cs::render
{
	using namespace renderer_detail;

	TemporalRenderer::TemporalRenderer() = default;

	TemporalRenderer* TemporalRenderer::GetSingleton()
	{
		return &TemporalPipeline::Get().Renderer();
	}

	TemporalRenderer::UpscaleMethod TemporalRenderer::GetUpscaleMethod() const
	{
		const auto effective =
			render::TemporalPipeline::Get().GetEffectiveConfiguration();
		if (!effective.superResolutionEnabled) {
			return UpscaleMethod::kNONE;
		}
		switch (effective.superResolution) {
		case render::temporal::SuperResolutionMethod::kNone:
			return UpscaleMethod::kNONE;
		case render::temporal::SuperResolutionMethod::kTAA:
			return UpscaleMethod::kTAA;
		case render::temporal::SuperResolutionMethod::kFSR3:
			return UpscaleMethod::kFSR;
		case render::temporal::SuperResolutionMethod::kDLSS:
			return UpscaleMethod::kDLSS;
		case render::temporal::SuperResolutionMethod::kCount:
			break;
		}
		return UpscaleMethod::kNONE;
	}

	bool TemporalRenderer::IsUpscalingActive() const
	{
		auto method = GetUpscaleMethod();

		if (!IsExternalUpscaler(method)) {
			return false;
		}

		const auto extent = _renderSize.Committed();
		const auto* state = cs::engine::GetGraphicsState();
		return state &&
			(extent.width < state->screenWidth ||
				extent.height < state->screenHeight);
	}

	bool TemporalRenderer::IsFrameGenerationDx12PathActive() const noexcept
	{
		return render::TemporalPipeline::Get().IsFrameGenerationProxyActive();
	}

	bool TemporalRenderer::IsFrameGenerationActive() const noexcept
	{
		const auto status = render::TemporalPipeline::Get().GetStatus();
		return IsFrameGenerationDx12PathActive() &&
			status.effective.frameGenerationEnabled;
	}

	bool TemporalRenderer::ShouldUseFrameGenerationThisFrame() const noexcept
	{
		if (!IsFrameGenerationDx12PathActive() ||
			!render::TemporalPipeline::Get()
				 .GetFrameGenerationCaptureResources()
				 .ready) {
			return false;
		}

		auto* main = RE::Main::GetSingleton();
		auto* ui = RE::UI::GetSingleton();
		const bool excludedMenu =
			!main || !ui || main->inMenuMode ||
			ui->GetMenuOpen<RE::MainMenu>() ||
			ui->GetMenuOpen<RE::PauseMenu>() ||
			ui->GetMenuOpen<RE::LoadingMenu>() ||
			ui->GetMenuOpen<RE::PipboyMenu>();
		return render::TemporalPipeline::Get().IsFrameGenerationEnabledForFrame(
			excludedMenu);
	}

	std::pair<std::uint32_t, std::uint32_t> TemporalRenderer::GetRenderSize() const noexcept
	{
		const auto* state = cs::engine::GetGraphicsState();
		if (!state) {
			return { 0, 0 };
		}
		const auto method = GetUpscaleMethod();
		if (!IsExternalUpscaler(method)) {
			const auto extent = GetActiveExtent(state->screenWidth, state->screenHeight);
			return { extent.width, extent.height };
		}
		const auto extent = _renderSize.Requested();
		return { extent.width, extent.height };
	}

	float TemporalRenderer::GetMipBias() const
	{
		return _mipBias.load(std::memory_order_relaxed);
	}

	void TemporalRenderer::ConfigureTAA()
	{
		auto upscaleMethod = GetUpscaleMethod();

		SetTemporalEnabled(upscaleMethod != UpscaleMethod::kNONE);
	}

	temporal::ProviderResult TemporalRenderer::PrepareRenderSize(
		RE::BSGraphics::State* a_state,
		UpscaleMethod a_method)
	{
		if (!a_state || !a_state->screenWidth || !a_state->screenHeight) {
			_renderSize.SetNative(0, 0);
			resolutionScale = { 1.0f, 1.0f };
			return {
				.code = temporal::ProviderResultCode::kFailure,
				.message =
					"Super-resolution sizing requires a valid output extent."
			};
		}

		_renderSize.SetNative(a_state->screenWidth, a_state->screenHeight);
		resolutionScale = { 1.0f, 1.0f };
		if (!IsExternalUpscaler(a_method)) {
			return { .code = temporal::ProviderResultCode::kSuccess };
		}

		const temporal::SuperResolutionSizeRequest request{
			.outputWidth = a_state->screenWidth,
			.outputHeight = a_state->screenHeight,
			.qualityMode = settings.qualityMode
		};
		const auto result =
			render::TemporalPipeline::Get().QuerySuperResolutionRenderSize(
				ToCoreMethod(a_method), request);
		if (!_renderSize.SetRequested(request, result)) {
			if (!result.result.Succeeded()) {
				return result.result;
			}
			return {
				.code = temporal::ProviderResultCode::kFailure,
				.sdkResult = result.result.sdkResult,
				.message =
					"The super-resolution provider returned an invalid render extent."
			};
		}
		return result.result;
	}

	void TemporalRenderer::ConfigureUpscaling()
	{
		auto* state = cs::engine::GetGraphicsState();
		if (!state) {
			return;
		}

		auto upscaleMethod = GetUpscaleMethod();
		const auto sizeResult = PrepareRenderSize(state, upscaleMethod);
		if (!sizeResult.Succeeded()) {
			_resourcesReady.store(false, std::memory_order_release);
			_spatialFallbackPreflightReady.store(
				false, std::memory_order_release);
			render::TemporalPipeline::Get().PostFailure(
				render::temporal::FailureDomain::kSuperResolution,
				std::format(
					"Super-resolution render-size query failed: {} (SDK {})",
					sizeResult.message,
					sizeResult.sdkResult));
			RestoreNativeFrameState();
			return;
		}
		const auto [renderWidth, renderHeight] = GetRenderSize();

		// Fail closed before publishing any jitter, ratios, or mip bias.
		const bool resourcesReady = CheckResources(upscaleMethod);
		const bool recoveryReady =
			resourcesReady && PreflightExternalResolve(upscaleMethod);
		_spatialFallbackPreflightReady.store(
			recoveryReady, std::memory_order_release);
		if (!recoveryReady) {
			_resourcesReady.store(false, std::memory_order_release);
			render::TemporalPipeline::Get().PostFailure(
				render::temporal::FailureDomain::kEngine,
				"External super resolution recovery resources failed preflight before reduced-resolution commitment.");
			RestoreNativeFrameState();
			return;
		}
		_renderSize.CommitRequested();
		resolutionScale = {
			_renderSize.WidthRatio(),
			_renderSize.HeightRatio()
		};

		float2 screenSize{ (float)state->screenWidth, (float)state->screenHeight };
		const bool upscalerActive = IsExternalUpscaler(upscaleMethod);

		if (upscalerActive) {
			auto phaseCount = GetJitterPhaseCount(
				static_cast<std::int32_t>(renderWidth),
				static_cast<std::int32_t>(state->screenWidth));

			GetJitterOffset(
				jitter.x,
				jitter.y,
				static_cast<std::int32_t>(state->frameCount),
				phaseCount);
		} else {
			jitter.x = -state->offsetX * screenSize.x / 2.0f;
			jitter.y = state->offsetY * screenSize.y / 2.0f;
		}

		// Ratios, jitter, and proxies are published after the native dynamic-resolution update.
		const float mipBias = upscalerActive
			? CalculateMipBias(
				  static_cast<float>(renderWidth),
				  screenSize.x,
				  upscaleMethod == UpscaleMethod::kDLSS)
			: 0.0f;
		_mipBias.store(mipBias, std::memory_order_relaxed);
		if (!samplerBias.Update(mipBias)) {
			// Retry until the engine has populated every sampler slot.
			return;
		}
	}

	void TemporalRenderer::PublishDynamicResolution()
	{
		auto* state = cs::engine::GetGraphicsState();
		if (!state) {
			return;
		}

		const auto method = GetUpscaleMethod();
		// Jitter is computed for any vendor method (including Native AA at scale 1.0); proxies only below native.
		const bool vendorMethod = IsExternalUpscaler(method);
		const bool scaleActive = vendorMethod &&
			(resolutionScale.x < 0.99f || resolutionScale.y < 0.99f);
		const float widthRatio =
			scaleActive ? resolutionScale.x : 1.0f;
		const float heightRatio =
			scaleActive ? resolutionScale.y : 1.0f;

		if (vendorMethod) {
			const auto [renderWidth, renderHeight] = GetRenderSize();
			if (renderWidth > 0 && renderHeight > 0) {
				state->offsetX = -2.0f * jitter.x / static_cast<float>(renderWidth);
				state->offsetY = 2.0f * jitter.y / static_cast<float>(renderHeight);
			}
		}

		// Build render-resolution proxies before the world and HDR imagespace chain use them.
		dynamicResolution.UpdateRenderTargets(widthRatio, heightRatio);

		_savedDynamicWidthRatio = widthRatio;
		_savedDynamicHeightRatio = heightRatio;
		cs::engine::SetDynamicResolution(
			widthRatio,
			heightRatio,
			widthRatio != 1.0f || heightRatio != 1.0f);
		_resolutionScalePublished = true;
	}

	void TemporalRenderer::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!a_device) {
			RejectInitialization("Upscaling requires a D3D11 device");
			return;
		}
	}

	bool TemporalRenderer::IsDrivingFrameState() const noexcept
	{
		return _hooksInstalled.load(std::memory_order_acquire)
			&& _resourcesReady.load(std::memory_order_acquire)
			&& !_quarantined.load(std::memory_order_acquire)
			&& _superResolutionEligible;
	}

	void TemporalRenderer::RestoreNativeFrameState()
	{
		_upscaledThisFrame = false;
		_srPublishedToFramebuffer.store(false, std::memory_order_release);
		if (auto* state = cs::engine::GetGraphicsState()) {
			state->offsetX = 0.0f;
			state->offsetY = 0.0f;
		}
		cs::engine::SetDynamicResolutionRatios(1.0f, 1.0f);
		if (const auto* state = cs::engine::GetGraphicsState()) {
			_renderSize.SetNative(state->screenWidth, state->screenHeight);
		} else {
			_renderSize.SetNative(0, 0);
		}
		resolutionScale = { 1.0f, 1.0f };
		_resolutionScalePublished = false;
		_mipBias.store(0.0f, std::memory_order_relaxed);
		ForceViewportToRenderTargetDimensions();
	}

	void TemporalRenderer::RestoreNativeFrameStateOnce()
	{
		// Restore only once before returning ratio ownership to the engine.
		if (!_resolutionScalePublished) {
			return;
		}
		RestoreNativeFrameState();
		L->warn("Upscaling stopped driving frame state; native resolution and viewport restored");
	}

	void TemporalRenderer::ScheduleNativeSuperResolutionFallback() noexcept
	{
		ClearFrameGenerationCaptureState();
		render::TemporalPipeline::Get().RequestFrameGenerationReset();
		_nativeSuperResolutionFallbackPending.store(
			true, std::memory_order_release);
	}

	void TemporalRenderer::QuarantineAfterException(const char* a_where) noexcept
	{
		const bool alreadyQuarantined =
			_quarantined.exchange(true, std::memory_order_acq_rel);
		_resourcesReady.store(false, std::memory_order_release);
		_upscaledThisFrame = false;
		ClearFrameGenerationCaptureState();
		// An entered imagespace or material scope must finish its own restoration first.
		_quarantineCleanupPending.store(true, std::memory_order_release);
		render::TemporalPipeline::Get().PostFailure(
			render::temporal::FailureDomain::kEngine,
			a_where ? a_where : "Temporal engine integration failed.");
		if (!alreadyQuarantined) {
			try {
				L->critical(
					"Upscaling quarantined after an exception in {}; cleanup is deferred to the "
					"next frame boundary",
					a_where);
			} catch (...) {
			}
		}
	}

	void TemporalRenderer::FinalizeQuarantineAtFrameBoundary() noexcept
	{
		if (!_quarantineCleanupPending.exchange(false, std::memory_order_acq_rel)) {
			return;
		}
		try {
			RestoreNativeFrameState();
		} catch (...) {
		}
		try {
			dynamicResolution.Release();
		} catch (...) {
		}
		try {
			samplerBias.Release();
		} catch (...) {
		}
		_imagespaceRatiosNeutralized = false;
	}

	void TemporalRenderer::ApplyConfiguration(const Settings& a_settings, bool a_eligible) noexcept
	{
		settings = a_settings;
		_superResolutionEligible = a_eligible;
	}

	void TemporalRenderer::RejectInitialization(std::string a_reason)
	{
		_hooksInstalled.store(false, std::memory_order_release);
		_resourcesReady.store(false, std::memory_order_release);
		TemporalPipeline::Get().PostFailure(temporal::FailureDomain::kEngine, std::move(a_reason));
	}
}
