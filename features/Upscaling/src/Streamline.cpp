#include "Streamline.h"

#include "StreamlineCore.h"
#include "Log.h"
#include "Util.h"

namespace cs::features::Upscaling
{
	namespace { auto* L = cs::log::Get("cs.feature.upscaling.streamline"); }

void Streamline::CacheDLSSFunctions()
{
	auto* core = cs::Streamline::GetSingleton();
	if (!core->featureDLSS || !core->slGetFeatureFunction) {
		L->info("Skipping DLSS function cache (featureDLSS={}, slGetFeatureFunction={})",
			core->featureDLSS, (void*)core->slGetFeatureFunction);
		return;
	}
	core->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
	core->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState",          (void*&)slDLSSGetState);
	core->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions",        (void*&)slDLSSSetOptions);
	L->info("DLSS entry points cached");
}

void Streamline::Upscale(Texture2D* a_upscaleTexture, Texture2D* a_dilatedMotionVectorTexture, float2 a_jitter, float2 a_renderSize, uint a_qualityMode)
{
	auto* core = cs::Streamline::GetSingleton();
	if (!core->IsInitialized() || !slDLSSSetOptions || !core->slSetTag || !core->slEvaluateFeature)
		return;

	UpdateConstants(a_jitter);

	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto& depthTexture = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain];

	static auto gameViewport = Util::State_GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	{
		sl::DLSSMode dlssMode;
		switch (a_qualityMode) {
		case 1: dlssMode = sl::DLSSMode::eMaxQuality;      break;
		case 2: dlssMode = sl::DLSSMode::eBalanced;        break;
		case 3: dlssMode = sl::DLSSMode::eMaxPerformance;  break;
		case 4: dlssMode = sl::DLSSMode::eUltraPerformance;break;
		default: dlssMode = sl::DLSSMode::eDLAA;           break;
		}

		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = dlssMode;
		dlssOptions.outputWidth = gameViewport->screenWidth;
		dlssOptions.outputHeight = gameViewport->screenHeight;
		dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;

		if (SL_FAILED(result, slDLSSSetOptions(viewport, dlssOptions))) {
			L->critical("Could not enable DLSS");
		}
	}

	{
		sl::Extent lowResExtent{ 0, 0, (uint)a_renderSize.x, (uint)a_renderSize.y };
		sl::Extent fullExtent{ 0, 0, gameViewport->screenWidth, gameViewport->screenHeight };

		sl::Resource colorIn  = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource depth    = { sl::ResourceType::eTex2d, reinterpret_cast<ID3D11Texture2D*>(depthTexture.texture), 0 };
		sl::Resource mvec     = { sl::ResourceType::eTex2d, a_dilatedMotionVectorTexture->resource.get(), 0 };

		sl::ResourceTag colorInTag  = sl::ResourceTag{ &colorIn,  sl::kBufferTypeScalingInputColor,  sl::ResourceLifecycle::eOnlyValidNow,    &lowResExtent };
		sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow,    &fullExtent  };
		sl::ResourceTag depthTag    = sl::ResourceTag{ &depth,    sl::kBufferTypeDepth,              sl::ResourceLifecycle::eValidUntilPresent,&lowResExtent };
		sl::ResourceTag mvecTag     = sl::ResourceTag{ &mvec,     sl::kBufferTypeMotionVectors,      sl::ResourceLifecycle::eValidUntilPresent,&lowResExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag };
		core->slSetTag(viewport, resourceTags, _countof(resourceTags), context);
	}

	static bool loggedOnce = false;
	if (!loggedOnce) {
		L->info("First DLSS dispatch: renderSize={}x{}, outputSize={}x{}, mode={}, jitter=({}, {})",
			(uint)a_renderSize.x, (uint)a_renderSize.y,
			gameViewport->screenWidth, gameViewport->screenHeight,
			a_qualityMode, a_jitter.x, a_jitter.y);
		loggedOnce = true;
	}

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };
	auto evalResult = core->slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);

	static bool evalLogged = false;
	if (!evalLogged) {
		L->info("slEvaluateFeature result: {}", (int)evalResult);
		evalLogged = true;
	}
}

void Streamline::UpdateConstants(float2 a_jitter)
{
	auto* core = cs::Streamline::GetSingleton();
	if (!core->slGetNewFrameToken || !core->slSetConstants)
		return;

	sl::Constants slConstants = {};
	slConstants.cameraNear = 0;
	slConstants.cameraFar = 1;
	slConstants.cameraAspectRatio = 0.0f;
	slConstants.cameraFOV = 0.0f;
	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraPos = {};
	slConstants.cameraFwd = {};
	slConstants.cameraUp = {};
	slConstants.cameraRight = {};
	slConstants.cameraViewToClip = {};
	slConstants.clipToCameraView = {};
	slConstants.clipToPrevClip = {};
	slConstants.depthInverted = sl::Boolean::eFalse;
	slConstants.jitterOffset = { -a_jitter.x, -a_jitter.y };
	slConstants.mvecScale = { 1, 1 };
	slConstants.prevClipToClip = {};
	slConstants.reset = sl::Boolean::eFalse;
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (SL_FAILED(res, core->slGetNewFrameToken(frameToken, nullptr))) {
		L->error("Could not get frame token");
	}
	if (SL_FAILED(res, core->slSetConstants(slConstants, *frameToken, viewport))) {
		L->error("Could not set constants");
	}
}

void Streamline::DestroyDLSSResources()
{
	auto* core = cs::Streamline::GetSingleton();
	if (slDLSSSetOptions) {
		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = sl::DLSSMode::eOff;
		slDLSSSetOptions(viewport, dlssOptions);
	}
	if (core->slFreeResources)
		core->slFreeResources(sl::kFeatureDLSS, viewport);
}

}
