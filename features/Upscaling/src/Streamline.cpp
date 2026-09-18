#include "Streamline.h"

#include <algorithm>
#include <cfloat>
#include <dxgi.h>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "StreamlineFrameGenerationContract.h"
#include "Utils/StreamlineModule.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.streamline");

		std::optional<sl::DLSSMode> ToDLSSMode(
			std::uint32_t a_qualityMode) noexcept
		{
			switch (a_qualityMode) {
			case 0:
				return sl::DLSSMode::eDLAA;
			case 1:
				return sl::DLSSMode::eMaxQuality;
			case 2:
				return sl::DLSSMode::eBalanced;
			case 3:
				return sl::DLSSMode::eMaxPerformance;
			case 4:
				return sl::DLSSMode::eUltraPerformance;
			default:
				return std::nullopt;
			}
		}

		std::optional<sl::FSRMode> ToFSRMode(
			std::uint32_t a_qualityMode) noexcept
		{
			switch (a_qualityMode) {
			case 0:
				return sl::FSRMode::eNativeAA;
			case 1:
				return sl::FSRMode::eQuality;
			case 2:
				return sl::FSRMode::eBalanced;
			case 3:
				return sl::FSRMode::ePerformance;
			case 4:
				return sl::FSRMode::eUltraPerformance;
			default:
				return std::nullopt;
			}
		}

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

	void Streamline::LoadInterposer(
		std::uint32_t a_logLevel,
		bool a_loadDlss,
		bool a_loadFsr,
		bool a_loadDlssG,
		bool a_loadFsrG)
	{
		triedInitialization = true;
		_latencyFeaturesRequested = a_loadDlssG;
		_fsrFeatureRequested = a_loadFsr;
		_fsrGFeatureRequested = a_loadFsrG;

		const auto loaded = cs::files::LoadStreamlineInterposer(Streamline::PluginDir);
		if (!loaded) {
			L->error(
				"Streamline authentication/load rejected: {} (trust {}, Windows {:#010x}).",
				sl::security::getTrustFailureMessage(loaded.failure),
				static_cast<std::uint32_t>(loaded.failure),
				loaded.systemError);
			return;
		}
		interposer = loaded.module;
		L->info("Interposer loaded at address: {0:p}", static_cast<void*>(interposer));

		L->info("Initializing Streamline");

		sl::Preferences pref;

		sl::Feature featuresToLoad[6]{};
		std::uint32_t featureCount = 0;
		if (a_loadDlss) {
			featuresToLoad[featureCount++] = sl::kFeatureDLSS;
		}
		if (a_loadFsr) {
			featuresToLoad[featureCount++] = sl::kFeatureFSR;
		}
		if (a_loadDlssG) {
			featuresToLoad[featureCount++] = sl::kFeatureDLSS_G;
			featuresToLoad[featureCount++] = sl::kFeaturePCL;
			featuresToLoad[featureCount++] = sl::kFeatureReflex;
		}
		if (a_loadFsrG) {
			featuresToLoad[featureCount++] = sl::kFeatureFSR_G;
		}

		pref.featuresToLoad = featuresToLoad;
		pref.numFeaturesToLoad = featureCount;

		switch (a_logLevel) {
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

		pref.renderAPI = sl::RenderAPI::eD3D12;
		pref.flags = sl::PreferenceFlags::eUseManualHooking |
			sl::PreferenceFlags::eUseFrameBasedResourceTagging;

		slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
		slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
		slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
		slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
		slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
		slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
		slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
		slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
		slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
		slSetTagForFrame =
			(PFun_slSetTagForFrame*)GetProcAddress(interposer, "slSetTagForFrame");
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
			featureFSR = false;
			deviceRegistered = false;
			L->info("Successfully initialized Streamline");
		}
	}

	bool Streamline::PrepareD3D12Device(ID3D12Device** a_device)
	{
		if (!initialized || !slUpgradeInterface || !a_device || !*a_device) {
			return false;
		}
		if (SL_FAILED(result, slUpgradeInterface(
				reinterpret_cast<void**>(a_device)))) {
			L->error(
				"Failed to upgrade the D3D12 device: {}",
				magic_enum::enum_name(result));
			return false;
		}
		return SetDevice(*a_device);
	}

	bool Streamline::PrepareDXGIFactory(IDXGIFactory4** a_factory)
	{
		if (!initialized || !slUpgradeInterface || !a_factory || !*a_factory) {
			return false;
		}
		if (SL_FAILED(result, slUpgradeInterface(
			reinterpret_cast<void**>(a_factory)))) {
			L->error(
				"Failed to upgrade the DXGI factory: {}",
				magic_enum::enum_name(result));
			return false;
		}
		return true;
	}

	bool Streamline::SetDevice(ID3D12Device* a_device)
	{
		if (!initialized || !slSetD3DDevice || !a_device) {
			return false;
		}
		if (SL_FAILED(result, slSetD3DDevice(a_device))) {
			L->error(
				"Failed to register the D3D12 device with Streamline: {}",
				magic_enum::enum_name(result));
			return false;
		}
		deviceRegistered = true;
		return true;
	}

	void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
	{
		if (!initialized || !deviceRegistered || !a_adapter ||
			!slIsFeatureLoaded || !slIsFeatureSupported ||
			!slGetFeatureRequirements) {
			featureDLSS = false;
			featureDLSSG = false;
			featureFSR = false;
			featureFSRG = false;
			featurePCL = false;
			featureReflex = false;
			return;
		}

		L->info("Checking features");
		DXGI_ADAPTER_DESC adapterDesc;
		a_adapter->GetDesc(&adapterDesc);

		sl::AdapterInfo adapterInfo;
		adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
		adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

		auto checkFeatureAvailability = [&](sl::Feature feature, const char* featureName, bool& outAvailable) {
			outAvailable = false;
			bool active = false;
			if (SL_FAILED(result, slIsFeatureLoaded(feature, active))) {
				L->info(
					"{} feature is not resident: {}",
					featureName,
					magic_enum::enum_name(result));
				sl::FeatureRequirements featureRequirements;
				const auto requirementsResult =
					slGetFeatureRequirements(feature, featureRequirements);
				if (requirementsResult != sl::Result::eOk) {
					L->info(
						"{} feature failed to load due to: {}",
						featureName,
						magic_enum::enum_name(requirementsResult));
				}
				return;
			}
			L->info(
				"{} feature is resident and {}",
				featureName,
				active ? "active" : "inactive");
			outAvailable = slIsFeatureSupported(feature, adapterInfo) == sl::Result::eOk;
		};

		checkFeatureAvailability(sl::kFeatureDLSS, "DLSS", featureDLSS);
		if (_fsrFeatureRequested) {
			checkFeatureAvailability(sl::kFeatureFSR, "FSR", featureFSR);
		}
		if (_latencyFeaturesRequested) {
			checkFeatureAvailability(sl::kFeatureDLSS_G, "DLSS-G", featureDLSSG);
			checkFeatureAvailability(sl::kFeaturePCL, "PCL", featurePCL);
			checkFeatureAvailability(sl::kFeatureReflex, "Reflex", featureReflex);
		}
		if (_fsrGFeatureRequested) {
			checkFeatureAvailability(sl::kFeatureFSR_G, "FSR-G", featureFSRG);
		}

		if (featureDLSS) {
			L->info("DLSS super-resolution is supported on the selected adapter");
		}
		if (featureDLSSG) {
			L->info("DLSS-G is supported on the selected adapter");
		}
		if (featureFSR) {
			L->info("FSR super-resolution is supported on the selected adapter");
		}
		if (featureFSRG) {
			L->info("FSR frame generation is supported on the selected adapter");
		}

		L->info("DLSS {} available", featureDLSS ? "is" : "is not");
		L->info("FSR {} available", featureFSR ? "is" : "is not");
	}

	void Streamline::PostDevice()
	{
		if (!initialized || !deviceRegistered || !slGetFeatureFunction)
			return;

		if (featureDLSS) {
			slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
			slGetFeatureFunction(
				sl::kFeatureDLSS,
				"slDLSSGetOptimalSettings",
				(void*&)slDLSSGetOptimalSettings);
		}
		if (featureDLSSG) {
			slGetFeatureFunction(
				sl::kFeatureDLSS_G,
				"slDLSSGSetOptions",
				(void*&)slDLSSGSetOptions);
			slGetFeatureFunction(
				sl::kFeatureDLSS_G,
				"slDLSSGGetState",
				(void*&)slDLSSGGetState);
		}
		if (featureFSR) {
			slGetFeatureFunction(
				sl::kFeatureFSR,
				"slFSRSetOptions",
				(void*&)slFSRSetOptions);
			slGetFeatureFunction(
				sl::kFeatureFSR,
				"slFSRGetOptimalSettings",
				(void*&)slFSRGetOptimalSettings);
			slGetFeatureFunction(
				sl::kFeatureFSR,
				"slFSRGetState",
				(void*&)slFSRGetState);
		}
		if (featureFSRG) {
			slGetFeatureFunction(
				sl::kFeatureFSR_G,
				"slFSRGSetOptions",
				(void*&)slFSRGSetOptions);
			slGetFeatureFunction(
				sl::kFeatureFSR_G,
				"slFSRGGetState",
				(void*&)slFSRGGetState);
			slGetFeatureFunction(
				sl::kFeatureFSR_G,
				"slFSRGQuiesce",
				(void*&)slFSRGQuiesce);
		}
		if (featurePCL) {
			slGetFeatureFunction(
				sl::kFeaturePCL,
				"slPCLSetMarker",
				(void*&)slPCLSetMarker);
		}
		if (featureReflex) {
			slGetFeatureFunction(
				sl::kFeatureReflex,
				"slReflexSleep",
				(void*&)slReflexSleep);
			slGetFeatureFunction(
				sl::kFeatureReflex,
				"slReflexSetOptions",
				(void*&)slReflexSetOptions);
			if (slReflexSetOptions) {
				sl::ReflexOptions options;
				options.mode = sl::ReflexMode::eOff;
				if (SL_FAILED(result, slReflexSetOptions(options))) {
					L->warn(
						"Could not initialize Reflex options: {}",
						magic_enum::enum_name(result));
				}
			}
		}
	}

	bool Streamline::Sleep(std::uint32_t a_frameIndex)
	{
		if (!featureReflex || !slReflexSleep ||
			!EnsureFrameToken(a_frameIndex)) {
			return false;
		}
		if (SL_FAILED(result, slReflexSleep(*frameToken))) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"Reflex sleep failed: {}",
				magic_enum::enum_name(result));
			return false;
		}
		return true;
	}

	bool Streamline::SetLatencyMarker(
		sl::PCLMarker a_marker,
		std::uint32_t a_frameIndex)
	{
		if (!featurePCL || !slPCLSetMarker ||
			!EnsureFrameToken(a_frameIndex)) {
			return false;
		}
		if (SL_FAILED(result, slPCLSetMarker(a_marker, *frameToken))) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"PCL marker {} failed: {}",
				static_cast<std::uint32_t>(a_marker),
				magic_enum::enum_name(result));
			return false;
		}
		return true;
	}

	bool Streamline::EnsureFrameToken(std::uint32_t a_frameIndex)
	{
		if (!initialized || !slGetNewFrameToken)
			return false;

		if (_lastFrameToken == a_frameIndex)
			return frameToken != nullptr;
		_lastFrameToken = a_frameIndex;

		if (SL_FAILED(result, slGetNewFrameToken(frameToken, &a_frameIndex))) {
			L->error("Could not get frame token: {}", magic_enum::enum_name(result));
			frameToken = nullptr;
			return false;
		}

		return frameToken != nullptr;
	}

	bool Streamline::CheckFrameConstants(
		sl::ViewportHandle p_viewport,
		std::uint32_t a_frameIndex,
		float a_jitterX,
		float a_jitterY,
		bool a_resetHistory,
		const render::temporal::FrameGenerationCamera& a_camera)
	{
		if (!initialized || !deviceRegistered || !slSetConstants)
			return false;

		if (!EnsureFrameToken(a_frameIndex))
			return false;

		if (!a_camera.valid || !(a_camera.aspectRatio > 0.0f)) {
			return false;
		}
		if (_constantsFrame == a_frameIndex) {
			const bool sameCamera =
				_constantsViewport == static_cast<std::uint32_t>(p_viewport) &&
				_constantsJitterX == a_jitterX && _constantsJitterY == a_jitterY &&
				_constantsCamera.engineFrame == a_camera.engineFrame &&
				_constantsCamera.nearPlane == a_camera.nearPlane &&
				_constantsCamera.farPlane == a_camera.farPlane &&
				_constantsCamera.verticalFov == a_camera.verticalFov &&
				_constantsCamera.aspectRatio == a_camera.aspectRatio &&
				std::equal(std::begin(a_camera.position),
					std::end(a_camera.position), _constantsCamera.position) &&
				std::equal(std::begin(a_camera.right),
					std::end(a_camera.right), _constantsCamera.right) &&
				std::equal(std::begin(a_camera.up),
					std::end(a_camera.up), _constantsCamera.up) &&
				std::equal(std::begin(a_camera.forward),
					std::end(a_camera.forward), _constantsCamera.forward) &&
				std::equal(std::begin(a_camera.currentWorldToClip),
					std::end(a_camera.currentWorldToClip), _constantsCamera.currentWorldToClip) &&
				std::equal(std::begin(a_camera.previousWorldToClip),
					std::end(a_camera.previousWorldToClip),
					_constantsCamera.previousWorldToClip) &&
				std::equal(std::begin(a_camera.viewToWorld),
					std::end(a_camera.viewToWorld),
					_constantsCamera.viewToWorld) &&
				std::equal(std::begin(a_camera.previousPosition),
					std::end(a_camera.previousPosition),
					_constantsCamera.previousPosition);
			if (!sameCamera || (a_resetHistory && !_constantsReset)) {
				L->error("Shared Streamline constants changed after submission for frame {}; rejecting the later consumer.",
					a_frameIndex);
				return false;
			}
			return true;
		}
		const auto makeMatrix = [](const float (&a_values)[16]) {
			sl::float4x4 matrix;
			for (std::uint32_t row = 0; row < 4; ++row) {
				const auto offset = row * 4;
				matrix.setRow(
					row,
					sl::float4(
						a_values[offset],
						a_values[offset + 1],
						a_values[offset + 2],
						a_values[offset + 3]));
			}
			return matrix;
		};
		const auto identity = [] {
			sl::float4x4 matrix;
			matrix.setRow(0, sl::float4(1.0f, 0.0f, 0.0f, 0.0f));
			matrix.setRow(1, sl::float4(0.0f, 1.0f, 0.0f, 0.0f));
			matrix.setRow(2, sl::float4(0.0f, 0.0f, 1.0f, 0.0f));
			matrix.setRow(3, sl::float4(0.0f, 0.0f, 0.0f, 1.0f));
			return matrix;
		};
		sl::Constants slConstants{};
		const float aspect = a_camera.aspectRatio;
		slConstants.cameraNear = a_camera.nearPlane;
		slConstants.cameraFar = a_camera.farPlane;
		slConstants.cameraAspectRatio = aspect;
		slConstants.cameraFOV = a_camera.verticalFov;
		slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
		slConstants.cameraPinholeOffset = { 0.f, 0.f };
		slConstants.cameraPos = {
			a_camera.position[0],
			a_camera.position[1],
			a_camera.position[2]
		};
		slConstants.cameraRight = {
			a_camera.right[0],
			a_camera.right[1],
			a_camera.right[2]
		};
		slConstants.cameraUp = {
			a_camera.up[0],
			a_camera.up[1],
			a_camera.up[2]
		};
		slConstants.cameraFwd = {
			a_camera.forward[0],
			a_camera.forward[1],
			a_camera.forward[2]
		};
		const auto currentWorldToClip =
			makeMatrix(a_camera.currentWorldToClip);
		const auto previousWorldToClip =
			makeMatrix(a_camera.previousWorldToClip);
		const float halfFov = a_camera.verticalFov * 0.5f;
		const float heightScale = 1.0f / std::tan(halfFov);
		const float widthScale = heightScale / aspect;
		const float depthScale =
			a_camera.farPlane /
			(a_camera.farPlane - a_camera.nearPlane);
		sl::float4x4 projection;
		projection.setRow(
			0, sl::float4(widthScale, 0.0f, 0.0f, 0.0f));
		projection.setRow(
			1, sl::float4(0.0f, heightScale, 0.0f, 0.0f));
		projection.setRow(
			2, sl::float4(0.0f, 0.0f, depthScale, 1.0f));
		projection.setRow(
			3,
			sl::float4(
				0.0f,
				0.0f,
				-a_camera.nearPlane * depthScale,
				0.0f));
		sl::float4x4 clipToWorld;
		sl::matrixFullInvert(clipToWorld, currentWorldToClip);
		slConstants.cameraViewToClip = projection;
		sl::matrixFullInvert(
			slConstants.clipToCameraView,
			slConstants.cameraViewToClip);
		sl::matrixMul(
			slConstants.clipToPrevClip,
			clipToWorld,
			previousWorldToClip);
		sl::matrixFullInvert(
			slConstants.prevClipToClip,
			slConstants.clipToPrevClip);
		slConstants.clipToLensClip = identity();
		slConstants.depthInverted = sl::Boolean::eFalse;

		slConstants.jitterOffset = { -a_jitterX, -a_jitterY };
		slConstants.reset = a_resetHistory ? sl::Boolean::eTrue : sl::Boolean::eFalse;

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

		_constantsFrame = a_frameIndex;
		_constantsViewport = static_cast<std::uint32_t>(p_viewport);
		_constantsReset = a_resetHistory;
		_constantsJitterX = a_jitterX;
		_constantsJitterY = a_jitterY;
		_constantsCamera = a_camera;
		return true;
	}

	bool Streamline::SetDLSSOptions(
		sl::ViewportHandle p_viewport,
		const render::temporal::SuperResolutionRequest& a_request)
	{
		if (!slDLSSSetOptions)
			return false;

		const auto mode = ToDLSSMode(a_request.qualityMode);
		if (!mode) {
			return false;
		}

		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = *mode;
		dlssOptions.outputWidth = a_request.outputWidth;
		dlssOptions.outputHeight = a_request.outputHeight;
		dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;
		dlssOptions.useAutoExposure = sl::Boolean::eTrue;

		std::optional<sl::DLSSPreset> customPreset;
		switch (a_request.providerPreset) {
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
		_dlssResourcesConfigured = true;
		return true;
	}

	render::temporal::SuperResolutionSizeResult
		Streamline::QueryDLSSRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request)
	{
		const auto mode = ToDLSSMode(a_request.qualityMode);
		if (!featureDLSS || !slDLSSGetOptimalSettings || !mode ||
			!a_request.outputWidth || !a_request.outputHeight) {
			return {
				.result = {
					.code =
						render::temporal::ProviderResultCode::kFailure,
					.message =
						"DLSS render-size query is unavailable or invalid."
				}
			};
		}

		sl::DLSSOptions options{};
		options.mode = *mode;
		options.outputWidth = a_request.outputWidth;
		options.outputHeight = a_request.outputHeight;
		sl::DLSSOptimalSettings settings{};
		const auto sdkResult =
			slDLSSGetOptimalSettings(options, settings);
		if (sdkResult != sl::Result::eOk ||
			!settings.optimalRenderWidth ||
			!settings.optimalRenderHeight) {
			return {
				.result = {
					.code =
						render::temporal::ProviderResultCode::kFailure,
					.sdkResult = static_cast<std::int64_t>(sdkResult),
					.message = "DLSS optimal-settings query failed."
				}
			};
		}
		return {
			.result = {
				.code =
					render::temporal::ProviderResultCode::kSuccess,
				.sdkResult = static_cast<std::int64_t>(sdkResult)
			},
			.renderWidth = settings.optimalRenderWidth,
			.renderHeight = settings.optimalRenderHeight
		};
	}

	bool Streamline::SetFSROptions(
		sl::ViewportHandle p_viewport,
		const render::temporal::SuperResolutionRequest& a_request)
	{
		const auto mode = ToFSRMode(a_request.qualityMode);
		if (!slFSRSetOptions || !mode ||
			!render::temporal::IsFo4PostTonemapSdr(a_request.color)) {
			return false;
		}

		sl::FSROptions options{};
		options.mode = *mode;
		options.outputWidth = a_request.outputWidth;
		options.outputHeight = a_request.outputHeight;
		options.maxRenderWidth = a_request.outputWidth;
		options.maxRenderHeight = a_request.outputHeight;
		options.sharpness = a_request.sharpness;
		options.preExposure = a_request.color.preExposure;
		options.frameTimeDeltaMilliseconds =
			a_request.frameTimeMilliseconds;
		options.viewSpaceToMetersFactor = 0.01428222656f;
		options.colorSpace = sl::FSRColorSpace::eGamma22;
		options.useAutoExposure = sl::Boolean::eTrue;
		options.dynamicResolutionEnabled = sl::Boolean::eFalse;
		if (SL_FAILED(result, slFSRSetOptions(p_viewport, options))) {
			L->error("Could not enable FSR: {}",
				magic_enum::enum_name(result));
			return false;
		}
		_fsrResourcesConfigured = true;
		return true;
	}

	render::temporal::SuperResolutionSizeResult
		Streamline::QueryFSRRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request)
	{
		const auto mode = ToFSRMode(a_request.qualityMode);
		if (!featureFSR || !slFSRGetOptimalSettings || !mode ||
			!a_request.outputWidth || !a_request.outputHeight) {
			return {
				.result = {
					.code =
						render::temporal::ProviderResultCode::kUnavailable,
					.message =
						"Native FSR render-size query is unavailable."
				}
			};
		}

		sl::FSROptions options{};
		options.mode = *mode;
		options.outputWidth = a_request.outputWidth;
		options.outputHeight = a_request.outputHeight;
		sl::FSROptimalSettings settings{};
		const auto sdkResult =
			slFSRGetOptimalSettings(options, settings);
		if (sdkResult != sl::Result::eOk ||
			!settings.optimalRenderWidth ||
			!settings.optimalRenderHeight) {
			return {
				.result = {
					.code =
						render::temporal::ProviderResultCode::kFailure,
					.sdkResult = static_cast<std::int64_t>(sdkResult),
					.message = "FSR optimal-settings query failed.",
					.failureDomain =
						render::temporal::FailureDomain::kStreamline
				}
			};
		}
		return {
			.result = {
				.code =
					render::temporal::ProviderResultCode::kSuccess,
				.sdkResult = static_cast<std::int64_t>(sdkResult)
			},
			.renderWidth = settings.optimalRenderWidth,
			.renderHeight = settings.optimalRenderHeight
		};
	}

	render::temporal::ProviderResult Streamline::UpscaleD3D12(
		const render::temporal::SuperResolutionRequest& a_request)
	{
		return UpscaleD3D12Feature(a_request, false);
	}

	render::temporal::ProviderResult Streamline::UpscaleFSRD3D12(
		const render::temporal::SuperResolutionRequest& a_request)
	{
		return UpscaleD3D12Feature(a_request, true);
	}

	render::temporal::ProviderResult Streamline::UpscaleD3D12Feature(
		const render::temporal::SuperResolutionRequest& a_request,
		bool a_fsr)
	{
		const auto* recording =
			std::get_if<render::temporal::D3D12RecordingContext>(
				&a_request.recording);
		const auto* colorInput =
			render::temporal::GetD3D12View(a_request.colorInput);
		const auto* privateOutput =
			render::temporal::GetD3D12View(a_request.privateOutput);
		const auto* depth =
			render::temporal::GetD3D12View(a_request.depth);
		const auto* motion =
			render::temporal::GetD3D12View(a_request.motionVectors);
		const auto* reactive =
			render::temporal::GetD3D12View(a_request.reactiveMask);
		const auto* transparency =
			render::temporal::GetD3D12View(
				a_request.transparencyCompositionMask);
		if (!recording || !recording->commandList || !colorInput ||
			!privateOutput || !depth || !motion || !reactive ||
			!transparency || !colorInput->resource ||
			!privateOutput->resource || !depth->resource ||
			!motion->resource || !reactive->resource ||
			!transparency->resource || !a_request.renderWidth ||
			!a_request.renderHeight || !a_request.outputWidth ||
			!a_request.outputHeight ||
			colorInput->resource == privateOutput->resource ||
			!render::temporal::IsFo4PostTonemapSdr(a_request.color) ||
			!slSetTagForFrame || !slEvaluateFeature ||
			!deviceRegistered ||
			(a_fsr && !featureFSR)) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.message = a_fsr
					? "FSR received incomplete or incompatible D3D12 inputs."
					: "DLSS received incomplete or incompatible D3D12 inputs.",
				.failureDomain =
					a_fsr && !featureFSR
						? render::temporal::FailureDomain::kStreamline
						: render::temporal::FailureDomain::kTransport
			};
		}

		if (!CheckFrameConstants(
				viewport,
				static_cast<std::uint32_t>(a_request.realFrame),
				a_request.jitterX,
				a_request.jitterY,
				a_request.resetHistory,
				a_request.camera) ||
			!(a_fsr ? SetFSROptions(viewport, a_request) :
					   SetDLSSOptions(viewport, a_request))) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.message = a_fsr
					? "FSR constants or options were rejected."
					: "DLSS constants or options were rejected.",
				.failureDomain =
					render::temporal::FailureDomain::kSuperResolution
			};
		}

		sl::Resource colorIn(
			sl::ResourceType::eTex2d,
			colorInput->resource,
			nullptr,
			nullptr,
			colorInput->state);
		sl::Resource colorOut(
			sl::ResourceType::eTex2d,
			privateOutput->resource,
			nullptr,
			nullptr,
			privateOutput->state);
		sl::Resource depthResource(
			sl::ResourceType::eTex2d,
			depth->resource,
			nullptr,
			nullptr,
			depth->state);
		sl::Resource motionResource(
			sl::ResourceType::eTex2d,
			motion->resource,
			nullptr,
			nullptr,
			motion->state);
		sl::Resource reactiveResource(
			sl::ResourceType::eTex2d,
			reactive->resource,
			nullptr,
			nullptr,
			reactive->state);
		sl::Resource transparencyResource(
			sl::ResourceType::eTex2d,
			transparency->resource,
			nullptr,
			nullptr,
			transparency->state);
		const sl::Extent inputExtent{
			0, 0, a_request.renderWidth, a_request.renderHeight
		};
		const sl::Extent outputExtent{
			0, 0, a_request.outputWidth, a_request.outputHeight
		};
		const sl::BufferType reactiveType = a_fsr
			? sl::kBufferTypeReactiveMaskHint
			: sl::kBufferTypeBiasCurrentColorHint;
		const sl::BufferType transparencyType = a_fsr
			? sl::kBufferTypeTransparencyAndCompositionMaskHint
			: sl::kBufferTypeTransparencyHint;
		const sl::ResourceTag tags[]{
			{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent },
			{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &outputExtent },
			{ &depthResource, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent },
			{ &motionResource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent },
			{ &reactiveResource, reactiveType, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent },
			{ &transparencyResource, transparencyType, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent }
		};
		if (SL_FAILED(result, slSetTagForFrame(
			*frameToken,
			viewport,
			tags,
			_countof(tags),
			recording->commandList))) {
			L->error(
				"Could not tag D3D12 {} inputs: {}",
				a_fsr ? "FSR" : "DLSS-SR",
				magic_enum::enum_name(result));
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(result),
				.message = a_fsr
					? "D3D12 FSR input tagging failed."
					: "D3D12 DLSS input tagging failed.",
				.failureDomain =
					render::temporal::FailureDomain::kSuperResolution
			};
		}

		const sl::ViewportHandle view(viewport);
		const sl::BaseStructure* inputs[]{ &view };
		if (SL_FAILED(result, slEvaluateFeature(
			a_fsr ? sl::kFeatureFSR : sl::kFeatureDLSS,
			*frameToken,
			inputs,
			_countof(inputs),
			recording->commandList))) {
			L->error(
				"D3D12 {} evaluation failed: {}",
				a_fsr ? "FSR" : "DLSS-SR",
				magic_enum::enum_name(result));
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(result),
				.message = a_fsr
					? "D3D12 FSR evaluation failed."
					: "D3D12 DLSS evaluation failed.",
				.failureDomain =
					render::temporal::FailureDomain::kSuperResolution
			};
		}
		return {
			.code = render::temporal::ProviderResultCode::kSuccess,
			.workState = render::temporal::ProviderWorkState::kRecorded
		};
	}

	bool Streamline::ConfigureDLSSG(
		bool a_enabled,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight,
		std::uint32_t a_backBufferCount,
		bool a_retainResources)
	{
		if (!featureDLSSG || !slDLSSGSetOptions ||
			!a_outputWidth || !a_outputHeight || !a_backBufferCount) {
			return false;
		}
		if (a_enabled) {
			if (!featureReflex || !slReflexSetOptions) {
				return false;
			}
			sl::ReflexOptions reflex{};
			reflex.mode = sl::ReflexMode::eLowLatency;
			reflex.useMarkersToOptimize = false;
			if (SL_FAILED(result, slReflexSetOptions(reflex))) {
				L->error(
					"Could not enable Reflex for DLSS-G: {}",
					magic_enum::enum_name(result));
				return false;
			}
		}
		sl::DLSSGOptions options{};
		options.mode = a_enabled
			? sl::DLSSGMode::eOn
			: sl::DLSSGMode::eOff;
		options.numFramesToGenerate = 1;
		options.mvecDepthWidth = a_renderWidth;
		options.mvecDepthHeight = a_renderHeight;
		options.colorWidth = a_outputWidth;
		options.colorHeight = a_outputHeight;
		options.numBackBuffers = a_backBufferCount;
		options.colorBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		options.mvecBufferFormat = DXGI_FORMAT_R16G16_FLOAT;
		options.depthBufferFormat = DXGI_FORMAT_R32_FLOAT;
		options.hudLessBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		options.enableUserInterfaceRecomposition = sl::Boolean::eFalse;
		if (a_retainResources) {
			options.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
		}
		if (SL_FAILED(result, slDLSSGSetOptions(viewport, options))) {
			L->error(
				"Could not configure DLSS-G: {}",
				magic_enum::enum_name(result));
			return false;
		}
		_dlssGResourcesConfigured = true;
		return true;
	}

	bool Streamline::ConfigureFSRG(
		bool a_enabled,
		const render::temporal::FrameGenerationRequest& a_request)
	{
		if (!featureFSRG || !slFSRGSetOptions ||
			!a_request.outputWidth || !a_request.outputHeight ||
			!render::temporal::IsFo4PostTonemapSdr(a_request.color)) {
			return false;
		}
		sl::FSRGOptions options{};
		options.mode =
			a_enabled ? sl::FSRGMode::eOn : sl::FSRGMode::eOff;
		options.displayWidth = a_request.outputWidth;
		options.displayHeight = a_request.outputHeight;
		options.maxRenderWidth = a_request.outputWidth;
		options.maxRenderHeight = a_request.outputHeight;
		options.backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		options.colorSpace = sl::FSRColorSpace::eGamma22;
		options.frameTimeDeltaMilliseconds =
			a_request.frameTimeMilliseconds;
		options.viewSpaceToMetersFactor = 0.01428222656f;
		options.onlyPresentGenerated = sl::Boolean::eFalse;
		if (SL_FAILED(result, slFSRGSetOptions(viewport, options))) {
			L->error(
				"Could not configure FSR-G: {}",
				magic_enum::enum_name(result));
			return false;
		}
		_fsrGResourcesConfigured = true;
		return true;
	}

	bool Streamline::TagDLSSGFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		return TagFrameGenerationFrame(
			a_request, sl::kFeatureDLSS_G, false);
	}

	bool Streamline::TagFSRGFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		return TagFrameGenerationFrame(
			a_request, sl::kFeatureFSR_G, true);
	}

	bool Streamline::TagFrameGenerationFrame(
		const render::temporal::FrameGenerationRequest& a_request,
		sl::Feature a_feature,
		bool a_evaluate)
	{
		const bool featureAvailable =
			a_feature == sl::kFeatureDLSS_G ? featureDLSSG : featureFSRG;
		if (!featureAvailable || !slSetTagForFrame || !slSetConstants ||
			(a_evaluate && !slEvaluateFeature) ||
			!EnsureFrameToken(static_cast<std::uint32_t>(a_request.realFrame)) ||
			!a_request.recording.commandList || !a_request.depth.resource ||
			!a_request.motionVectors.resource ||
			!a_request.hudlessColor.resource ||
			!a_request.camera.valid ||
			!a_request.outputWidth || !a_request.outputHeight) {
			return false;
		}

		if (!CheckFrameConstants(
				viewport,
				static_cast<std::uint32_t>(a_request.realFrame),
				a_request.jitterX,
				a_request.jitterY,
				a_request.resetHistory,
				a_request.camera)) {
			return false;
		}

		sl::Resource depth(
			sl::ResourceType::eTex2d,
			a_request.depth.resource,
			nullptr,
			nullptr,
			a_request.depth.state);
		sl::Resource motion(
			sl::ResourceType::eTex2d,
			a_request.motionVectors.resource,
			nullptr,
			nullptr,
			a_request.motionVectors.state);
		sl::Resource hudless(
			sl::ResourceType::eTex2d,
			a_request.hudlessColor.resource,
			nullptr,
			nullptr,
			a_request.hudlessColor.state);
		const sl::Extent renderExtent{
			0, 0, a_request.renderWidth, a_request.renderHeight
		};
		const sl::Extent outputExtent{
			0, 0, a_request.outputWidth, a_request.outputHeight
		};
		const sl::ResourceTag tags[]{
			{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent },
			{ &motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent },
			{ &hudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &outputExtent }
		};
		if (SL_FAILED(result, slSetTagForFrame(
			*frameToken,
			viewport,
			tags,
			_countof(tags),
			reinterpret_cast<sl::CommandBuffer*>(
				a_request.recording.commandList)))) {
			L->error(
				"Could not tag {} inputs: {}",
				a_feature == sl::kFeatureDLSS_G ? "DLSS-G" : "FSR-G",
				magic_enum::enum_name(result));
			return false;
		}
		if (a_evaluate) {
			const sl::ViewportHandle view(viewport);
			const sl::BaseStructure* inputs[]{ &view };
			if (SL_FAILED(result, slEvaluateFeature(
				a_feature,
				*frameToken,
				inputs,
				_countof(inputs),
				reinterpret_cast<sl::CommandBuffer*>(
					a_request.recording.commandList)))) {
				L->error(
					"Could not evaluate FSR-G preparation: {}",
					magic_enum::enum_name(result));
				return false;
			}
		}
		return true;
	}

	bool Streamline::ClearFrameGenerationTags(
		std::uint32_t a_frameIndex,
		ID3D12GraphicsCommandList* a_commandList) noexcept
	{
		return ClearFrameGenerationTagsChecked(
			a_frameIndex, a_commandList) == sl::Result::eOk;
	}

	sl::Result Streamline::ClearFrameGenerationTagsChecked(
		std::uint32_t a_frameIndex,
		ID3D12GraphicsCommandList* a_commandList) noexcept
	{
		if (!slSetTagForFrame) {
			return sl::Result::eErrorMissingOrInvalidAPI;
		}
		if (!EnsureFrameToken(a_frameIndex)) {
			return sl::Result::eErrorInvalidState;
		}
		const sl::ResourceTag tags[]{
			{ nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, nullptr },
			{ nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, nullptr },
			{ nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, nullptr }
		};
		const auto result = slSetTagForFrame(
			*frameToken,
			viewport,
			tags,
			_countof(tags),
			reinterpret_cast<sl::CommandBuffer*>(a_commandList));
		if (result != sl::Result::eOk) {
			L->error(
				"Could not invalidate frame-generation inputs: {}",
				magic_enum::enum_name(result));
			return result;
		}
		return sl::Result::eOk;
	}

	bool Streamline::PollDLSSGState() noexcept
	{
		_dlssGInputCompletionDependency.reset();
		if (!featureDLSSG || !slDLSSGGetState) {
			return !featureDLSSG;
		}
		sl::DLSSGState state{};
		const auto result = slDLSSGGetState(viewport, state, nullptr);
		if (result != sl::Result::eOk) {
			L->error(
				"Could not query DLSS-G state: {}",
				magic_enum::enum_name(result));
			return false;
		}
		_dlssGPresentedFrames.Add(state.numFramesActuallyPresented);
		_dlssGStatus = state.status;
		if (state.inputsProcessingCompletionFence &&
			state.lastPresentInputsProcessingCompletionFenceValue) {
			render::temporal::GpuCompletionDependency dependency{
				.value =
					state.lastPresentInputsProcessingCompletionFenceValue
			};
			dependency.fence.copy_from(
				static_cast<ID3D12Fence*>(
					state.inputsProcessingCompletionFence));
			_dlssGInputCompletionDependency = std::move(dependency);
		}
		if (_dlssGStatus != sl::DLSSGStatus::eOk) {
			L->error(
				"DLSS-G reported status {:#x}",
				static_cast<std::uint32_t>(_dlssGStatus));
			return false;
		}
		return true;
	}

	bool Streamline::CheckFSRGCompletionCapability() noexcept
	{
		return PollFSRGState(false);
	}

	bool Streamline::PollFSRGState() noexcept
	{
		return PollFSRGState(true);
	}

	bool Streamline::PollFSRGState(
		bool a_requireSubmittedDependency) noexcept
	{
		_fsrGInputCompletionDependency.reset();
		if (!featureFSRG || !slFSRGGetState) {
			return false;
		}
		sl::FSRGState state{};
		const auto result = slFSRGGetState(viewport, state);
		if (result != sl::Result::eOk) {
			L->error(
				"Could not query FSR-G state: {}",
				magic_enum::enum_name(result));
			return false;
		}
		winrt::com_ptr<ID3D12Fence> completionFence;
		completionFence.attach(
			static_cast<ID3D12Fence*>(state.completionFence));
		if (state.completionMode !=
				sl::FSRGCompletionMode::eFence ||
			!completionFence) {
			L->error(
				"FSR-G does not expose the required host-input completion "
				"fence");
			return false;
		}
		if (state.completionFenceValue) {
			_fsrGInputCompletionDependency =
				render::temporal::GpuCompletionDependency{
					.fence = std::move(completionFence),
					.value = state.completionFenceValue
				};
		} else if (a_requireSubmittedDependency) {
			L->error(
				"FSR-G did not expose a submitted host-input completion "
				"value after Present");
			return false;
		}
		return true;
	}

	bool Streamline::ClearCurrentFrameGenerationTags() noexcept
	{
		return ClearCurrentFrameGenerationTagsChecked() ==
		       sl::Result::eOk;
	}

	sl::Result Streamline::ClearCurrentFrameGenerationTagsChecked() noexcept
	{
		return !frameToken || _lastFrameToken == UINT32_MAX
			? sl::Result::eOk
			: ClearFrameGenerationTagsChecked(_lastFrameToken);
	}

	std::uint32_t Streamline::ConsumeDLSSGPresentedFrameCount() noexcept
	{
		return _dlssGPresentedFrames.Consume();
	}

	std::optional<render::temporal::GpuCompletionDependency>
	Streamline::ConsumeDLSSGInputCompletionDependency() noexcept
	{
		return std::exchange(_dlssGInputCompletionDependency, std::nullopt);
	}

	std::optional<render::temporal::GpuCompletionDependency>
	Streamline::ConsumeFSRGInputCompletionDependency() noexcept
	{
		return std::exchange(
			_fsrGInputCompletionDependency, std::nullopt);
	}

	render::temporal::ProviderResult
	Streamline::DestroyDLSSGResources() noexcept
	{
		if (!_dlssGResourcesConfigured) {
			return {
				.code =
					render::temporal::ProviderResultCode::kSuccess
			};
		}
		if (!slDLSSGSetOptions || !slFreeResources) {
			return {
				.code =
					render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(
					sl::Result::eErrorMissingOrInvalidAPI),
				.message =
					"Streamline cleanup exports are unavailable."
			};
		}
		const auto cleanup = streamline_fg::DestroyResources(
			_dlssGResourcesConfigured,
			viewport,
			[&]() {
				return ClearCurrentFrameGenerationTagsChecked();
			},
			slDLSSGSetOptions,
			slFreeResources);
		if (!cleanup.succeeded) {
			L->error(
				"Could not {}: {}",
				cleanup.operation,
				magic_enum::enum_name(cleanup.sdkResult));
			return {
				.code =
					render::temporal::ProviderResultCode::kFailure,
				.sdkResult =
					static_cast<std::int64_t>(cleanup.sdkResult),
				.message =
					std::string("Streamline could not ") +
					cleanup.operation + "."
			};
		}
		(void)_dlssGPresentedFrames.Consume();
		_dlssGInputCompletionDependency.reset();
		_dlssGStatus = sl::DLSSGStatus::eOk;
		_dlssGResourcesConfigured = false;
		return {
			.code =
				render::temporal::ProviderResultCode::kSuccess
		};
	}

	render::temporal::ProviderResult
	Streamline::SetDLSSGPresentationActive(bool a_active) noexcept
	{
		return SetPresentationFeatureActive(
			sl::kFeatureDLSS_G, a_active, "DLSS-G");
	}

	render::temporal::ProviderResult
	Streamline::DestroyFSRGResources() noexcept
	{
		if (!_fsrGResourcesConfigured) {
			return {
				.code =
					render::temporal::ProviderResultCode::kSuccess
			};
		}
		if (!slFSRGSetOptions || !slFSRGQuiesce || !slFreeResources) {
			return {
				.code =
					render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(
					sl::Result::eErrorMissingOrInvalidAPI),
				.message =
					"Streamline FSR-G cleanup exports are unavailable.",
				.failureDomain =
					render::temporal::FailureDomain::kStreamline
			};
		}
		sl::FSRGOptions options{};
		options.mode = sl::FSRGMode::eOff;
		auto result = ClearCurrentFrameGenerationTagsChecked();
		if (result == sl::Result::eOk) {
			result = slFSRGSetOptions(viewport, options);
		}
		if (result == sl::Result::eOk) {
			result = slFSRGQuiesce();
		}
		if (result == sl::Result::eOk) {
			result = slFreeResources(sl::kFeatureFSR_G, viewport);
		}
		if (result != sl::Result::eOk) {
			return {
				.code =
					render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(result),
				.message =
					"Streamline could not retire FSR-G resources.",
				.failureDomain =
					render::temporal::FailureDomain::kStreamline
			};
		}
		_fsrGInputCompletionDependency.reset();
		_fsrGResourcesConfigured = false;
		return {
			.code =
				render::temporal::ProviderResultCode::kSuccess
		};
	}

	render::temporal::ProviderResult
	Streamline::SetFSRGPresentationActive(bool a_active) noexcept
	{
		return SetPresentationFeatureActive(
			sl::kFeatureFSR_G, a_active, "FSR-G");
	}

	render::temporal::ProviderResult
	Streamline::SetPresentationFeatureActive(
		sl::Feature a_feature,
		bool a_active,
		const char* a_name) noexcept
	{
		if (!initialized || !slSetFeatureLoaded) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(
					sl::Result::eErrorMissingOrInvalidAPI),
				.message =
					"Streamline presentation-hook control is unavailable.",
				.failureDomain =
					render::temporal::FailureDomain::kStreamline
			};
		}
		const auto result =
			slSetFeatureLoaded(a_feature, a_active);
		if (result != sl::Result::eOk) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(result),
				.message = std::string("Streamline could not ") +
					(a_active ? "enable " : "disable ") + a_name +
					" presentation hooks.",
				.failureDomain =
					render::temporal::FailureDomain::kStreamline
			};
		}
		return {
			.code = render::temporal::ProviderResultCode::kSuccess
		};
	}

	render::temporal::ProviderResult
	Streamline::DestroyDLSSResources() noexcept
	{
		if (!_dlssResourcesConfigured) {
			return {
				.code = render::temporal::ProviderResultCode::kSuccess
			};
		}
		if (!slDLSSSetOptions || !slFreeResources) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(
					sl::Result::eErrorMissingOrInvalidAPI),
				.message = "Streamline DLSS cleanup exports are unavailable.",
				.failureDomain = render::temporal::FailureDomain::kStreamline
			};
		}

		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = sl::DLSSMode::eOff;

		const auto disable = slDLSSSetOptions(viewport, dlssOptions);
		if (disable != sl::Result::eOk) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(disable),
				.message = "Streamline could not disable DLSS.",
				.failureDomain = render::temporal::FailureDomain::kStreamline
			};
		}
		const auto release = slFreeResources(sl::kFeatureDLSS, viewport);
		if (release != sl::Result::eOk) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(release),
				.message = "Streamline could not release DLSS resources.",
				.failureDomain = render::temporal::FailureDomain::kStreamline
			};
		}
		_dlssResourcesConfigured = false;
		_constantsFrame.reset();
		return {
			.code = render::temporal::ProviderResultCode::kSuccess
		};
	}

	render::temporal::ProviderResult
	Streamline::DestroyFSRResources() noexcept
	{
		if (!_fsrResourcesConfigured) {
			return {
				.code = render::temporal::ProviderResultCode::kSuccess
			};
		}
		if (!slFSRSetOptions || !slFreeResources) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(
					sl::Result::eErrorMissingOrInvalidAPI),
				.message = "Streamline FSR cleanup exports are unavailable.",
				.failureDomain =
					render::temporal::FailureDomain::kSuperResolution
			};
		}

		sl::FSROptions options{};
		options.mode = sl::FSRMode::eOff;
		const auto disable = slFSRSetOptions(viewport, options);
		if (disable != sl::Result::eOk) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(disable),
				.message = "Streamline could not disable FSR.",
				.failureDomain =
					render::temporal::FailureDomain::kSuperResolution
			};
		}
		const auto release = slFreeResources(sl::kFeatureFSR, viewport);
		if (release != sl::Result::eOk) {
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = static_cast<std::int64_t>(release),
				.message = "Streamline could not release FSR resources.",
				.failureDomain =
					render::temporal::FailureDomain::kSuperResolution
			};
		}
		_fsrResourcesConfigured = false;
		_constantsFrame.reset();
		return {
			.code = render::temporal::ProviderResultCode::kSuccess
		};
	}
}
