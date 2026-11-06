#include "Render/FrameBuffer.h"
#include "Render/SharedData.h"
#include "Render/TemporalRendererInternals.h"

#include <cmath>
#include <cstdint>

namespace cs::render
{
	using namespace renderer_detail;

	bool TemporalRenderer::PreflightExternalResolve(UpscaleMethod a_method)
	{
		if (!IsExternalUpscaler(a_method)) {
			return true;
		}

		auto* state = cs::engine::GetGraphicsState();
		auto* context = cs::engine::GetImmediateContext();
		auto* sceneColorRTV = cs::engine::GetRenderTargetRTV(kSceneColorTarget);
		const auto [renderWidth, renderHeight] = GetRenderSize();
		winrt::com_ptr<ID3D11Texture2D> sceneColor;
		D3D11_TEXTURE2D_DESC sceneColorDesc{};
		if (!state || !context || !sceneColorRTV ||
			!HasRequiredResources(a_method) ||
			!TryGetSceneColorTexture(sceneColor, sceneColorDesc) || !renderWidth ||
			!renderHeight || renderWidth > sceneColorDesc.Width ||
			renderHeight > sceneColorDesc.Height || !upscalingTexture ||
			!upscalingTexture->srv || !publicationTexture ||
			!publicationTexture->resource || !publicationTexture->rtv ||
			!cs::render::GetLinearClampSampler() || !upscalingDataCB || !upscaleRasterizerState ||
			!upscaleBlendState || !GetUpscaleVS() || !GetSpatialFallbackPS() ||
			!GetChainInputsUpscalePS()) {
			return false;
		}

		winrt::com_ptr<ID3D11Resource> renderTargetResource;
		sceneColorRTV->GetResource(renderTargetResource.put());
		return renderTargetResource.get() == sceneColor.get() &&
		       sceneColorDesc.Width == state->screenWidth &&
		       sceneColorDesc.Height == state->screenHeight;
	}

	render::temporal::ProviderResultCode TemporalRenderer::Upscale()
	{
		auto upscaleMethod = GetUpscaleMethod();
		if (!IsExternalUpscaler(upscaleMethod)) {
			return render::temporal::ProviderResultCode::kFailure;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		if (!context || !state) {
			return render::temporal::ProviderResultCode::kFailure;
		}
		// Resources are recreated across render-size and load transitions.
		if (!upscalingTexture || !superResolutionDepthTexture ||
			!reactiveMaskTexture || !transparencyCompositionMaskTexture) {
			return render::temporal::ProviderResultCode::kSkipped;
		}

		auto* motionVectorTexture =
			cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* motionVectorSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		if (!motionVectorTexture || !motionVectorSRV || !motionVectorCopyTexture) {
			return render::temporal::ProviderResultCode::kSkipped;
		}

		bool upscaled = false;
		bool providerSkipped = false;
		const auto [renderWidth, renderHeight] = GetRenderSize();
		if (renderWidth == 0 || renderHeight == 0) {
			return render::temporal::ProviderResultCode::kSkipped;
		}
		{
			// Keep OM unbound until provider reads complete.
			cs::engine::OMScope omScope(context);

			{
				cs::render::annotation::ScopedEvent encodeScope(
					"Upscaling::EncodeTextures");
				cs::ComputeScope computeScope(context);

				// Null t0 and the G-buffer normal's unused channel produce the accepted zero masks.
				auto* normalsSRV = cs::engine::GetRenderTargetSRV(kNormalsTarget);
				auto* depthSRV = cs::engine::GetDepthStencilDepthSRV(
					RE::BSGraphics::DepthStencilTargetID::kMain);
				auto* encodeShader = GetEncodeTexturesCS();
				if (!depthSRV || !encodeShader) {
					return render::temporal::ProviderResultCode::kSkipped;
				}

				ID3D11ShaderResourceView* views[4] = { nullptr, normalsSRV,
					motionVectorSRV, depthSRV };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);
				context->CSSetShader(encodeShader, nullptr, 0);

				UpscalingDataCB upscalingData;
				upscalingData.trueSamplingDim =
					float2((float)renderWidth, (float)renderHeight);
				upscalingData.pad0 = { 0.0f, 0.0f };
				upscalingDataCB->Update(upscalingData);
				auto upscalingBuffer = upscalingDataCB->CB();
				context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

				ID3D11UnorderedAccessView* uavs[4] = {
					reactiveMaskTexture->uav.get(),
					transparencyCompositionMaskTexture->uav.get(),
					motionVectorCopyTexture->uav.get(),
					superResolutionDepthTexture->uav.get()
				};
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

				context->Dispatch((renderWidth + 7) / 8, (renderHeight + 7) / 8, 1);

				ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr,
					nullptr };
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs,
					nullptr);

				ID3D11Buffer* nullBuffer = nullptr;
				context->CSSetConstantBuffers(0, 1, &nullBuffer);
			}

			const auto frameCount = state->frameCount;
			if (_lastDispatchedFrame.has_value() &&
				frameCount != _lastDispatchedFrame.value() + 1u) {
				render::TemporalPipeline::Get().RequestSuperResolutionReset();
			}
			const bool resetHistory =
				render::TemporalPipeline::Get().SuperResolutionResetPending();
			if (resetHistory) {
				_resetSubmissions.fetch_add(1, std::memory_order_relaxed);
			}
			const auto* timer = RE::BSTimer::GetSingleton();
			const auto realFrame = render::TemporalPipeline::Get().CurrentRealFrame();
			const auto snapshot = cs::engine::GetCapturedWorldCameraRecord(frameCount);
			const auto camera = render::temporal::BuildFrameGenerationCamera(
				snapshot, state->screenWidth, state->screenHeight);
			render::temporal::SuperResolutionRequest request{
				.recording =
					render::temporal::D3D11RecordingContext{ .context = context },
				.colorInput =
					render::temporal::D3D11GpuView{ .resource =
														upscalingTexture->resource.get(),
						.srv = upscalingTexture->srv.get(),
						.alias12 = upscalingTexture->resource12.get() },
				.privateOutput =
					render::temporal::D3D11GpuView{
						.resource = sharpenerTexture ? sharpenerTexture->resource.get() : nullptr,
						.srv = sharpenerTexture ? sharpenerTexture->srv.get() : nullptr,
						.uav =
							sharpenerTexture ? sharpenerTexture->uav.get() : nullptr,
						.alias12 =
							sharpenerTexture ? sharpenerTexture->resource12.get() : nullptr },
				.publicationOutput =
					render::temporal::D3D11GpuView{
						.resource = publicationTexture ? publicationTexture->resource.get() : nullptr,
						.srv = publicationTexture ? publicationTexture->srv.get() : nullptr,
						.uav =
							publicationTexture ? publicationTexture->uav.get() : nullptr,
						.alias12 =
							publicationTexture ? publicationTexture->resource12.get() : nullptr },
				.depth =
					render::temporal::D3D11GpuView{
						.resource = superResolutionDepthTexture->resource.get(),
						.alias12 =
							superResolutionDepthTexture->resource12.get() },
				.motionVectors =
					render::temporal::D3D11GpuView{
						.resource = motionVectorCopyTexture->resource.get(),
						.alias12 = motionVectorCopyTexture->resource12.get() },
				.reactiveMask =
					render::temporal::D3D11GpuView{
						.resource = reactiveMaskTexture->resource.get(),
						.alias12 = reactiveMaskTexture->resource12.get() },
				.transparencyCompositionMask =
					render::temporal::D3D11GpuView{
						.resource = transparencyCompositionMaskTexture->resource.get(),
						.alias12 =
							transparencyCompositionMaskTexture->resource12.get() },
				.renderWidth = renderWidth,
				.renderHeight = renderHeight,
				.outputWidth = state->screenWidth,
				.outputHeight = state->screenHeight,
				.qualityMode = settings.qualityMode,
				.providerPreset = settings.presetDLSS,
				.realFrame = realFrame,
				.engineFrame = frameCount,
				.jitterX = jitter.x,
				.jitterY = jitter.y,
				.sharpness = settings.sharpnessFSR,
				.postProcessSharpness = settings.sharpnessDLSS,
				.frameTimeMilliseconds =
					(timer ? timer->realTimeDelta : 0.0f) * 1000.0f,
				.resetHistory = resetHistory,
				.postProcessSharpening =
					upscaleMethod == UpscaleMethod::kDLSS &&
					settings.sharpnessEnabledDLSS &&
					settings.sharpnessDLSS > 0.0f,
				.color = { .resourceFormat = upscalingTexture->desc.Format,
					.range = render::temporal::ColorRange::kFull,
					.transfer = render::temporal::TransferFunction::kLinear,
					.primaries = render::temporal::ColorPrimaries::kUnspecified,
					.stage = render::temporal::ColorStage::kPreTonemap,
					.alpha = render::temporal::AlphaMode::kIgnored,
					.exposure = render::temporal::ExposureMode::kAutomatic },
				.camera = camera
			};
			(void)render::TemporalPipeline::Get().ApplyFrozenFrameConstants(
				request);

			render::temporal::ProviderResult providerResult;
			if (upscaleMethod == UpscaleMethod::kDLSS) {
				cs::render::annotation::ScopedEvent providerScope("Upscaling::DLSS");
				providerResult =
					render::TemporalPipeline::Get().EvaluateSuperResolution(
						render::temporal::SuperResolutionMethod::kDLSS, request);
			} else if (upscaleMethod == UpscaleMethod::kFSR3 ||
					   upscaleMethod == UpscaleMethod::kFSR4) {
				cs::render::annotation::ScopedEvent providerScope(
					upscaleMethod == UpscaleMethod::kFSR4 ? "Upscaling::FSR4" : "Upscaling::FSR3");
				providerResult =
					render::TemporalPipeline::Get().EvaluateSuperResolution(
						upscaleMethod, request);
			}
			upscaled = providerResult.CanPublishOutput();
			providerSkipped =
				providerResult.code == render::temporal::ProviderResultCode::kSkipped;
			_providerPublicationOutputReady =
				providerResult.publicationOutputReady;
			_superResolutionSubmissionUnsafe =
				providerResult.failureDomain ==
					render::temporal::FailureDomain::kTransport ||
				(providerResult.workState ==
						render::temporal::ProviderWorkState::kSubmitted &&
					!providerResult.outputDependencyEstablished);
			if (!upscaled &&
				providerResult.failureDomain ==
					render::temporal::FailureDomain::kTransport) {
				render::TemporalPipeline::Get().PostFailure(
					providerResult.failureDomain,
					providerResult.message.empty() ? "Super-resolution transport did not establish a safe output dependency." : providerResult.message);
			}

			if (upscaled) {
				_lastDispatchedFrame = frameCount;
			}
		}

		_upscaledThisFrame = upscaled;
		if (upscaled) {
			_upscaleDispatches.fetch_add(1, std::memory_order_relaxed);
			_consecutiveSkips = 0;
			return render::temporal::ProviderResultCode::kSuccess;
		}
		if (providerSkipped) {
			return render::temporal::ProviderResultCode::kSkipped;
		}
		_providerFailures.fetch_add(1, std::memory_order_relaxed);
		return render::temporal::ProviderResultCode::kFailure;
	}

	bool TemporalRenderer::NoteSuperResolutionSkip() noexcept
	{
		_transientFailures.fetch_add(1, std::memory_order_relaxed);
		render::TemporalPipeline::Get().SkipWorldFrame();
		return ++_consecutiveSkips >=
		       render::temporal::kMaxConsecutiveSuperResolutionSkips;
	}

	bool TemporalRenderer::PerformUpscaling()
	{
		cs::render::annotation::ScopedEvent upscaleScope("Upscaling::SuperResolution", false);
		_upscaledThisFrame = false;
		_spatialFallbackThisFrame.store(false, std::memory_order_release);
		_superResolutionSubmissionUnsafe = false;
		_providerPublicationOutputReady = false;
		// Keep the last completed resolve result stable across vfunc queries within
		// the frame.
		const auto finish = [this](bool a_resolved,
								bool a_externalPublished = false) {
			_upscaledThisFrame = a_externalPublished;
			_srPublishedToFramebuffer.store(a_externalPublished,
				std::memory_order_release);
			return a_resolved;
		};

		auto* context = cs::engine::GetImmediateContext();
		winrt::com_ptr<ID3D11Texture2D> sceneColor;
		D3D11_TEXTURE2D_DESC sceneColorDesc{};
		if (!context || !upscalingTexture ||
			!TryGetSceneColorTexture(sceneColor, sceneColorDesc) ||
			!HasSceneColorContract(sceneColorDesc) ||
			!MatchesTextureContract(upscalingTexture, sceneColorDesc,
				sceneColorDesc.Format)) {
			ScheduleNativeSuperResolutionFallback();
			return finish(false);
		}
		const auto [renderWidth, renderHeight] = GetRenderSize();
		if (!renderWidth || !renderHeight || renderWidth > sceneColorDesc.Width ||
			renderHeight > sceneColorDesc.Height) {
			ScheduleNativeSuperResolutionFallback();
			return finish(false);
		}

		{
			cs::render::annotation::ScopedEvent inputScope(
				"Upscaling::CaptureInputColor");
			cs::engine::CopyResourcePreservingOM(
				context, upscalingTexture->resource.get(), sceneColor.get());
		}

		const bool captured = cs::engine::GetCapturedWorldCameraRecord(GetEngineFrame()).has_value();
		if (!captured) {
			render::TemporalPipeline::Get().SkipWorldFrame();
		}
		const bool worldFrame = captured &&
		                        render::TemporalPipeline::Get().ClaimWorldFrame(GetEngineFrame());
		auto outcome = render::temporal::ProviderResultCode::kSkipped;
		if (worldFrame) {
			outcome = Upscale();
		}
		if (outcome != render::temporal::ProviderResultCode::kSuccess) {
			if (_superResolutionSubmissionUnsafe) {
				return finish(false);
			}
			if (!ApplySpatialFallback(sceneColor.get(), sceneColorDesc, renderWidth,
					renderHeight)) {
				render::TemporalPipeline::Get().PostFailure(
					render::temporal::FailureDomain::kEngine,
					"External super resolution and the spatial recovery resolve both "
					"failed.");
				ScheduleNativeSuperResolutionFallback();
				return finish(false);
			}
			_spatialFallbacks.fetch_add(1, std::memory_order_relaxed);
			_spatialFallbackThisFrame.store(true, std::memory_order_release);
			if (outcome == render::temporal::ProviderResultCode::kFailure) {
				L->warn(
					"spatial fallback published after provider evaluation failure; "
					"native TAA begins next frame.");
				ScheduleNativeSuperResolutionFallback();
			} else if (worldFrame) {
				if (NoteSuperResolutionSkip()) {
					L->error(
						"super resolution skipped {} consecutive world frames; "
						"native TAA begins next frame.",
						render::temporal::kMaxConsecutiveSuperResolutionSkips);
					ScheduleNativeSuperResolutionFallback();
				}
			} else if (captured) {
				// Later world frame of a latency frame: structural, not a provider skip.
				render::TemporalPipeline::Get().SkipWorldFrame();
			}
			UpscaleChainInputs();
			return finish(true, false);
		}

		const auto method = GetUpscaleMethod();
		bool published = false;
		if (method == UpscaleMethod::kFSR3 ||
			method == UpscaleMethod::kFSR4) {
			published = PublishUpscalingOutput(
				context, sceneColor.get(),
				sharpenerTexture ? sharpenerTexture->resource.get() : nullptr,
				_upscaledThisFrame);
		} else if (method == UpscaleMethod::kDLSS) {
			auto* output = _providerPublicationOutputReady ? publicationTexture : sharpenerTexture;
			published = PublishUpscalingOutput(
				context, sceneColor.get(),
				output ? output->resource.get() : nullptr,
				_upscaledThisFrame);
		}
		if (published) {
			render::TemporalPipeline::Get().ConsumeSuperResolutionReset(true);
			UpscaleChainInputs();
			return finish(true, true);
		}
		if (!ApplySpatialFallback(sceneColor.get(), sceneColorDesc, renderWidth,
				renderHeight)) {
			render::TemporalPipeline::Get().PostFailure(
				render::temporal::FailureDomain::kEngine,
				"Super-resolution publication and the spatial recovery resolve both "
				"failed.");
			ScheduleNativeSuperResolutionFallback();
			return finish(false);
		}
		_spatialFallbacks.fetch_add(1, std::memory_order_relaxed);
		_spatialFallbackThisFrame.store(true, std::memory_order_release);
		L->warn(
			"spatial fallback published after provider publication failure; "
			"native TAA begins next frame.");
		ScheduleNativeSuperResolutionFallback();
		UpscaleChainInputs();
		return finish(true, false);
	}

	bool TemporalRenderer::ApplySpatialFallback(
		ID3D11Texture2D* a_sceneColor,
		const D3D11_TEXTURE2D_DESC& a_sceneColorDesc, std::uint32_t a_sourceWidth,
		std::uint32_t a_sourceHeight)
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* sceneColorRTV = cs::engine::GetRenderTargetRTV(kSceneColorTarget);
		auto* vertexShader = GetUpscaleVS();
		auto* pixelShader = GetSpatialFallbackPS();
		if (!context || !a_sceneColor || !sceneColorRTV || !upscalingTexture ||
			!upscalingTexture->srv || !publicationTexture ||
			!publicationTexture->resource || !publicationTexture->rtv ||
			!cs::render::GetLinearClampSampler() || !upscalingDataCB || !upscaleRasterizerState ||
			!upscaleBlendState || !vertexShader || !pixelShader || !a_sourceWidth ||
			!a_sourceHeight || a_sourceWidth > a_sceneColorDesc.Width ||
			a_sourceHeight > a_sceneColorDesc.Height) {
			return false;
		}

		winrt::com_ptr<ID3D11Resource> renderTargetResource;
		sceneColorRTV->GetResource(renderTargetResource.put());
		if (renderTargetResource.get() != a_sceneColor) {
			return false;
		}

		{
			cs::render::annotation::ScopedEvent fallbackScope(
				"Upscaling::SpatialFallback");
			cs::engine::OMScope omScope(context);
			context->IASetInputLayout(nullptr);
			context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
			context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader, nullptr, 0);

			const D3D11_VIEWPORT viewport{
				.TopLeftX = 0.0f,
				.TopLeftY = 0.0f,
				.Width = static_cast<float>(a_sceneColorDesc.Width),
				.Height = static_cast<float>(a_sceneColorDesc.Height),
				.MinDepth = 0.0f,
				.MaxDepth = 1.0f
			};
			context->RSSetViewports(1, &viewport);
			context->RSSetState(upscaleRasterizerState.get());
			context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);
			context->OMSetDepthStencilState(nullptr, 0);
			auto* fallbackRTV = publicationTexture->rtv.get();
			context->OMSetRenderTargets(1, &fallbackRTV, nullptr);

			const UpscalingDataCB data{
				.trueSamplingDim = { static_cast<float>(a_sourceWidth),
					static_cast<float>(a_sourceHeight) },
				.pad0 = { 0.0f, 0.0f }
			};
			upscalingDataCB->Update(data);
			auto* constants = upscalingDataCB->CB();
			context->PSSetConstantBuffers(0, 1, &constants);
			auto* source = upscalingTexture->srv.get();
			context->PSSetShaderResources(0, 1, &source);
			auto* sampler = cs::render::GetLinearClampSampler();
			context->PSSetSamplers(0, 1, &sampler);
			context->PSSetShader(pixelShader, nullptr, 0);
			context->Draw(3, 0);

			ID3D11ShaderResourceView* nullSource = nullptr;
			context->PSSetShaderResources(0, 1, &nullSource);
			ID3D11Buffer* nullConstants = nullptr;
			context->PSSetConstantBuffers(0, 1, &nullConstants);
			context->PSSetShader(nullptr, nullptr, 0);
			context->VSSetShader(nullptr, nullptr, 0);
		}
		return PublishUpscalingOutput(context, a_sceneColor,
			publicationTexture->resource.get(), true);
	}

	void TemporalRenderer::UpscaleChainInputs()
	{
		if ((!_upscaledThisFrame &&
				!_spatialFallbackThisFrame.load(std::memory_order_acquire)) ||
			!IsUpscalingActive()) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		auto* fullscreenVS = GetUpscaleVS();
		if (!context || !state || !fullscreenVS || !cs::render::GetLinearClampSampler() ||
			!jitterCB || !upscalingDataCB || !upscaleRasterizerState ||
			!upscaleBlendState || !upscaleDepthStencilState) {
			return;
		}

		const float2 screenSize{ (float)state->screenWidth, (float)state->screenHeight };
		if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
			return;
		}

		cs::render::annotation::ScopedEvent annotationScope("Upscaling::UpscaleChainInputs");

		// Restore the engine's exact OM bindings after the passes.
		cs::engine::OMScope omScope(context);
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(fullscreenVS, nullptr, 0);

		const D3D11_VIEWPORT viewport{
			.TopLeftX = 0.0f,
			.TopLeftY = 0.0f,
			.Width = screenSize.x,
			.Height = screenSize.y,
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f
		};
		context->RSSetViewports(1, &viewport);
		context->RSSetState(upscaleRasterizerState.get());
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		UpscaleDepth(context);
		UpscaleMotionAndMasks(context);

		context->PSSetShader(nullptr, nullptr, 0);
		context->VSSetShader(nullptr, nullptr, 0);
	}

	void TemporalRenderer::UpscaleDepth(ID3D11DeviceContext* a_context)
	{
		auto* depthTexture =
			cs::engine::GetDepthStencilTexture(RE::BSGraphics::DepthStencilTargetID::kMain);
		auto* depthDSV =
			cs::engine::GetDepthStencilDSV(RE::BSGraphics::DepthStencilTargetID::kMain);
		auto* depthSRV =
			cs::engine::GetDepthStencilDepthSRV(RE::BSGraphics::DepthStencilTargetID::kMain);
		auto* refractionNormalsTexture =
			cs::engine::GetRenderTargetTexture(kRefractionNormalTarget);
		auto* refractionNormalsRTV =
			cs::engine::GetRenderTargetRTV(kRefractionNormalTarget);
		auto* depthUpscalePS = GetDepthRefractionUpscalePS();
		if (!depthTexture || !depthDSV || !depthSRV || !refractionNormalsTexture ||
			!refractionNormalsRTV || !depthUpscalePS) {
			return;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC depthSRVDesc{};
		depthSRV->GetDesc(&depthSRVDesc);
		D3D11_TEXTURE2D_DESC refractionNormalsDesc{};
		refractionNormalsTexture->GetDesc(&refractionNormalsDesc);
		if (!EnsureShaderReadableCopy(sceneDepthCopy, depthTexture, depthSRVDesc.Format,
				"Upscaling/SceneDepthCopy") ||
			!EnsureShaderReadableCopy(refractionNormalsCopy, refractionNormalsTexture,
				refractionNormalsDesc.Format, "Upscaling/RefractionNormalsCopy")) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope("Upscaling::UpscaleDepth");

		ID3D11SamplerState* samplers[] = { cs::render::GetLinearClampSampler() };
		a_context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

		JitterCB jitterData{};
		jitterData.jitter = jitter;
		jitterCB->Update(jitterData);
		auto bufferArray = jitterCB->CB();
		a_context->PSSetConstantBuffers(0, 1, &bufferArray);

		a_context->CopyResource(sceneDepthCopy->resource.get(), depthTexture);
		a_context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);
		a_context->CopyResource(refractionNormalsCopy->resource.get(), refractionNormalsTexture);

		ID3D11ShaderResourceView* srvs[] = { refractionNormalsCopy->srv.get(),
			sceneDepthCopy->srv.get() };
		a_context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		// Fallout 4's SAO derives camera-Z without a second output.
		ID3D11RenderTargetView* rtvs[] = { refractionNormalsRTV };
		a_context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, depthDSV);

		a_context->PSSetShader(depthUpscalePS, nullptr, 0);
		a_context->Draw(3, 0);

		ID3D11ShaderResourceView* nullPSResources[2] = { nullptr, nullptr };
		a_context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);
	}

	void TemporalRenderer::UpscaleMotionAndMasks(ID3D11DeviceContext* a_context)
	{
		auto* motionTexture = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* motionRTV = cs::engine::GetRenderTargetRTV(kMotionVectorTarget);
		auto* maskTexture = cs::engine::GetRenderTargetTexture(kPipboyMaskTarget);
		auto* maskRTV = cs::engine::GetRenderTargetRTV(kPipboyMaskTarget);
		auto* chainInputsPS = GetChainInputsUpscalePS();
		if (!motionTexture || !motionRTV || !maskTexture || !maskRTV || !chainInputsPS) {
			return;
		}

		D3D11_TEXTURE2D_DESC motionDesc{};
		D3D11_TEXTURE2D_DESC maskDesc{};
		motionTexture->GetDesc(&motionDesc);
		maskTexture->GetDesc(&maskDesc);
		if (!EnsureShaderReadableCopy(motionVectorSourceCopy, motionTexture, motionDesc.Format,
				"Upscaling/MotionVectorSourceCopy") ||
			!EnsureShaderReadableCopy(pipboyMaskCopy, maskTexture, maskDesc.Format,
				"Upscaling/PipboyMaskCopy")) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope("Upscaling::UpscaleMotionAndMasks");

		// Logical 15 doubles as the menu background; VATS gets its own copy.
		auto* vatsEffect = cs::engine::GetImageSpaceEffect(
			RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_VATS_TARGET);
		auto* vatsSource = cs::engine::GetRenderTargetTexture(kVatsMaskTarget);
		auto* vatsSourceSRV = cs::engine::GetRenderTargetSRV(kVatsMaskTarget);
		const bool vats = vatsEffect && vatsEffect->IsActive() && vatsSource && vatsSourceSRV &&
		                  EnsureShaderReadableCopy(vatsMaskFull, vatsSource, DXGI_FORMAT_R8G8B8A8_UNORM,
							  "Upscaling/VatsMaskFull", true);

		const auto [renderWidth, renderHeight] = GetRenderSize();
		upscalingDataCB->Update(UpscalingDataCB{
			.trueSamplingDim = { static_cast<float>(renderWidth), static_cast<float>(renderHeight) },
			.pad0 = { 0.0f, 0.0f } });
		auto constants = upscalingDataCB->CB();
		a_context->PSSetConstantBuffers(0, 1, &constants);

		a_context->CopyResource(motionVectorSourceCopy->resource.get(), motionTexture);
		a_context->CopyResource(pipboyMaskCopy->resource.get(), maskTexture);
		ID3D11ShaderResourceView* srvs[] = { motionVectorSourceCopy->srv.get(),
			pipboyMaskCopy->srv.get(), vats ? vatsSourceSRV : pipboyMaskCopy->srv.get() };
		a_context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { motionRTV, maskRTV, vats ? vatsMaskFull->rtv.get() : nullptr };
		a_context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);
		a_context->PSSetShader(chainInputsPS, nullptr, 0);
		a_context->Draw(3, 0);

		ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
		a_context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);
		_vatsMaskUpscaled = vats;
	}

	void TemporalRenderer::ResolveSceneBeforeImagespace()
	{
		_resolveSeamSeen.store(true, std::memory_order_release);
		LatchFrameGenerationDecision();
		_sceneResolved = false;
		_vatsMaskUpscaled = false;
		_preUiHandoff = {};
		if (!_imagespaceScope) {
			return;
		}
		_preUiHandoff = temporal::PlanPreUiHandoff(
			_renderUiFullEffectsPath.load(std::memory_order_acquire),
			ShouldUseFrameGenerationThisFrame(),
			IsDrivingFrameState());
		if (_preUiHandoff.captureFrameGenerationInputs) {
			CaptureFrameGenerationInputs();
		}
		if (!_preUiHandoff.driveSuperResolution) {
			_sceneResolved = true;
			return;
		}

		const auto upscaleMethod = GetUpscaleMethod();
		const bool external = IsExternalUpscaler(upscaleMethod);
		bool upscaled = true;
		if (external) {
			upscaled = PerformUpscaling();
		} else {
			_srPublishedToFramebuffer.store(false, std::memory_order_release);
		}
		// Freeze previews after temporal inputs settle.
		CaptureSelectedDebugSnapshot();

		if (external && upscaled) {
			// The chain, UI and native TAA now run full-extent without jitter.
			if (auto* state = cs::engine::GetGraphicsState()) {
				state->offsetX = 0.0f;
				state->offsetY = 0.0f;
			}
			cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
			// The engine bakes the render ratio into its dynamic triangle UVs.
			_chainTriangle.Apply();
			// Only the chain's VATS effect reads logical 15.
			if (_vatsMaskUpscaled) {
				_vatsMaskSwap.Apply(kVatsMaskTarget, vatsMaskFull->resource.get(),
					vatsMaskFull->srv.get(), vatsMaskFull->rtv.get());
			}
		}
		// False means the provider and spatial recovery both failed.
		_sceneResolved = upscaled;
	}

	void TemporalRenderer::FinishPreUiSeam()
	{
		if (!_imagespaceScope) {
			return;
		}
		if (!_preUiHandoff.driveSuperResolution) {
			FinishPreUiScene(
				true, _preUiHandoff.ShouldCaptureHudlessColor(true));
			return;
		}

		// Jitter must not leak into UI regardless of whether the resolve succeeded.
		if (auto* state = cs::engine::GetGraphicsState()) {
			state->offsetX = 0.0f;
			state->offsetY = 0.0f;
		}
		FinishPreUiScene(
			_sceneResolved, _preUiHandoff.ShouldCaptureHudlessColor(_sceneResolved));
		SetTemporalEnabled(GetUpscaleMethod() == UpscaleMethod::kTAA);
	}

}  // namespace cs::render
