#include "Streamline.h"
#include "Upscaling.h"

#include "Log.h"

namespace cs::features::FrameGeneration
{
	namespace { auto* L = cs::log::Get("cs.feature.framegen.dlssg"); }

void StreamlineFG::LoadInterposer()
{
	interposer = LoadLibrary(L"Data\\F4SE\\Plugins\\Streamline\\sl.interposer.dll");
	if (!interposer) {
		L->warn("Failed to load interposer: {:#x}", GetLastError());
		return;
	}
	L->info("Interposer loaded");

	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slSetTagForFrame = (PFun_slSetTagForFrame2*)GetProcAddress(interposer, "slSetTagForFrame");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
}

bool StreamlineFG::InitStreamline()
{
	if (!interposer || !slInit) return false;

	L->info("Initializing Streamline");

	// Pre-load plugin DLLs for MO2 USVFS compatibility
	LoadLibrary(L"Data\\F4SE\\Plugins\\Streamline\\sl.common.dll");
	LoadLibrary(L"Data\\F4SE\\Plugins\\Streamline\\sl.dlss_g.dll");

	sl::Preferences pref{};
	pref.showConsole = false;
	auto debugLogging = Upscaling::GetSingleton()->settings.debugLogging;
	pref.logLevel = debugLogging ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
	if (debugLogging) {
		pref.logMessageCallback = [](sl::LogType, const char* msg) {
			cs::log::Get("cs.feature.framegen.dlssg.internal")->info("{}", msg);
		};
	}
	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f4f4f4f4f4f4f4f4f4f4f4f4f4f4f4f4";
	pref.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	pref.renderAPI = sl::RenderAPI::eD3D12;

	// Resolve real path where sl.interposer.dll lives (MO2 USVFS may virtualize it)
	static wchar_t interposerDir[MAX_PATH];
	GetModuleFileNameW(interposer, interposerDir, MAX_PATH);
	wchar_t* lastSlash = wcsrchr(interposerDir, L'\\');
	if (!lastSlash) lastSlash = wcsrchr(interposerDir, L'/');
	if (lastSlash) *lastSlash = L'\0';

	static const wchar_t* pluginPaths[] = { interposerDir };
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;

	static sl::Feature features[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
	pref.featuresToLoad = features;
	pref.numFeaturesToLoad = _countof(features);

	auto result = slInit(pref, sl::kSDKVersion);
	L->info("slInit result: {}", (int)result);

	if (result != sl::Result::eOk) {
		L->error("Streamline init failed");
		return false;
	}

	slInitialized = true;
	return true;
}

void StreamlineFG::SetD3DDevice(ID3D12Device* a_device)
{
	d3d12Device = a_device;
	if (slSetD3DDevice && slInitialized) {
		slSetD3DDevice(a_device);
		L->info("D3D12 device set");
	}
}

bool StreamlineFG::CheckAndEnableDLSSG()
{
	if (!slInitialized) return false;

	if (slGetFeatureFunction) {
		slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", (void*&)slDLSSGSetOptions);
		slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", (void*&)slDLSSGGetState);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetMarker", (void*&)slReflexSetMarker);
		L->info("Feature functions loaded: SetOptions={:#x}, GetState={:#x}, ReflexMarker={:#x}",
			(uintptr_t)slDLSSGSetOptions, (uintptr_t)slDLSSGGetState, (uintptr_t)slReflexSetMarker);
	}

	if (!slDLSSGSetOptions || !slReflexSetMarker) {
		L->warn("Missing required function pointers");
		return false;
	}

	// Query hardware capability for MFG
	uint32_t maxFrames = 1;
	if (slDLSSGGetState) {
		sl::DLSSGState state{};
		slDLSSGGetState(viewport, state, nullptr);
		maxFrames = state.numFramesToGenerateMax;
	}

	// Enable DLSS-G — called once at init, not per-frame
	// Clamp requested frames to hardware max (1 on RTX 40xx, up to 3 on RTX 50xx)
	auto upscaling = Upscaling::GetSingleton();
	uint32_t requestedFrames = std::clamp((uint32_t)upscaling->settings.frameGenFrames, 1u, maxFrames);

	configuredFrameCount = requestedFrames;

	sl::DLSSGOptions options{};
	options.mode = sl::DLSSGMode::eOn;
	options.numFramesToGenerate = requestedFrames;

	auto result = slDLSSGSetOptions(viewport, options);
	if (result != sl::Result::eOk) {
		L->warn("Failed to enable DLSS-G: {}", (int)result);
		return false;
	}

	featureDLSSG = true;
	L->info("DLSS-G enabled: {}x frame gen (requested {}, hardware max {})",
		requestedFrames + 1, upscaling->settings.frameGenFrames, maxFrames);

	// Reflex must be active when DLSS-G is on
	if (slReflexSetOptions) {
		sl::ReflexOptions reflexOptions{};
		reflexOptions.mode = sl::ReflexMode::eLowLatency;
		slReflexSetOptions(reflexOptions);
	}

	if (slDLSSGGetState) {
		sl::DLSSGState state{};
		slDLSSGGetState(viewport, state, nullptr);
		L->info("Status: {}, minDim: {}, maxFrames: {}",
			(int)state.status, state.minWidthOrHeight, state.numFramesToGenerateMax);
	}

	return true;
}

void StreamlineFG::SetEnabled(bool a_enabled)
{
	if (!slDLSSGSetOptions || !featureDLSSG) return;

	sl::DLSSGOptions options{};
	options.mode = a_enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
	options.numFramesToGenerate = configuredFrameCount;
	slDLSSGSetOptions(viewport, options);
}

static sl::float4x4 toSLMatrix(const __m128* mat)
{
	sl::float4x4 result;
	for (int i = 0; i < 4; i++) {
		alignas(16) float row[4];
		_mm_store_ps(row, mat[i]);
		result[i] = sl::float4(row[0], row[1], row[2], row[3]);
	}
	return result;
}

static sl::float3 toSLFloat3(const __m128* v)
{
	alignas(16) float vals[4];
	_mm_store_ps(vals, *v);
	return sl::float3(vals[0], vals[1], vals[2]);
}

void StreamlineFG::AcquireFrameToken()
{
	if (!slGetNewFrameToken || !featureDLSSG) return;

	if (SL_FAILED(res, slGetNewFrameToken(frameToken, nullptr))) {
		static bool loggedOnce = false;
		if (!loggedOnce) { L->error("Failed to get frame token"); loggedOnce = true; }
	}
}

void StreamlineFG::SetPCLMarker(sl::PCLMarker marker)
{
	if (!slReflexSetMarker || !frameToken) return;
	slReflexSetMarker(marker, *frameToken);
}

void StreamlineFG::Present(
	ID3D12GraphicsCommandList* a_cmdList,
	ID3D12Resource* a_depth,
	ID3D12Resource* a_motionVectors,
	ID3D12Resource* a_hudlessColor,
	ID3D12Resource* a_uiColorAlpha,
	float2 a_screenSize,
	float2 a_jitter,
	float a_cameraNear, float a_cameraFar,
	const CameraData& a_camera)
{
	if (!featureDLSSG || !frameToken) return;

	// Set per-frame constants — matrices MUST be unjittered per DLSS-G docs
	if (slSetConstants) {
		sl::Constants constants{};

		// Derive unjittered projection: inv(viewMat) * viewProjUnjittered
		sl::float4x4 viewMatrix = toSLMatrix(a_camera.viewMat);
		sl::float4x4 invView;
		sl::matrixFullInvert(invView, viewMatrix);
		sl::float4x4 vpUnjittered = toSLMatrix(a_camera.viewProjUnjittered);
		sl::matrixMul(constants.cameraViewToClip, invView, vpUnjittered);
		sl::matrixFullInvert(constants.clipToCameraView, constants.cameraViewToClip);

		sl::float4x4 currentVP = toSLMatrix(a_camera.currentViewProjUnjittered);
		sl::float4x4 previousVP = toSLMatrix(a_camera.previousViewProjUnjittered);
		sl::float4x4 invCurrentVP;
		sl::matrixFullInvert(invCurrentVP, currentVP);
		sl::matrixMul(constants.clipToPrevClip, invCurrentVP, previousVP);
		sl::matrixFullInvert(constants.prevClipToClip, constants.clipToPrevClip);

		constants.cameraPos = sl::float3(a_camera.posX, a_camera.posY, a_camera.posZ);
		constants.cameraUp = toSLFloat3(a_camera.viewUp);
		constants.cameraRight = toSLFloat3(a_camera.viewRight);
		constants.cameraFwd = toSLFloat3(a_camera.viewDir);
		constants.cameraNear = a_cameraNear;
		constants.cameraFar = a_cameraFar;
		constants.cameraAspectRatio = a_screenSize.x / a_screenSize.y;
		// Extract vertical FOV from unjittered projection matrix
		constants.cameraFOV = 2.0f * std::atan(1.0f / constants.cameraViewToClip[1].y);
		constants.cameraMotionIncluded = sl::Boolean::eTrue;
		constants.cameraPinholeOffset = { 0.f, 0.f };
		constants.depthInverted = sl::Boolean::eTrue;
		constants.jitterOffset = { -a_jitter.x, -a_jitter.y };
		constants.mvecScale = { 1.0f, 1.0f };
		constants.reset = sl::Boolean::eFalse;
		constants.motionVectors3D = sl::Boolean::eFalse;
		constants.orthographicProjection = sl::Boolean::eFalse;
		constants.motionVectorsDilated = sl::Boolean::eFalse;
		constants.motionVectorsJittered = sl::Boolean::eFalse;

		if (SL_FAILED(res, slSetConstants(constants, *frameToken, viewport))) {
			static bool loggedOnce = false;
			if (!loggedOnce) { L->error("Failed to set constants"); loggedOnce = true; }
		}
	}

	// Tag D3D12 resources
	if (a_depth && a_motionVectors && a_hudlessColor && slSetTagForFrame) {
		sl::Resource depth = { sl::ResourceType::eTex2d, a_depth, 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, a_motionVectors, 0 };
		sl::Resource hudless = { sl::ResourceType::eTex2d, a_hudlessColor, 0 };

		// Explicit extent matching screen size
		sl::Extent fullExtent = { 0, 0, (uint32_t)a_screenSize.x, (uint32_t)a_screenSize.y };

		if (a_uiColorAlpha) {
			sl::Resource uiColor = { sl::ResourceType::eTex2d, a_uiColorAlpha, 0 };

			sl::ResourceTag tags[] = {
				{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
				{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
				{ &hudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
				{ &uiColor, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
			};
			slSetTagForFrame(*frameToken, viewport, tags, _countof(tags), (sl::CommandBuffer*)a_cmdList);
		} else {
			sl::ResourceTag tags[] = {
				{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
				{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
				{ &hudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
				{ nullptr, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, nullptr },
			};
			slSetTagForFrame(*frameToken, viewport, tags, _countof(tags), (sl::CommandBuffer*)a_cmdList);
		}
	}
}

void StreamlineFG::Shutdown()
{
	if (slInitialized && slShutdown) {
		slShutdown();
		slInitialized = false;
	}
}

}
