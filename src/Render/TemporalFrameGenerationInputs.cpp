#include "Render/TemporalRendererInternals.h"
#include "Render/FrameGenerationOrchestration.h"

namespace cs::render
{
	using namespace renderer_detail;

	void TemporalRenderer::PrepareFirstPersonAlphaInputs()
	{
		InvalidateFirstPersonAlphaState();
		if (!_resourcesReady.load(std::memory_order_acquire) ||
			!ShouldUseFrameGenerationThisFrame() ||
			!_copyDepthForFrameGenerationCS ||
			!_frameGenerationCopyCB) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* preAlphaColor = cs::engine::GetRenderTargetTexture(kPreAlphaColorTarget);
		auto* preAlphaSRV = cs::engine::GetRenderTargetSRV(kPreAlphaColorTarget);
		auto* postAlphaColor = cs::engine::GetRenderTargetTexture(kSceneColorTarget);
		auto* postAlphaSRV = cs::engine::GetRenderTargetSRV(kSceneColorTarget);
		auto* nativeMotion = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* nativeMotionSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		auto* nativeDepth =
			cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		auto* nativeDepthSRV =
			cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
		const auto capture =
			render::TemporalPipeline::Get()
				.GetFrameGenerationCaptureResources();
		const auto& sharedMotion = capture.motion;
		const auto& sharedDepth = capture.depth;
		if (!context ||
			!HaveMatchingCopyContract(preAlphaColor, postAlphaColor) ||
			!ViewReferencesTexture(preAlphaSRV, preAlphaColor) ||
			!ViewReferencesTexture(postAlphaSRV, postAlphaColor) ||
			!ViewReferencesTexture(nativeMotionSRV, nativeMotion) ||
			!ViewReferencesTexture(nativeDepthSRV, nativeDepth) ||
			!sharedMotion.resource ||
			!sharedMotion.uav ||
			!sharedDepth.resource ||
			!sharedDepth.uav) {
			return;
		}

		const auto [renderWidth, renderHeight] = GetRenderSize();
		D3D11_TEXTURE2D_DESC colorDesc{};
		D3D11_TEXTURE2D_DESC motionDesc{};
		D3D11_TEXTURE2D_DESC depthDesc{};
		D3D11_TEXTURE2D_DESC sharedMotionDesc{};
		D3D11_TEXTURE2D_DESC sharedDepthDesc{};
		preAlphaColor->GetDesc(&colorDesc);
		nativeMotion->GetDesc(&motionDesc);
		nativeDepth->GetDesc(&depthDesc);
		sharedMotion.resource->GetDesc(&sharedMotionDesc);
		sharedDepth.resource->GetDesc(&sharedDepthDesc);
		if (!renderWidth ||
			!renderHeight ||
			renderWidth > colorDesc.Width ||
			renderHeight > colorDesc.Height ||
			renderWidth > motionDesc.Width ||
			renderHeight > motionDesc.Height ||
			renderWidth > depthDesc.Width ||
			renderHeight > depthDesc.Height ||
			sharedMotionDesc.Width != capture.width ||
			sharedMotionDesc.Height != capture.height ||
			sharedDepthDesc.Width != sharedMotionDesc.Width ||
			sharedDepthDesc.Height != sharedMotionDesc.Height ||
			motionDesc.Format != DXGI_FORMAT_R16G16_FLOAT ||
			sharedMotionDesc.Format != DXGI_FORMAT_R16G16_FLOAT ||
			sharedDepthDesc.Format != DXGI_FORMAT_R32_FLOAT) {
			return;
		}

		{
			cs::render::annotation::ScopedEvent annotationScope(
				"Upscaling/FrameGeneration/CapturePreAlphaColor");
			cs::engine::CopyResourcePreservingOM(
				context, preAlphaColor, postAlphaColor);
		}
		_firstPersonAlphaStamp = {
			.stage = FirstPersonAlphaStage::kPrepared,
			.engineFrame = GetEngineFrame(),
			.preAlphaColor = preAlphaColor,
			.postAlphaColor = postAlphaColor,
			.nativeMotion = nativeMotion,
			.nativeDepth = nativeDepth,
			.sharedMotion = sharedMotion.resource,
			.sharedDepth = sharedDepth.resource
		};
	}

	void TemporalRenderer::FinishFirstPersonAlphaInputs()
	{
		const auto stamp = _firstPersonAlphaStamp;
		InvalidateFirstPersonAlphaState();
		if (stamp.stage != FirstPersonAlphaStage::kPrepared ||
			stamp.engineFrame != GetEngineFrame() ||
			!ShouldUseFrameGenerationThisFrame()) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* preAlphaColor = cs::engine::GetRenderTargetTexture(kPreAlphaColorTarget);
		auto* preAlphaSRV = cs::engine::GetRenderTargetSRV(kPreAlphaColorTarget);
		auto* postAlphaColor = cs::engine::GetRenderTargetTexture(kSceneColorTarget);
		auto* postAlphaSRV = cs::engine::GetRenderTargetSRV(kSceneColorTarget);
		auto* nativeMotion = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* nativeMotionSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		auto* nativeDepth =
			cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		auto* nativeDepthSRV =
			cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
		const auto capture =
			render::TemporalPipeline::Get()
				.GetFrameGenerationCaptureResources();
		const auto& sharedMotion = capture.motion;
		const auto& sharedDepth = capture.depth;
		if (!context ||
			preAlphaColor != stamp.preAlphaColor ||
			postAlphaColor != stamp.postAlphaColor ||
			nativeMotion != stamp.nativeMotion ||
			nativeDepth != stamp.nativeDepth ||
			sharedMotion.resource != stamp.sharedMotion ||
			!sharedMotion.uav ||
			sharedDepth.resource != stamp.sharedDepth ||
			!sharedDepth.uav ||
			!ViewReferencesTexture(preAlphaSRV, preAlphaColor) ||
			!ViewReferencesTexture(postAlphaSRV, postAlphaColor) ||
			!ViewReferencesTexture(nativeMotionSRV, nativeMotion) ||
			!ViewReferencesTexture(nativeDepthSRV, nativeDepth)) {
			return;
		}

		const auto [renderWidth, renderHeight] = GetRenderSize();
		const FrameGenerationCopyCB dimensions{
			renderWidth,
			renderHeight,
			capture.width,
			capture.height,
			1,
			0,
			0,
			0
		};
		if (!dimensions.renderWidth || !dimensions.renderHeight ||
			!dimensions.outputWidth || !dimensions.outputHeight) {
			return;
		}
		if (!temporal::WriteFrameGenerationInput(
				[]() {
					return render::TemporalPipeline::Get()
						.AcquireFrameGenerationInputWrite();
				},
				[&]() {
					cs::render::annotation::ScopedEvent annotationScope(
						"FG_CaptureDepthMotion");
					cs::engine::ComputeOMScope scope(context, 4, 0, 2, 1);
					_frameGenerationCopyCB->Update(dimensions);
					ID3D11Buffer* constantBuffer = _frameGenerationCopyCB->CB();
					context->CSSetConstantBuffers(0, 1, &constantBuffer);
					ID3D11ShaderResourceView* srvs[] = {
						nativeDepthSRV,
						nativeMotionSRV,
						preAlphaSRV,
						postAlphaSRV
					};
					context->CSSetShaderResources(
						0, static_cast<UINT>(std::size(srvs)), srvs);
					ID3D11UnorderedAccessView* uavs[] = {
						sharedDepth.uav,
						sharedMotion.uav
					};
					context->CSSetUnorderedAccessViews(
						0,
						static_cast<UINT>(std::size(uavs)),
						uavs,
						nullptr);
					context->CSSetShader(
						_copyDepthForFrameGenerationCS.get(), nullptr, 0);
					context->Dispatch(
						(dimensions.outputWidth + 7) / 8,
						(dimensions.outputHeight + 7) / 8,
						1);
				})) {
			render::TemporalPipeline::Get().FailFrameGenerationFrame(
				"frame-generation shared inputs were still in provider use");
			return;
		}

		_firstPersonAlphaStamp = stamp;
		_firstPersonAlphaStamp.stage = FirstPersonAlphaStage::kConditioned;
	}

	void TemporalRenderer::BeginFrameGenerationCaptureState() noexcept
	{
		_frameGenerationInputsCaptured = false;
		_hudlessCapturePending = false;
		render::TemporalPipeline::Get()
			.ResetFrameGenerationCaptureDiagnostics();
		render::TemporalPipeline::Get().ResetFsrFrameGenerationCamera();
		render::TemporalPipeline::Get().SetFrameGenerationInputsReady(false);
	}

	void TemporalRenderer::InvalidateFirstPersonAlphaState() noexcept
	{
		_firstPersonAlphaStamp = {};
	}

	bool TemporalRenderer::ConsumeFirstPersonAlphaInputs(
		std::uint64_t a_engineFrame,
		ID3D11Texture2D* a_nativeMotion,
		ID3D11Texture2D* a_nativeDepth,
		ID3D11Texture2D* a_sharedMotion,
		ID3D11Texture2D* a_sharedDepth) noexcept
	{
		const auto stamp = _firstPersonAlphaStamp;
		InvalidateFirstPersonAlphaState();
		return stamp.stage == FirstPersonAlphaStage::kConditioned &&
			stamp.engineFrame == a_engineFrame &&
			stamp.nativeMotion == a_nativeMotion &&
			stamp.nativeDepth == a_nativeDepth &&
			stamp.sharedMotion == a_sharedMotion &&
			stamp.sharedDepth == a_sharedDepth;
	}

	void TemporalRenderer::CaptureFrameGenerationInputs()
	{
		BeginFrameGenerationCaptureState();
		if (render::TemporalPipeline::Get().ArmFrameGenerationReset()) {
			InvalidateFirstPersonAlphaState();
			render::TemporalPipeline::Get().RequestFsrFrameGenerationReset();
		}
		const auto capture =
			render::TemporalPipeline::Get()
				.GetFrameGenerationCaptureResources();
		if (!capture.ready || !_copyDepthForFrameGenerationCS ||
			!_frameGenerationCopyCB) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* motion = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* motionSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		auto* depth =
			cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		auto* depthSRV =
			cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
		const auto& sharedMotion = capture.motion;
		const auto& sharedDepth = capture.depth;
		if (!context || !motion || !motionSRV || !depth || !depthSRV ||
			!sharedMotion.resource || !sharedMotion.uav ||
			!sharedDepth.resource || !sharedDepth.uav) {
			return;
		}
		const auto [renderWidth, renderHeight] = GetRenderSize();
		D3D11_TEXTURE2D_DESC sourceMotionDesc{};
		D3D11_TEXTURE2D_DESC targetMotionDesc{};
		motion->GetDesc(&sourceMotionDesc);
		sharedMotion.resource->GetDesc(&targetMotionDesc);
		if (sourceMotionDesc.Width != targetMotionDesc.Width ||
			sourceMotionDesc.Height != targetMotionDesc.Height ||
			sourceMotionDesc.Format != DXGI_FORMAT_R16G16_FLOAT ||
			targetMotionDesc.Format != DXGI_FORMAT_R16G16_FLOAT) {
			render::TemporalPipeline::Get().FailFrameGenerationFrame(
				"RT29 motion-vector contract is incompatible");
			return;
		}

		const bool alphaConditioned = ConsumeFirstPersonAlphaInputs(
			GetEngineFrame(),
			motion,
			depth,
			sharedMotion.resource,
			sharedDepth.resource);
		if (!alphaConditioned) {
			if (!temporal::WriteFrameGenerationInput(
					[]() {
						return render::TemporalPipeline::Get()
							.AcquireFrameGenerationInputWrite();
					},
					[&]() {
						cs::render::annotation::ScopedEvent annotationScope(
							"FG_CaptureDepthMotion");
						cs::engine::ComputeOMScope scope(context, 4, 0, 2, 1);
						const FrameGenerationCopyCB dimensions{
							renderWidth,
							renderHeight,
							capture.width,
							capture.height,
							0,
							0,
							0,
							0
						};
						_frameGenerationCopyCB->Update(dimensions);
						ID3D11Buffer* constantBuffer =
							_frameGenerationCopyCB->CB();
						context->CSSetConstantBuffers(
							0, 1, &constantBuffer);
						ID3D11ShaderResourceView* srvs[] = {
							depthSRV, motionSRV, nullptr, nullptr
						};
						context->CSSetShaderResources(
							0,
							static_cast<UINT>(std::size(srvs)),
							srvs);
						ID3D11UnorderedAccessView* uavs[] = {
							sharedDepth.uav,
							sharedMotion.uav
						};
						context->CSSetUnorderedAccessViews(
							0,
							static_cast<UINT>(std::size(uavs)),
							uavs,
							nullptr);
						context->CSSetShader(
							_copyDepthForFrameGenerationCS.get(),
							nullptr,
							0);
						context->Dispatch(
							(capture.width + 7) / 8,
							(capture.height + 7) / 8,
							1);
					})) {
				render::TemporalPipeline::Get().FailFrameGenerationFrame(
					"frame-generation shared inputs were still in provider use");
				return;
			}
		}
		render::TemporalPipeline::Get().RecordFrameGenerationCapture(
			alphaConditioned);
		CaptureFrameGenerationInputDebugSnapshot();

		_frameGenerationInputsCaptured = true;
		_hudlessCapturePending = true;
	}

	void TemporalRenderer::CaptureHUDLessColor()
	{
		const auto capture =
			render::TemporalPipeline::Get()
				.GetFrameGenerationCaptureResources();
		if (!_hudlessCapturePending || !_frameGenerationInputsCaptured ||
			!capture.ready) {
			return;
		}
		_hudlessCapturePending = false;
		render::TemporalPipeline::Get().SetHudlessCapturePending(false);

		auto* context = cs::engine::GetImmediateContext();
		auto* frameBufferRTV =
			cs::engine::GetRenderTargetRTV(cs::engine::RenderTarget::kFrameBuffer);
		const auto& hudless = capture.hudlessColor;
		if (!context || !frameBufferRTV || !hudless.resource) {
			render::TemporalPipeline::Get().SetFrameGenerationInputsReady(false);
			return;
		}

		winrt::com_ptr<ID3D11Resource> frameBufferResource;
		frameBufferRTV->GetResource(frameBufferResource.put());
		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		if (!frameBufferResource ||
			FAILED(frameBufferResource->QueryInterface(IID_PPV_ARGS(frameBuffer.put())))) {
			render::TemporalPipeline::Get().SetFrameGenerationInputsReady(false);
			return;
		}

		D3D11_TEXTURE2D_DESC sourceDesc{};
		D3D11_TEXTURE2D_DESC targetDesc{};
		frameBuffer->GetDesc(&sourceDesc);
		hudless.resource->GetDesc(&targetDesc);
		if (sourceDesc.Width != targetDesc.Width || sourceDesc.Height != targetDesc.Height ||
			sourceDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
			targetDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			render::TemporalPipeline::Get().FailFrameGenerationFrame(
				"pre-UI RT0 is incompatible with SDR HUDLessColor");
			return;
		}
		if (!temporal::WriteFrameGenerationInput(
				[]() {
					return render::TemporalPipeline::Get()
						.AcquireFrameGenerationInputWrite();
				},
				[&]() {
					cs::render::annotation::ScopedEvent annotationScope(
						"FG_CaptureHUDLess_RT0_PostImagespace");
					cs::engine::CopyResourcePreservingOM(
						context,
						hudless.resource,
						frameBuffer.get());
				})) {
			render::TemporalPipeline::Get().FailFrameGenerationFrame(
				"frame-generation HUD-less input was still in provider use");
			return;
		}
		CaptureFrameGenerationHudlessDebugSnapshot();
		render::TemporalPipeline::Get().SetFrameGenerationInputsReady(true);
		const auto [renderWidth, renderHeight] = GetRenderSize();
		const bool transactionReady =
			render::TemporalPipeline::Get().RecordInputPacket(
				GetEngineFrame(),
				{ renderWidth, renderHeight },
				{ capture.width, capture.height },
				capture.frameSlot);
		if (!transactionReady) {
			render::TemporalPipeline::Get().SetFrameGenerationInputsReady(false);
			RecordFrameGenerationFailure();
			return;
		}
		render::TemporalPipeline::Get().RecordFrameGenerationDispatch();
	}

	void TemporalRenderer::ClearFrameGenerationCaptureState() noexcept
	{
		_frameGenerationInputsCaptured = false;
		_hudlessCapturePending = false;
		render::TemporalPipeline::Get()
			.ResetFrameGenerationCaptureDiagnostics();
		render::TemporalPipeline::Get().SetFrameGenerationInputsReady(false);
		InvalidateFirstPersonAlphaState();
	}

	void TemporalRenderer::RecordFrameGenerationFailure() noexcept
	{
		render::TemporalPipeline::Get().RecordFrameGenerationFailure();
	}
}
