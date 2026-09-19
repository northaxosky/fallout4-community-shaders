#include "Render/TemporalRendererInternals.h"
#include "Render/FrameBuffer.h"

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
		auto* frameBufferRTV =
			cs::engine::GetRenderTargetRTV(cs::engine::RenderTarget::kFrameBuffer);
		const auto [renderWidth, renderHeight] = GetRenderSize();
		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!state || !context || !frameBufferRTV ||
			!HasRequiredResources(a_method) ||
			!TryGetFrameBufferTexture(frameBuffer, frameBufferDesc) || !renderWidth ||
			!renderHeight || renderWidth > frameBufferDesc.Width ||
			renderHeight > frameBufferDesc.Height || !upscalingTexture ||
			!upscalingTexture->srv || !publicationTexture ||
			!publicationTexture->resource || !publicationTexture->rtv ||
			!linearSampler || !upscalingDataCB || !upscaleRasterizerState ||
			!upscaleBlendState || !GetUpscaleVS() || !GetSpatialFallbackPS()) {
			return false;
		}

		winrt::com_ptr<ID3D11Resource> renderTargetResource;
		frameBufferRTV->GetResource(renderTargetResource.put());
		return renderTargetResource.get() == frameBuffer.get() &&
		       frameBufferDesc.Width == state->screenWidth &&
		       frameBufferDesc.Height == state->screenHeight;
	}

	bool TemporalRenderer::Upscale()
	{
		auto upscaleMethod = GetUpscaleMethod();
		if (!IsExternalUpscaler(upscaleMethod)) {
			return false;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		if (!context || !state || !upscalingTexture || !superResolutionDepthTexture ||
			!reactiveMaskTexture || !transparencyCompositionMaskTexture) {
			return false;
		}

		auto* motionVectorTexture =
			cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* motionVectorSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		if (!motionVectorTexture || !motionVectorSRV || !motionVectorCopyTexture) {
			return false;
		}

		bool upscaled = false;
		const auto [renderWidth, renderHeight] = GetRenderSize();
		if (renderWidth == 0 || renderHeight == 0) {
			return false;
		}
		{
			// Keep OM unbound until provider reads complete.
			cs::engine::OMScope omScope(context);

			{
				cs::render::annotation::ScopedEvent encodeScope(
					"Upscaling/EncodeTextures");
				cs::ComputeScope computeScope(context);

				// Null t0 and RT20's unused channel produce the accepted zero masks.
				auto* normalsSRV = cs::engine::GetRenderTargetSRV(kNormalsTarget);
				auto* depthSRV = cs::engine::GetDepthStencilDepthSRV(
					cs::engine::DepthStencilTarget::kMain);
				auto* encodeShader = GetEncodeTexturesCS();
				if (!depthSRV || !encodeShader) {
					return false;
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
			const auto* timer = RE::BSTimer::GetSingleton();
			const auto realFrame = render::TemporalPipeline::Get().CurrentRealFrame();
			const auto& snapshot = cs::engine::GetFrameBuffer();
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
				.realFrame = realFrame ? realFrame : frameCount,
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
				.color = { .resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
					.range = render::temporal::ColorRange::kFull,
					.transfer = render::temporal::TransferFunction::kGamma22,
					.primaries = render::temporal::ColorPrimaries::kUnspecified,
					.stage = render::temporal::ColorStage::kPostTonemapLut,
					.alpha = render::temporal::AlphaMode::kIgnored,
					.exposure = render::temporal::ExposureMode::kAutomatic },
				.camera = camera
			};
			(void)render::TemporalPipeline::Get().ApplyFrozenFrameConstants(
				request);

			render::temporal::ProviderResult providerResult;
			if (upscaleMethod == UpscaleMethod::kDLSS) {
				cs::render::annotation::ScopedEvent providerScope("Upscaling/DLSS");
				providerResult =
					render::TemporalPipeline::Get().EvaluateSuperResolution(
						render::temporal::SuperResolutionMethod::kDLSS, request);
			} else if (upscaleMethod == UpscaleMethod::kFSR ||
				upscaleMethod == UpscaleMethod::kFSR4) {
				cs::render::annotation::ScopedEvent providerScope(
					upscaleMethod == UpscaleMethod::kFSR4
						? "Upscaling/FSR4"
						: "Upscaling/FSR3");
				providerResult =
					render::TemporalPipeline::Get().EvaluateSuperResolution(
						upscaleMethod == UpscaleMethod::kFSR4
							? render::temporal::SuperResolutionMethod::kFSR4
							: render::temporal::SuperResolutionMethod::kFSR3,
						request);
			}
			upscaled = providerResult.CanPublishOutput();
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
					providerResult.message.empty()
						? "Super-resolution transport did not establish a safe output dependency."
						: providerResult.message);
			}

			if (upscaled) {
				_lastDispatchedFrame = frameCount;
			}
		}

		if (upscaled) {
			_upscaleDispatches.fetch_add(1, std::memory_order_relaxed);
		} else {
			_providerFailures.fetch_add(1, std::memory_order_relaxed);
		}
		_upscaledThisFrame = upscaled;
		return upscaled;
	}

	bool TemporalRenderer::PerformUpscaling()
	{
		cs::render::annotation::ScopedEvent upscaleScope("Upscaling/SuperResolution");
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
		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!context || !upscalingTexture ||
			!TryGetFrameBufferTexture(frameBuffer, frameBufferDesc) ||
			!HasSDRUpscalingContract(frameBufferDesc) ||
			!MatchesTextureContract(upscalingTexture, frameBufferDesc,
				DXGI_FORMAT_R8G8B8A8_UNORM)) {
			ScheduleNativeSuperResolutionFallback();
			return finish(false);
		}
		const auto [renderWidth, renderHeight] = GetRenderSize();
		if (!renderWidth || !renderHeight || renderWidth > frameBufferDesc.Width ||
			renderHeight > frameBufferDesc.Height) {
			ScheduleNativeSuperResolutionFallback();
			return finish(false);
		}

		{
			cs::render::annotation::ScopedEvent inputScope(
				"Upscaling/CaptureInputColor");
			cs::engine::CopyResourcePreservingOM(
				context, upscalingTexture->resource.get(), frameBuffer.get());
		}

		if (!Upscale()) {
			if (_superResolutionSubmissionUnsafe) {
				return finish(false);
			}
			if (!ApplySpatialFallback(frameBuffer.get(), frameBufferDesc, renderWidth,
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
			L->warn(
				"spatial fallback published after provider evaluation failure; "
				"native TAA begins next frame.");
			ScheduleNativeSuperResolutionFallback();
			UpscaleDepth();
			return finish(true, false);
		}

		const auto method = GetUpscaleMethod();
		bool published = false;
		if (method == UpscaleMethod::kFSR ||
			method == UpscaleMethod::kFSR4) {
			published = PublishUpscalingOutput(
				context, frameBuffer.get(),
				sharpenerTexture ? sharpenerTexture->resource.get() : nullptr,
				_upscaledThisFrame);
		} else if (method == UpscaleMethod::kDLSS) {
			auto* output = _providerPublicationOutputReady
				? publicationTexture
				: sharpenerTexture;
			published = PublishUpscalingOutput(
				context, frameBuffer.get(),
				output ? output->resource.get() : nullptr,
				_upscaledThisFrame);
		}
		if (published) {
			render::TemporalPipeline::Get().ConsumeSuperResolutionReset(true);
			UpscaleDepth();
			return finish(true, true);
		}
		if (!ApplySpatialFallback(frameBuffer.get(), frameBufferDesc, renderWidth,
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
		UpscaleDepth();
		return finish(true, false);
	}

	bool TemporalRenderer::RecoverMissedResolveAtRenderUIReturn()
	{
		auto* context = cs::engine::GetImmediateContext();
		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!context || !upscalingTexture ||
			!TryGetFrameBufferTexture(frameBuffer, frameBufferDesc) ||
			!HasSDRUpscalingContract(frameBufferDesc) ||
			!MatchesTextureContract(upscalingTexture, frameBufferDesc,
				DXGI_FORMAT_R8G8B8A8_UNORM)) {
			ScheduleNativeSuperResolutionFallback();
			return false;
		}
		UINT viewportCount = 1;
		D3D11_VIEWPORT sourceViewport{};
		context->RSGetViewports(&viewportCount, &sourceViewport);
		const auto [renderWidth, renderHeight] = GetRenderSize();
		const auto outputExtent =
			viewportCount == 1 ? cs::engine::ClassifyRenderUIOutputExtent(
									 sourceViewport.TopLeftX, sourceViewport.TopLeftY,
									 sourceViewport.Width, sourceViewport.Height, renderWidth,
									 renderHeight, frameBufferDesc.Width, frameBufferDesc.Height) :
								 cs::engine::RenderUIOutputExtent::kInvalid;
		if (outputExtent == cs::engine::RenderUIOutputExtent::kInvalid) {
			_renderUiRecoverySourceWidth.store(0, std::memory_order_relaxed);
			_renderUiRecoverySourceHeight.store(0, std::memory_order_relaxed);
			_renderUiRecoveryPassthrough.store(false, std::memory_order_release);
			ScheduleNativeSuperResolutionFallback();
			return false;
		}

		const bool passthrough =
			outputExtent == cs::engine::RenderUIOutputExtent::kFull;
		const auto sourceWidth = passthrough ? frameBufferDesc.Width : renderWidth;
		const auto sourceHeight = passthrough ? frameBufferDesc.Height : renderHeight;
		_renderUiRecoverySourceWidth.store(sourceWidth, std::memory_order_relaxed);
		_renderUiRecoverySourceHeight.store(sourceHeight, std::memory_order_relaxed);
		_renderUiRecoveryPassthrough.store(passthrough, std::memory_order_release);

		{
			cs::render::annotation::ScopedEvent inputScope(
				"Upscaling/CaptureMissedResolveInputColor");
			cs::engine::CopyResourcePreservingOM(
				context, upscalingTexture->resource.get(), frameBuffer.get());
		}

		const bool published =
			passthrough ? ApplyPassthroughFallback(frameBuffer.get(), context) : ApplySpatialFallback(frameBuffer.get(), frameBufferDesc, sourceWidth, sourceHeight);
		if (!published) {
			ScheduleNativeSuperResolutionFallback();
			return false;
		}

		_upscaledThisFrame = false;
		_srPublishedToFramebuffer.store(false, std::memory_order_release);
		_spatialFallbacks.fetch_add(1, std::memory_order_relaxed);
		_spatialFallbackThisFrame.store(true, std::memory_order_release);
		ScheduleNativeSuperResolutionFallback();
		UpscaleDepth();
		return true;
	}

	bool TemporalRenderer::ApplySpatialFallback(
		ID3D11Texture2D* a_frameBuffer,
		const D3D11_TEXTURE2D_DESC& a_frameBufferDesc, std::uint32_t a_sourceWidth,
		std::uint32_t a_sourceHeight)
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* frameBufferRTV =
			cs::engine::GetRenderTargetRTV(cs::engine::RenderTarget::kFrameBuffer);
		auto* vertexShader = GetUpscaleVS();
		auto* pixelShader = GetSpatialFallbackPS();
		if (!context || !a_frameBuffer || !frameBufferRTV || !upscalingTexture ||
			!upscalingTexture->srv || !publicationTexture ||
			!publicationTexture->resource || !publicationTexture->rtv ||
			!linearSampler || !upscalingDataCB || !upscaleRasterizerState ||
			!upscaleBlendState || !vertexShader || !pixelShader || !a_sourceWidth ||
			!a_sourceHeight || a_sourceWidth > a_frameBufferDesc.Width ||
			a_sourceHeight > a_frameBufferDesc.Height) {
			return false;
		}

		winrt::com_ptr<ID3D11Resource> renderTargetResource;
		frameBufferRTV->GetResource(renderTargetResource.put());
		if (renderTargetResource.get() != a_frameBuffer) {
			return false;
		}

		cs::render::annotation::ScopedEvent fallbackScope(
			"Upscaling/SpatialFallback");
		{
			cs::engine::OMScope omScope(context);
			context->IASetInputLayout(nullptr);
			context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
			context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader, nullptr, 0);

			const D3D11_VIEWPORT viewport{
				.TopLeftX = 0.0f,
				.TopLeftY = 0.0f,
				.Width = static_cast<float>(a_frameBufferDesc.Width),
				.Height = static_cast<float>(a_frameBufferDesc.Height),
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
			auto* sampler = linearSampler.get();
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
		return PublishUpscalingOutput(context, a_frameBuffer,
			publicationTexture->resource.get(), true);
	}

	bool TemporalRenderer::ApplyPassthroughFallback(
		ID3D11Texture2D* a_frameBuffer, ID3D11DeviceContext* a_context)
	{
		if (!a_frameBuffer || !a_context || !upscalingTexture ||
			!upscalingTexture->resource || !publicationTexture ||
			!publicationTexture->resource) {
			return false;
		}

		cs::render::annotation::ScopedEvent passthroughScope(
			"Upscaling/FullExtentPassthrough");
		if (!PrepareUpscalingPassthrough(a_context,
				publicationTexture->resource.get(),
				upscalingTexture->resource.get())) {
			return false;
		}
		return PublishUpscalingOutput(a_context, a_frameBuffer,
			publicationTexture->resource.get(), true);
	}

	void TemporalRenderer::UpscaleDepth()
	{
		if ((!_upscaledThisFrame &&
				!_spatialFallbackThisFrame.load(std::memory_order_acquire)) ||
			!IsUpscalingActive()) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		if (!context || !state || !linearSampler || !jitterCB ||
			!upscaleRasterizerState || !upscaleBlendState ||
			!upscaleDepthStencilState) {
			return;
		}

		float2 screenSize{ (float)state->screenWidth, (float)state->screenHeight };
		if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
			return;
		}

		auto* depthTexture =
			cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		auto* depthDSV =
			cs::engine::GetDepthStencilDSV(cs::engine::DepthStencilTarget::kMain);
		auto* depthCopyTexture = cs::engine::GetDepthStencilTexture(
			cs::engine::DepthStencilTarget::kMainCopy);
		auto* depthCopySRV = cs::engine::GetDepthStencilDepthSRV(
			cs::engine::DepthStencilTarget::kMainCopy);
		auto* depthCopyStencilSRV = cs::engine::GetDepthStencilStencilSRV(
			cs::engine::DepthStencilTarget::kMainCopy);
		auto* refractionNormalsTexture =
			cs::engine::GetRenderTargetTexture(kRefractionNormalTarget);
		auto* refractionNormalsCopy =
			cs::engine::GetRenderTargetCopyTexture(kRefractionNormalTarget);
		auto* refractionNormalsCopySRV =
			cs::engine::GetRenderTargetCopySRV(kRefractionNormalTarget);
		auto* refractionNormalsRTV =
			cs::engine::GetRenderTargetRTV(kRefractionNormalTarget);

		if (!depthTexture || !depthDSV || !depthCopyTexture || !depthCopySRV ||
			!refractionNormalsTexture || !refractionNormalsCopy ||
			!refractionNormalsCopySRV || !refractionNormalsRTV) {
			return;
		}

		auto* fullscreenVS = GetUpscaleVS();
		auto* depthUpscalePS = GetDepthRefractionUpscalePS();
		if (!fullscreenVS || !depthUpscalePS) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope("Upscaling/UpscaleDepth");

		// Restore the engine's exact OM bindings after the depth pass.
		cs::engine::OMScope omScope(context);
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		context->VSSetShader(fullscreenVS, nullptr, 0);

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenSize.x;
		viewport.Height = screenSize.y;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		context->RSSetState(upscaleRasterizerState.get());
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		ID3D11SamplerState* samplers[] = { linearSampler.get() };
		context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

		JitterCB jitterData{};
		jitterData.jitter = jitter;

		jitterCB->Update(jitterData);
		auto bufferArray = jitterCB->CB();
		context->PSSetConstantBuffers(0, 1, &bufferArray);

		const auto copyIfNonAliased = [&](ID3D11Resource* dst, ID3D11Resource* src) {
			if (dst && src && dst != src) {
				context->CopyResource(dst, src);
			}
		};

		{
			copyIfNonAliased(depthCopyTexture, depthTexture);

			context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

			copyIfNonAliased(refractionNormalsCopy, refractionNormalsTexture);

			ID3D11ShaderResourceView* srvs[] = { refractionNormalsCopySRV, depthCopySRV,
				depthCopyStencilSRV };
			context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

			// Fallout 4's SAO derives camera-Z without a second output.
			ID3D11RenderTargetView* rtvs[] = { refractionNormalsRTV };
			context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, depthDSV);

			context->PSSetShader(depthUpscalePS, nullptr, 0);
			context->Draw(3, 0);
		}

		ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
		context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

		context->PSSetShader(nullptr, nullptr, 0);
		context->VSSetShader(nullptr, nullptr, 0);
	}

}  // namespace cs::render
