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
		bool a_loadDlssG,
		sl::RenderAPI a_renderApi)
	{
		triedInitialization = true;
		_latencyFeaturesRequested = a_loadDlssG;
		_renderApi = a_renderApi;

		if (a_loadDlssG) {
			const auto runtimePath =
				std::wstring(PluginDir) + L"\\nvngx_dlssg.dll";
			if (GetFileAttributesW(runtimePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
				L->error("DLSS-G requires the shipped nvngx_dlssg.dll; restore the complete SDK package (error {}).",
					GetLastError());
				return;
			}
		}

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

		sl::Feature featuresToLoad[4]{};
		std::uint32_t featureCount = 0;
		if (a_loadDlss) {
			featuresToLoad[featureCount++] = sl::kFeatureDLSS;
		}
		if (a_loadDlssG) {
			featuresToLoad[featureCount++] = sl::kFeatureDLSS_G;
			featuresToLoad[featureCount++] = sl::kFeaturePCL;
			featuresToLoad[featureCount++] = sl::kFeatureReflex;
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

		pref.renderAPI = a_renderApi;
		pref.flags = sl::PreferenceFlags::eUseManualHooking |
			sl::PreferenceFlags::eUseFrameBasedResourceTagging;

		slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
		slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
		slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
		slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
		slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
		slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
		slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
		slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
		slSetTagForFrame =
			(PFun_slSetTagForFrame*)GetProcAddress(interposer, "slSetTagForFrame");
		slGetNativeInterface =
			(PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
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

	streamline::SwapChainUpgradeResult Streamline::UpgradeD3D11SwapChain(
		IDXGISwapChain** a_swapChain,
		bool a_dlssAdmitted) noexcept
	{
		if (!streamline::ShouldInstallSwapChainProxy(
				initialized,
				deviceRegistered,
				a_dlssAdmitted,
				IsD3D12Session())) {
			return {
				.status = streamline::SwapChainUpgradeStatus::kIneligible
			};
		}
		const auto result = streamline::UpgradeD3D11SwapChain(
			slUpgradeInterface, slGetNativeInterface, a_swapChain);
		if (result.Succeeded()) {
			L->info(
				"Installed the Streamline D3D11 swap-chain presentation proxy");
		} else {
			L->error(
				"Failed to install the Streamline D3D11 swap-chain presentation proxy "
				"(status {}, SDK {})",
				static_cast<unsigned>(result.status),
				magic_enum::enum_name(result.sdkResult));
		}
		return result;
	}

	bool Streamline::PrepareD3D12Device(ID3D12Device** a_device)
	{
		if (!initialized || _renderApi != sl::RenderAPI::eD3D12 ||
			!slUpgradeInterface || !a_device || !*a_device) {
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
		if (!initialized || _renderApi != sl::RenderAPI::eD3D12 ||
			!slUpgradeInterface || !a_factory || !*a_factory) {
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
		if (!initialized || _renderApi != sl::RenderAPI::eD3D12 ||
			!slSetD3DDevice || !a_device) {
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
		if (_latencyFeaturesRequested) {
			checkFeatureAvailability(sl::kFeatureDLSS_G, "DLSS-G", featureDLSSG);
			checkFeatureAvailability(sl::kFeaturePCL, "PCL", featurePCL);
			checkFeatureAvailability(sl::kFeatureReflex, "Reflex", featureReflex);
		}

		if (featureDLSS) {
			L->info("DLSS super-resolution is supported on the selected adapter");
		}
		if (featureDLSSG) {
			L->info("DLSS-G is supported on the selected adapter");
		}

		L->info("DLSS {} available", featureDLSS ? "is" : "is not");
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
				std::equal(std::begin(a_camera.currentWorldToClip),
					std::end(a_camera.currentWorldToClip), _constantsCamera.currentWorldToClip) &&
				std::equal(std::begin(a_camera.previousWorldToClip),
					std::end(a_camera.previousWorldToClip), _constantsCamera.previousWorldToClip);
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
		const SuperResolutionExecutionContext& a_context)
	{
		if (!slDLSSSetOptions)
			return false;

		const auto mode = ToDLSSMode(a_context.qualityMode);
		if (!mode) {
			return false;
		}

		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = *mode;
		dlssOptions.outputWidth = a_context.outputWidth;
		dlssOptions.outputHeight = a_context.outputHeight;
		dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;
		dlssOptions.useAutoExposure = sl::Boolean::eTrue;

		std::optional<sl::DLSSPreset> customPreset;
		switch (a_context.providerPreset) {
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

	void Streamline::EvaluateDLSS(
		sl::ViewportHandle vp,
		const SuperResolutionExecutionContext& a_context,
		const sl::Extent& extentIn,
		const sl::Extent& extentOut)
	{
		_evaluatedThisDispatch = false;

		auto* context = a_context.commandContext;
		if (!context || !slSetTagForFrame || !slEvaluateFeature)
			return;
		if (!deviceRegistered) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err,
				"DLSS evaluation skipped: the device was never registered with Streamline");
			return;
		}

		sl::Resource colorInRes = { sl::ResourceType::eTex2d, a_context.colorInput, 0 };
		sl::Resource colorOutRes = { sl::ResourceType::eTex2d, a_context.privateOutput, 0 };
		sl::Resource depthRes = { sl::ResourceType::eTex2d, a_context.depth, 0 };
		sl::Resource mvecRes = { sl::ResourceType::eTex2d, a_context.motionVectors, 0 };
		sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, a_context.reactiveMask, 0 };
		sl::Resource transparencyMaskRes = {
			sl::ResourceType::eTex2d, a_context.transparencyCompositionMask, 0
		};

		if (!CheckFrameConstants(
				vp,
				static_cast<std::uint32_t>(a_context.realFrame),
				a_context.jitterX,
				a_context.jitterY,
				a_context.resetHistory,
				a_context.camera))
			return;

		if (!SetDLSSOptions(vp, a_context))
			return;

		sl::ResourceTag tags[] = {
			{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
			{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extentOut },
			{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
			{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
			{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn },
			{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilEvaluate, &extentIn }
		};

		const sl::Result tagResult =
			slSetTagForFrame(*frameToken, vp, tags, _countof(tags), context);
		if (tagResult != sl::Result::eOk) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err,
				"slSetTagForFrame failed: {}", magic_enum::enum_name(tagResult));
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

	bool Streamline::Upscale(const SuperResolutionExecutionContext& a_context)
	{
		if (!a_context.commandContext || !a_context.depth || !a_context.colorInput ||
			!a_context.privateOutput || !a_context.reactiveMask ||
			!a_context.transparencyCompositionMask || !a_context.motionVectors ||
			!a_context.renderWidth || !a_context.renderHeight ||
			!a_context.outputWidth || !a_context.outputHeight ||
			a_context.colorInput == a_context.privateOutput ||
			!IsFo4PostTonemapSdr(a_context.color))
			return false;

		sl::Extent extentIn{ 0, 0, a_context.renderWidth, a_context.renderHeight };
		sl::Extent extentOut{ 0, 0, a_context.outputWidth, a_context.outputHeight };

		EvaluateDLSS(viewport, a_context, extentIn, extentOut);

		return _evaluatedThisDispatch;
	}

	bool Streamline::UpscaleD3D12(
		const render::temporal::SuperResolutionRequest& a_request)
	{
		const auto* recording =
			std::get_if<render::temporal::D3D12RecordingContext>(
				&a_request.recording);
		const auto* colorInput =
			std::get_if<render::temporal::D3D12GpuView>(
				&a_request.colorInput);
		const auto* privateOutput =
			std::get_if<render::temporal::D3D12GpuView>(
				&a_request.privateOutput);
		const auto* depth =
			std::get_if<render::temporal::D3D12GpuView>(
				&a_request.depth);
		const auto* motion =
			std::get_if<render::temporal::D3D12GpuView>(
				&a_request.motionVectors);
		const auto* reactive =
			std::get_if<render::temporal::D3D12GpuView>(
				&a_request.reactiveMask);
		const auto* transparency =
			std::get_if<render::temporal::D3D12GpuView>(
				&a_request.transparencyCompositionMask);
		if (!recording || !recording->commandList || !colorInput ||
			!privateOutput || !depth || !motion || !reactive ||
			!transparency || !colorInput->resource ||
			!privateOutput->resource || !depth->resource ||
			!motion->resource || !reactive->resource ||
			!transparency->resource || !a_request.renderWidth ||
			!a_request.renderHeight || !a_request.outputWidth ||
			!a_request.outputHeight ||
			!render::temporal::IsFo4PostTonemapSdr(a_request.color) ||
			!slSetTagForFrame || !slEvaluateFeature ||
			!deviceRegistered) {
			return false;
		}

		const auto legacyContext = SuperResolutionExecutionContext{
			.renderWidth = a_request.renderWidth,
			.renderHeight = a_request.renderHeight,
			.outputWidth = a_request.outputWidth,
			.outputHeight = a_request.outputHeight,
			.qualityMode = a_request.qualityMode,
			.providerPreset = a_request.providerPreset,
			.realFrame = a_request.realFrame,
			.engineFrame = a_request.engineFrame,
			.jitterX = a_request.jitterX,
			.jitterY = a_request.jitterY,
			.sharpness = a_request.sharpness,
			.frameTimeMilliseconds = a_request.frameTimeMilliseconds,
			.cameraNear = a_request.cameraNear,
			.cameraFar = a_request.cameraFar,
			.cameraVerticalFov = a_request.cameraVerticalFov,
			.resetHistory = a_request.resetHistory,
			.color = a_request.color
		};
		if (!CheckFrameConstants(
				viewport,
				static_cast<std::uint32_t>(a_request.realFrame),
				a_request.jitterX,
				a_request.jitterY,
				a_request.resetHistory,
				a_request.camera) ||
			!SetDLSSOptions(viewport, legacyContext)) {
			return false;
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
		const sl::ResourceTag tags[]{
			{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
			{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &outputExtent },
			{ &depthResource, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
			{ &motionResource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
			{ &reactiveResource, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eOnlyValidNow, &inputExtent },
			{ &transparencyResource, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eOnlyValidNow, &inputExtent }
		};
		if (SL_FAILED(result, slSetTagForFrame(
			*frameToken,
			viewport,
			tags,
			_countof(tags),
			recording->commandList))) {
			L->error(
				"Could not tag D3D12 DLSS-SR inputs: {}",
				magic_enum::enum_name(result));
			return false;
		}

		const sl::ViewportHandle view(viewport);
		const sl::BaseStructure* inputs[]{ &view };
		if (SL_FAILED(result, slEvaluateFeature(
			sl::kFeatureDLSS,
			*frameToken,
			inputs,
			_countof(inputs),
			recording->commandList))) {
			L->error(
				"D3D12 DLSS-SR evaluation failed: {}",
				magic_enum::enum_name(result));
			return false;
		}
		return true;
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
			reflex.useMarkersToOptimize = true;
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
		if (a_enabled && slDLSSGGetState) {
			sl::DLSSGState state{};
			if (SL_FAILED(result, slDLSSGGetState(viewport, state, &options)) ||
				state.status != sl::DLSSGStatus::eOk) {
				L->error(
					"DLSS-G rejected the active configuration: result={} status={:#x}",
					magic_enum::enum_name(result),
					static_cast<std::uint32_t>(state.status));
				return false;
			}
		}
		return true;
	}

	bool Streamline::TagDLSSGFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		if (!featureDLSSG || !slSetTagForFrame || !slSetConstants ||
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
				"Could not tag DLSS-G inputs: {}",
				magic_enum::enum_name(result));
			return false;
		}
		return true;
	}

	bool Streamline::WaitForDLSSGInputs(
		ID3D12CommandQueue* a_queue) noexcept
	{
		if (!featureDLSSG || !slDLSSGGetState || !a_queue) {
			return !featureDLSSG;
		}
		sl::DLSSGState state{};
		if (slDLSSGGetState(viewport, state, nullptr) != sl::Result::eOk) {
			return false;
		}
		if (state.lastPresentInputsProcessingCompletionFenceValue &&
			state.lastPresentInputsProcessingCompletionFenceValue !=
				_lastDLSSGCountedFenceValue &&
			state.numFramesActuallyPresented > 1) {
			_dlssGGeneratedFrames +=
				state.numFramesActuallyPresented - 1;
			_lastDLSSGCountedFenceValue =
				state.lastPresentInputsProcessingCompletionFenceValue;
		}
		if (!state.inputsProcessingCompletionFence ||
			!state.lastPresentInputsProcessingCompletionFenceValue) {
			return true;
		}
		auto* fence = static_cast<ID3D12Fence*>(
			state.inputsProcessingCompletionFence);
		return SUCCEEDED(a_queue->Wait(
			fence,
			state.lastPresentInputsProcessingCompletionFenceValue));
	}

	std::uint32_t Streamline::ConsumeDLSSGGeneratedFrameCount() noexcept
	{
		return std::exchange(_dlssGGeneratedFrames, 0);
	}

	void Streamline::DestroyDLSSGResources() noexcept
	{
		_dlssGGeneratedFrames = 0;
		_lastDLSSGCountedFenceValue = 0;
		if (slDLSSGSetOptions) {
			sl::DLSSGOptions options{};
			options.mode = sl::DLSSGMode::eOff;
			(void)slDLSSGSetOptions(viewport, options);
		}
		if (slFreeResources) {
			(void)slFreeResources(sl::kFeatureDLSS_G, viewport);
		}
	}

	void Streamline::DestroyDLSSResources()
	{
		cs::engine::WaitForGpuIdle(cs::engine::GetImmediateContext());

		if (!slDLSSSetOptions || !slFreeResources)
			return;

		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = sl::DLSSMode::eOff;

		slDLSSSetOptions(viewport, dlssOptions);
		slFreeResources(sl::kFeatureDLSS, viewport);
	}
}
