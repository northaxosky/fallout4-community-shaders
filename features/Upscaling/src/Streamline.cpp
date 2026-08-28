#include "Streamline.h"

#include <cfloat>
#include <dxgi.h>
#include <filesystem>
#include <optional>
#include <string>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Upscaling.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.streamline");

		constexpr UINT NVIDIA_VENDOR_ID = 0x10DE;

		void LoggingCallback(sl::LogType type, const char* msg)
		{
			std::string rawMsg(msg);
			while (!rawMsg.empty() && (rawMsg.back() == '\n' || rawMsg.back() == '\r'))
				rawMsg.pop_back();

			const char* p = msg;
			while (*p == '[') {
				const char* close = strchr(p, ']');
				if (!close)
					break;
				p = close + 1;
				while (*p == ' ' || *p == '\t') ++p;
			}
			std::string cleanMsg(p);
			size_t start = cleanMsg.find_first_not_of(" \t\r\n");
			size_t end = cleanMsg.find_last_not_of(" \t\r\n");
			if (start != std::string::npos && end != std::string::npos)
				cleanMsg = cleanMsg.substr(start, end - start + 1);
			else
				cleanMsg.clear();

			bool onlyBrackets = true;
			for (char c : cleanMsg) {
				if (c != '[' && c != ']' && c != ' ' && c != '\t') {
					onlyBrackets = false;
					break;
				}
			}
			if (cleanMsg.empty() || onlyBrackets) {
				L->info("[StreamlineSDK:RAW] {}", rawMsg);
				return;
			}

			switch (type) {
			case sl::LogType::eInfo:
				L->info("[StreamlineSDK] {}", cleanMsg);
				break;
			case sl::LogType::eWarn:
				L->warn("[StreamlineSDK] {}", cleanMsg);
				break;
			case sl::LogType::eError:
				L->error("[StreamlineSDK] {}", cleanMsg);
				break;
			}
		}
	}

	void Streamline::LoadInterposer()
	{
		triedInitialization = true;

		std::wstring interposerPath = std::wstring(Streamline::PluginDir) + L"\\sl.interposer.dll";
		interposer = LoadLibraryW(interposerPath.c_str());
		if (interposer == nullptr) {
			DWORD errorCode = GetLastError();
			L->info("Failed to load interposer: Error Code {0:x}", errorCode);
			return;
		}
		L->info("Interposer loaded at address: {0:p}", static_cast<void*>(interposer));

		L->info("Initializing Streamline");

		sl::Preferences pref;

		sl::Feature featuresToLoad[] = { sl::kFeatureDLSS };

		pref.featuresToLoad = featuresToLoad;
		pref.numFeaturesToLoad = _countof(featuresToLoad);

		switch (Upscaling::GetSingleton()->settings.streamlineLogLevel) {
		case 2:
			pref.logLevel = sl::LogLevel::eVerbose;
			break;
		case 1:
			pref.logLevel = sl::LogLevel::eDefault;
			break;
		case 0:
		default:
			pref.logLevel = sl::LogLevel::eOff;
			break;
		}
		pref.logMessageCallback = LoggingCallback;
		pref.showConsole = false;
		std::error_code pluginPathError;
		auto pluginDirAbsolute = std::filesystem::absolute(std::filesystem::path(Streamline::PluginDir), pluginPathError);
		if (pluginPathError)
			pluginDirAbsolute = std::filesystem::path(Streamline::PluginDir);
		static std::wstring pluginDirAbsoluteW;
		pluginDirAbsoluteW = pluginDirAbsolute.wstring();
		static const wchar_t* pluginPaths[1]{};
		pluginPaths[0] = pluginDirAbsoluteW.c_str();
		pref.pathsToPlugins = pluginPaths;
		pref.numPathsToPlugins = 1;
		L->info("Plugin search path: {}", pluginDirAbsolute.string());

		pref.engine = sl::EngineType::eCustom;
		pref.engineVersion = "1.0.0";
		pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

		pref.renderAPI = sl::RenderAPI::eD3D11;
		pref.flags = sl::PreferenceFlags::eUseManualHooking;

		slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
		slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
		slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
		slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
		slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
#pragma warning(push)
#pragma warning(disable: 4996)
		slSetTag = (PFun_slSetTag*)GetProcAddress(interposer, "slSetTag");
#pragma warning(pop)
		slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
		slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
		slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
		slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
		slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
		slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

		if (!slInit) {
			L->error("Interposer is missing slInit");
			return;
		}

		if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
			L->critical("Failed to initialize Streamline: {}", magic_enum::enum_name(res));
		} else {
			initialized = true;
			featureDLSS = false;
			deviceRegistered = false;
			L->info("Successfully initialized Streamline");
		}
	}

	void Streamline::UpgradeInterfaces(ID3D11Device** a_device, IDXGISwapChain** a_swapChain)
	{
		if (!initialized || !slUpgradeInterface) {
			return;
		}

		if (a_device && *a_device) {
			if (SL_FAILED(result, slUpgradeInterface(reinterpret_cast<void**>(a_device)))) {
				L->warn("Failed to upgrade the D3D11 device interface: {}", magic_enum::enum_name(result));
			} else {
				L->info("Upgraded the D3D11 device interface");
			}
		}

		if (a_swapChain && *a_swapChain) {
			if (SL_FAILED(result, slUpgradeInterface(reinterpret_cast<void**>(a_swapChain)))) {
				L->warn("Failed to upgrade the swap-chain interface: {}", magic_enum::enum_name(result));
			} else {
				L->info("Upgraded the swap-chain interface");
			}
		}
	}

	bool Streamline::SetDevice(ID3D11Device* a_device)
	{
		if (!initialized || !slSetD3DDevice || !a_device) {
			return false;
		}
		if (SL_FAILED(result, slSetD3DDevice(a_device))) {
			L->error("Failed to register the D3D11 device with Streamline: {}", magic_enum::enum_name(result));
			return false;
		}
		deviceRegistered = true;
		return true;
	}

	void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
	{
		if (!initialized || !deviceRegistered || !a_adapter)
			return;

		L->info("Checking features");
		DXGI_ADAPTER_DESC adapterDesc;
		a_adapter->GetDesc(&adapterDesc);

		sl::AdapterInfo adapterInfo;
		adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
		adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

		auto checkFeatureAvailability = [&](sl::Feature feature, const char* featureName, bool& outAvailable) {
			outAvailable = false;
			bool loaded = false;
			if (SL_FAILED(result, slIsFeatureLoaded(feature, loaded))) {
				L->warn("{} load-state query failed: {}", featureName, magic_enum::enum_name(result));
				return;
			}
			if (!loaded) {
				L->info("{} feature is not loaded", featureName);
				sl::FeatureRequirements featureRequirements;
				sl::Result requirementsResult = slGetFeatureRequirements(feature, featureRequirements);
				if (requirementsResult != sl::Result::eOk) {
					L->info("{} feature failed to load due to: {}", featureName, magic_enum::enum_name(requirementsResult));
				}
				return;
			}

			L->info("{} feature is loaded", featureName);
			outAvailable = slIsFeatureSupported(feature, adapterInfo) == sl::Result::eOk;
		};

		checkFeatureAvailability(sl::kFeatureDLSS, "DLSS", featureDLSS);

		if (featureDLSS) {
			isRTXBelow40series = IsRTXAndBelow40Series(a_adapter);

			if (isRTXBelow40series)
				L->info("Older RTX GPU detected, DLSS 4.0 will be used instead of DLSS 4.5");
			else
				L->info("Newer RTX GPU detected, DLSS 4.5 will be used instead of DLSS 4.0");
		}

		L->info("DLSS {} available", featureDLSS ? "is" : "is not");
	}

	void Streamline::PostDevice()
	{
		if (!initialized || !deviceRegistered || !slGetFeatureFunction)
			return;

		if (featureDLSS) {
			slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
		}
	}

	bool Streamline::EnsureFrameToken()
	{
		auto* graphicsState = cs::engine::GetGraphicsState();
		if (!initialized || !slGetNewFrameToken || !graphicsState)
			return false;

		if (_lastFrameToken == graphicsState->frameCount)
			return frameToken != nullptr;
		_lastFrameToken = graphicsState->frameCount;

		if (SL_FAILED(result, slGetNewFrameToken(frameToken, &graphicsState->frameCount))) {
			L->error("Could not get frame token: {}", magic_enum::enum_name(result));
			frameToken = nullptr;
			return false;
		}

		return frameToken != nullptr;
	}

	bool Streamline::CheckFrameConstants(sl::ViewportHandle p_viewport)
	{
		if (!initialized || !deviceRegistered || !slSetConstants)
			return false;

		if (!EnsureFrameToken())
			return false;

		sl::Constants slConstants = {};

		slConstants.cameraNear = 0.0f;
		slConstants.cameraFar = 1.0f;
		slConstants.cameraAspectRatio = 0.0f;
		slConstants.cameraFOV = 0.0f;
		slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
		slConstants.cameraPinholeOffset = { 0.f, 0.f };
		slConstants.cameraPos = {};
		slConstants.cameraRight = {};
		slConstants.cameraUp = {};
		slConstants.cameraFwd = {};
		slConstants.cameraViewToClip = {};
		slConstants.clipToCameraView = {};
		slConstants.clipToPrevClip = {};
		slConstants.prevClipToClip = {};
		slConstants.depthInverted = sl::Boolean::eFalse;

		auto jitter = Upscaling::GetSingleton()->jitter;
		slConstants.jitterOffset = { -jitter.x, -jitter.y };
		slConstants.reset = sl::Boolean::eFalse;

		slConstants.mvecScale = { 1.0f, 1.0f };
		slConstants.motionVectors3D = sl::Boolean::eFalse;
		slConstants.motionVectorsInvalidValue = FLT_MIN;
		slConstants.orthographicProjection = sl::Boolean::eFalse;
		slConstants.motionVectorsDilated = sl::Boolean::eFalse;
		slConstants.motionVectorsJittered = sl::Boolean::eFalse;

		if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, p_viewport))) {
			L->error("Could not set constants: {}", magic_enum::enum_name(res));
			return false;
		}

		return true;
	}

	bool Streamline::IsRTXAndBelow40Series(IDXGIAdapter* a_adapter)
	{
		DXGI_ADAPTER_DESC adapterDesc = {};

		a_adapter->GetDesc(&adapterDesc);

		UINT vendorId = adapterDesc.VendorId;
		UINT deviceId = adapterDesc.DeviceId;

		if (vendorId != NVIDIA_VENDOR_ID)
			return false;

		if (deviceId >= 0x2200 && deviceId <= 0x2600)
			return true;

		if (deviceId >= 0x1E00 && deviceId <= 0x1FFF)
			return true;

		return false;
	}

	bool Streamline::SetDLSSOptions(sl::ViewportHandle p_viewport, std::uint32_t width)
	{
		auto* upscaling = Upscaling::GetSingleton();
		auto* graphicsState = cs::engine::GetGraphicsState();
		if (!slDLSSSetOptions || !graphicsState)
			return false;

		sl::DLSSOptions dlssOptions{};

		std::uint32_t qualityMode = upscaling->settings.qualityMode;
		switch (qualityMode) {
		case 1:
			dlssOptions.mode = sl::DLSSMode::eMaxQuality;
			break;
		case 2:
			dlssOptions.mode = sl::DLSSMode::eBalanced;
			break;
		case 3:
			dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
			break;
		case 4:
			dlssOptions.mode = sl::DLSSMode::eUltraPerformance;
			break;
		default:
			dlssOptions.mode = sl::DLSSMode::eDLAA;
			break;
		}

		dlssOptions.outputWidth = width;
		dlssOptions.outputHeight = graphicsState->screenHeight;
		dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;
		dlssOptions.useAutoExposure = sl::Boolean::eTrue;

		std::optional<sl::DLSSPreset> customPreset;
		switch (upscaling->settings.presetDLSS) {
		case 1:
			customPreset = sl::DLSSPreset::ePresetJ;
			break;
		case 2:
			customPreset = sl::DLSSPreset::ePresetK;
			break;
		case 3:
			customPreset = sl::DLSSPreset::ePresetL;
			break;
		case 4:
			customPreset = sl::DLSSPreset::ePresetM;
			break;
		}

		if (customPreset.has_value()) {
			dlssOptions.dlaaPreset = customPreset.value();
			dlssOptions.ultraQualityPreset = customPreset.value();
			dlssOptions.qualityPreset = customPreset.value();
			dlssOptions.balancedPreset = customPreset.value();
			dlssOptions.performancePreset = customPreset.value();
			dlssOptions.ultraPerformancePreset = customPreset.value();
		} else if (isRTXBelow40series) {
			dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetJ;
			dlssOptions.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
			dlssOptions.qualityPreset = sl::DLSSPreset::ePresetJ;
			dlssOptions.balancedPreset = sl::DLSSPreset::ePresetJ;
			dlssOptions.performancePreset = sl::DLSSPreset::ePresetJ;
			dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetM;
		} else {
			dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetJ;
			dlssOptions.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
			dlssOptions.qualityPreset = sl::DLSSPreset::ePresetK;
			dlssOptions.balancedPreset = sl::DLSSPreset::ePresetK;
			dlssOptions.performancePreset = sl::DLSSPreset::ePresetM;
			dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
		}

		dlssOptions.preExposure = 1.0f;
#pragma warning(push)
#pragma warning(disable: 4996)
		dlssOptions.sharpness = 0.0f;
#pragma warning(pop)

		if (SL_FAILED(result, slDLSSSetOptions(p_viewport, dlssOptions))) {
			L->critical("Could not enable DLSS: {}", magic_enum::enum_name(result));
			return false;
		}
		return true;
	}

	void Streamline::EvaluateDLSS(sl::ViewportHandle vp,
		ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
		ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
		const sl::Extent& extentIn, const sl::Extent& extentOut, std::uint32_t outputWidth)
	{
		_evaluatedThisDispatch = false;

		auto* context = cs::engine::GetImmediateContext();
		if (!context || !slSetTag || !slEvaluateFeature)
			return;
		if (!deviceRegistered) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err,
				"DLSS evaluation skipped: the device was never registered with Streamline");
			return;
		}

		sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, 0 };
		sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, 0 };
		sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
		sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
		sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
		sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

		if (!CheckFrameConstants(vp))
			return;

		if (!SetDLSSOptions(vp, outputWidth))
			return;

		sl::ResourceTag tags[] = {
			{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentIn },
			{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentOut },
			{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
			{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
			{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
			{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn }
		};

#pragma warning(push)
#pragma warning(disable: 4996)
		const sl::Result tagResult = slSetTag(vp, tags, _countof(tags), context);
#pragma warning(pop)
		if (tagResult != sl::Result::eOk) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err,
				"slSetTag failed: {}", magic_enum::enum_name(tagResult));
			return;
		}

		sl::ViewportHandle view(vp);
		const sl::BaseStructure* inputs[] = { &view };

		sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);

		if (evalResult != sl::Result::eOk) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err,
				"slEvaluateFeature failed: {}", magic_enum::enum_name(evalResult));
			return;
		}

		_evaluatedThisDispatch = true;
	}

	bool Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors)
	{
		auto* graphicsState = cs::engine::GetGraphicsState();
		auto* depthTexture = cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		if (!graphicsState || !depthTexture || !a_upscalingTexture || !a_reactiveMask ||
			!a_transparencyCompositionMask || !a_motionVectors)
			return false;

		auto* upscaling = Upscaling::GetSingleton();
		if (!upscaling->sharpenerTexture ||
			upscaling->sharpenerTexture->resource.get() == a_upscalingTexture) {
			return false;
		}
		ID3D11Resource* colorOut = upscaling->sharpenerTexture->resource.get();

		const auto [renderWidth, renderHeight] = upscaling->GetRenderSize();
		if (renderWidth == 0 || renderHeight == 0)
			return false;
		sl::Extent extentIn{ 0, 0, renderWidth, renderHeight };
		sl::Extent extentOut{ 0, 0, graphicsState->screenWidth, graphicsState->screenHeight };

		EvaluateDLSS(viewport,
			a_upscalingTexture, colorOut,
			depthTexture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask,
			extentIn, extentOut, graphicsState->screenWidth);

		return _evaluatedThisDispatch;
	}

	void Streamline::DestroyDLSSResources()
	{
		if (!slDLSSSetOptions || !slFreeResources)
			return;

		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = sl::DLSSMode::eOff;

		slDLSSSetOptions(viewport, dlssOptions);
		slFreeResources(sl::kFeatureDLSS, viewport);
	}
}
