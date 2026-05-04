#include "Streamline.h"

#include <magic_enum/magic_enum.hpp>

#include "Log.h"
#include "Util.h"

namespace cs::features::Upscaling
{
	namespace { auto* L = cs::log::Get("cs.feature.upscaling.streamline"); }

void Streamline::LoadInterposer()
{
	// Check if the FrameGeneration plugin already loaded an interposer
	interposer = GetModuleHandleW(L"sl.interposer.dll");
	if (interposer) {
		alreadyInitialized = true;
		L->info("Interposer already loaded by FrameGeneration plugin at {0:p}", static_cast<void*>(interposer));
		return;
	}

	interposer = LoadLibraryW(L"Data/F4SE/Plugins/Streamline/sl.interposer.dll");
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		L->info("Failed to load interposer: Error Code {0:x}", errorCode);
	} else {
		L->info("Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}
}

void Streamline::Initialize()
{
	L->info("Initializing Streamline");

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS };
	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.logLevel = sl::LogLevel::eOff;
	pref.logMessageCallback = nullptr;
	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";
	pref.flags |= sl::PreferenceFlags::eUseManualHooking;

	pref.renderAPI = sl::RenderAPI::eD3D11;

	// Hook up all of the functions exported by the SL Interposer Library
	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slSetTag = (PFun_slSetTag2*)GetProcAddress(interposer, "slSetTag");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

	// Validate critical function pointers
	bool missingCritical = false;
	auto check = [&](const void* ptr, const char* name) {
		if (!ptr) {
			L->error("Failed to resolve: {}", name);
			missingCritical = true;
		}
	};
	check(slInit, "slInit");
	check(slShutdown, "slShutdown");
	check(slIsFeatureSupported, "slIsFeatureSupported");
	check(slIsFeatureLoaded, "slIsFeatureLoaded");
	check(slEvaluateFeature, "slEvaluateFeature");
	check(slSetTag, "slSetTag");
	check(slSetConstants, "slSetConstants");
	check(slGetNewFrameToken, "slGetNewFrameToken");
	check(slSetD3DDevice, "slSetD3DDevice");

	if (missingCritical) {
		initialized = false;
		return;
	}

	if (alreadyInitialized) {
		L->info("Skipping slInit — already initialized by FrameGeneration plugin");
		initialized = true;
	} else if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		L->critical("Failed to initialize Streamline");
	} else {
		initialized = true;
		L->info("Successfully initialized Streamline");
	}
}

void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
{
	L->info("Checking features");
	DXGI_ADAPTER_DESC adapterDesc;
	a_adapter->GetDesc(&adapterDesc);

	sl::AdapterInfo adapterInfo;
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	slIsFeatureLoaded(sl::kFeatureDLSS, featureDLSS);
	if (featureDLSS) {
		L->info("DLSS feature is loaded");
		featureDLSS = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk;
	}
	else {
		L->info("DLSS feature is not loaded");
		sl::FeatureRequirements featureRequirements;
		sl::Result result = slGetFeatureRequirements(sl::kFeatureDLSS, featureRequirements);
		if (result != sl::Result::eOk) {
			L->info("DLSS feature failed to load due to: {}", magic_enum::enum_name(result));
		}
	}

	L->info("DLSS {} available", featureDLSS ? "is" : "is not");
}

void Streamline::PostDevice()
{
	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}
}

void Streamline::Upscale(Texture2D* a_upscaleTexture, Texture2D* a_dilatedMotionVectorTexture, float2 a_jitter, float2 a_renderSize, uint a_qualityMode)
{
	UpdateConstants(a_jitter);

	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto& depthTexture = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain];

	static auto gameViewport = Util::State_GetSingleton();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	{
		sl::DLSSMode dlssMode;
		switch (a_qualityMode) {
		case 1:
			dlssMode = sl::DLSSMode::eMaxQuality;
			break;
		case 2:
			dlssMode = sl::DLSSMode::eBalanced;
			break;
		case 3:
			dlssMode = sl::DLSSMode::eMaxPerformance;
			break;
		case 4:
			dlssMode = sl::DLSSMode::eUltraPerformance;
			break;
		default:
			dlssMode = sl::DLSSMode::eDLAA;
			break;
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

		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource depth = { sl::ResourceType::eTex2d, reinterpret_cast<ID3D11Texture2D*>(depthTexture.texture), 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, a_dilatedMotionVectorTexture->resource.get(), 0};

		sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };
		sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &lowResExtent };
		sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &lowResExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag };
		slSetTag(viewport, resourceTags, _countof(resourceTags), context);
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
	auto evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);

	static bool evalLogged = false;
	if (!evalLogged) {
		L->info("slEvaluateFeature result: {}", (int)evalResult);
		evalLogged = true;
	}
}

void Streamline::UpdateConstants(float2 a_jitter)
{
	auto gameViewport = RE::BSGraphics::State::GetSingleton();

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
	slConstants.jitterOffset = { -a_jitter.x, -a_jitter.y};
	slConstants.mvecScale = { 1, 1 };  // TODO(#20): verify mvec space — FSR uses renderSize scale, DLSS uses {1,1}
	slConstants.prevClipToClip = {};
	slConstants.reset = sl::Boolean::eFalse;
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (SL_FAILED(res, slGetNewFrameToken(frameToken, nullptr))) {
		L->error("Could not get frame token");
	}

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, viewport))) {
		L->error("Could not set constants");
	}
}

void Streamline::DestroyDLSSResources()
{
	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	slDLSSSetOptions(viewport, dlssOptions);
	slFreeResources(sl::kFeatureDLSS, viewport);
}

}
