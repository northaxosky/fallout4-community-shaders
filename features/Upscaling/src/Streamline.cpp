#include "Streamline.h"

#include "Render/StreamlineCore.h"
#include "Render/Engine.h"
#include "Render/CameraConstants.h"
#include "Log.h"
#include "Upscaling.h"

namespace cs::features::upscaling
{
	namespace { auto* L = cs::log::Get("cs.feature.upscaling.streamline"); }

	namespace
	{
		// Curated DLSS model presets; K/L/M are the current transformer-based defaults.
		sl::DLSSPreset MapDLSSPreset(uint a_idx)
		{
			switch (a_idx) {
			case 1: return sl::DLSSPreset::ePresetJ;
			case 2: return sl::DLSSPreset::ePresetK;
			case 3: return sl::DLSSPreset::ePresetL;
			case 4: return sl::DLSSPreset::ePresetM;
			default: return sl::DLSSPreset::eDefault;
			}
		}
	}

void Streamline::CacheDLSSFunctions()
{
	auto* core = cs::Streamline::GetSingleton();
	if (!core->featureDLSS || !core->slGetFeatureFunction) {
		L->info("Skipping DLSS function cache (featureDLSS={}, slGetFeatureFunction={})",
			core->featureDLSS, (void*)core->slGetFeatureFunction);
		return;
	}
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
	auto& depthTexture = rendererData->depthStencilTargets[(uint)cs::engine::DepthStencilTarget::kMain];

	static auto gameViewport = cs::engine::GetGraphicsState();
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
		// FO4's post-upscale color buffer is a float HDR format; detect it rather than assume SDR.
		D3D11_TEXTURE2D_DESC colorDesc{};
		a_upscaleTexture->resource->GetDesc(&colorDesc);
		const bool colorHDR =
			colorDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
			colorDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
			colorDesc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT;
		dlssOptions.colorBuffersHDR = colorHDR ? sl::Boolean::eTrue : sl::Boolean::eFalse;

		const sl::DLSSPreset dlssPreset = MapDLSSPreset(Upscaling::GetSingleton()->settings.presetDLSS);
		dlssOptions.dlaaPreset = dlssPreset;
		dlssOptions.qualityPreset = dlssPreset;
		dlssOptions.balancedPreset = dlssPreset;
		dlssOptions.performancePreset = dlssPreset;
		dlssOptions.ultraPerformancePreset = dlssPreset;

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

	auto* gameViewport = cs::engine::GetGraphicsState();
	if (!gameViewport)
		return;
	const auto& camView = gameViewport->cameraState.camViewData;
	const auto& camState = gameViewport->cameraState;

	cs::engine::CameraConstants cam{};
	cam.viewMat = camView.viewMat;
	cam.viewProjUnjittered = camView.viewProjUnjittered;
	cam.currentViewProjUnjittered = camView.currentViewProjUnjittered;
	cam.previousViewProjUnjittered = camView.previousViewProjUnjittered;
	cam.viewUp = &camView.viewUp;
	cam.viewRight = &camView.viewRight;
	cam.viewDir = &camView.viewDir;
	cam.posX = camState.posAdjust.x;
	cam.posY = camState.posAdjust.y;
	cam.posZ = camState.posAdjust.z;

	// First dispatch has undefined temporal history; reset once to avoid a startup ghost.
	static bool firstConstantsFrame = true;
	const sl::Boolean resetHistory = firstConstantsFrame ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	firstConstantsFrame = false;

	sl::Constants slConstants = cs::engine::BuildSLConstants(
		cam, gameViewport->screenWidth, gameViewport->screenHeight,
		cs::engine::GetCameraNear(), cs::engine::GetCameraFar(),
		a_jitter.x, a_jitter.y, resetHistory);

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
