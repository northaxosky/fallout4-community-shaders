#include "FidelityFX.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "DX12SwapChain.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/RendererContext.h"
#include "Upscaling.h"

ffxFunctions ffxModule{};

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.fidelityfx");

		FfxResource GetFfxResource(ID3D11Resource* dx11Resource,
			[[maybe_unused]] wchar_t const* ffxResName,
			FfxResourceStates state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ)
		{
			FfxResource resource = {};
			resource.resource = reinterpret_cast<void*>(const_cast<ID3D11Resource*>(dx11Resource));
			resource.state = state;
			resource.description = GetFfxResourceDescriptionDX11(dx11Resource);

#ifdef _DEBUG
			if (ffxResName) {
				wcscpy_s(resource.name, ffxResName);
			}
#endif

			return resource;
		}

	}

	bool FidelityFX::LoadFrameGeneration() try
	{
		if (IsFrameGenerationModuleReady()) {
			return true;
		}

		const auto frameGenerationPath =
			std::filesystem::path(PluginDir) / L"amd_fidelityfx_framegeneration_dx12.dll";
		const auto loaderPath =
			std::filesystem::path(PluginDir) / L"amd_fidelityfx_loader_dx12.dll";
		frameGenerationModule = LoadLibraryW(frameGenerationPath.c_str());
		loaderModule = LoadLibraryW(loaderPath.c_str());
		if (!frameGenerationModule || !loaderModule) {
			L->warn(
				"FidelityFX frame generation is unavailable (framegeneration={}, loader={})",
				frameGenerationModule != nullptr,
				loaderModule != nullptr);
			return false;
		}

		ffxLoadFunctions(&ffxModule, loaderModule);
		const bool exportsReady =
			ffxModule.CreateContext && ffxModule.DestroyContext && ffxModule.Configure &&
			ffxModule.Query && ffxModule.Dispatch;
		if (!exportsReady) {
			L->error("FidelityFX loader is missing required API exports");
			return false;
		}

		L->info("Loaded FidelityFX 3.1.4 DX12 frame-generation provider");
		return true;
	}
	catch (...) {
		L->error("FidelityFX frame-generation module loading failed");
		return false;
	}

	HRESULT FidelityFX::CreateSwapChainContext(
		ID3D12Device* a_device,
		ID3D12CommandQueue* a_queue,
		IDXGIFactory4* a_factory,
		HWND a_window,
		DXGI_SWAP_CHAIN_DESC1& a_desc,
		IDXGISwapChain4** a_swapChain) try
	{
		if (!IsFrameGenerationModuleReady() || !a_device || !a_queue || !a_factory ||
			!a_window || !a_swapChain) {
			return E_INVALIDARG;
		}

		*a_swapChain = nullptr;
		ffx::CreateContextDescFrameGenerationSwapChainForHwndDX12 createDesc{};
		createDesc.desc = &a_desc;
		createDesc.dxgiFactory = a_factory;
		createDesc.fullscreenDesc = nullptr;
		createDesc.gameQueue = a_queue;
		createDesc.hwnd = a_window;
		createDesc.swapchain = a_swapChain;
		if (ffx::CreateContext(swapChainContext, nullptr, createDesc) != ffx::ReturnCode::Ok ||
			!*a_swapChain) {
			L->error("FidelityFX failed to create the DX12 swap-chain context");
			return E_FAIL;
		}
		swapChainContextCreated = true;
		return S_OK;
	}
	catch (...) {
		swapChainContextCreated = false;
		L->error("FidelityFX swap-chain context creation failed");
		return E_FAIL;
	}

	bool FidelityFX::CreateFrameGenerationContext(
		ID3D12Device* a_device,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format) try
	{
		if (frameGenerationContextCreated && !WaitForPresents()) {
			L->error("FidelityFX presents did not quiesce before context recreation");
			return false;
		}
		DestroyFrameGenerationContext();
		if (!swapChainContextCreated || !a_device || !a_width || !a_height ||
			a_format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			return false;
		}

		ffx::CreateContextDescFrameGeneration createDesc{};
		createDesc.displaySize = { a_width, a_height };
		createDesc.maxRenderSize = createDesc.displaySize;
		createDesc.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
		createDesc.backBufferFormat = ffxApiGetSurfaceFormatDX12(a_format);

		ffx::CreateBackendDX12Desc backendDesc{};
		backendDesc.device = a_device;
		if (ffx::CreateContext(frameGenerationContext, nullptr, createDesc, backendDesc) !=
			ffx::ReturnCode::Ok) {
			L->error("FidelityFX failed to create the frame-generation context");
			return false;
		}
		frameGenerationContextCreated = true;
		callbackReset.store(true, std::memory_order_release);
		frameGenerationActive = false;
		frameID.fetch_add(2, std::memory_order_relaxed);
		return true;
	}
	catch (...) {
		frameGenerationContextCreated = false;
		frameGenerationActive = false;
		L->error("FidelityFX frame-generation context creation failed");
		return false;
	}

	void FidelityFX::DestroyFrameGenerationContext() noexcept
	{
		frameGenerationActive = false;
		frameGenerationCameraData = {};
		if (!frameGenerationContextCreated) {
			return;
		}
		try {
			ffx::DestroyContext(frameGenerationContext);
		} catch (...) {
		}
		frameGenerationContext = {};
		frameGenerationContextCreated = false;
	}

	void FidelityFX::DestroySwapChainContext() noexcept
	{
		if (!WaitForPresents()) {
			L->warn("FidelityFX presents did not quiesce before shutdown");
		}
		DestroyFrameGenerationContext();
		if (!swapChainContextCreated) {
			return;
		}
		try {
			ffx::DestroyContext(swapChainContext);
		} catch (...) {
		}
		swapChainContext = {};
		swapChainContextCreated = false;
	}

	bool FidelityFX::CacheFrameGenerationCameraData() noexcept
	{
		frameGenerationCameraData = {};
		const auto& frameBuffer = cs::engine::GetFrameBuffer();
		float verticalFov = 0.0f;
		// Generated frames can be omitted safely; never reuse stale camera data.
		if (!cs::engine::HasUsableWorldCamera(frameBuffer.data) ||
			!TryGetPublishedVerticalFov(frameBuffer, verticalFov)) {
			return false;
		}

		FrameGenerationCameraSnapshot data{};
		const auto basis = cs::engine::GetCameraWorldBasis(frameBuffer.data);
		const auto position = cs::engine::CameraWorldOrigin(frameBuffer.data);
		data.right[0] = basis.right.x;
		data.right[1] = basis.right.y;
		data.right[2] = basis.right.z;
		data.up[0] = basis.up.x;
		data.up[1] = basis.up.y;
		data.up[2] = basis.up.z;
		data.forward[0] = basis.forward.x;
		data.forward[1] = basis.forward.y;
		data.forward[2] = basis.forward.z;
		data.position[0] = position.x;
		data.position[1] = position.y;
		data.position[2] = position.z;
		data.nearPlane = cs::engine::GetCameraNear();
		data.farPlane = cs::engine::GetCameraFar();
		data.verticalFov = verticalFov;
		data.frameCount = frameBuffer.frameCount;
		if (auto* timer = RE::BSTimer::GetSingleton()) {
			data.frameTimeDelta = timer->realTimeDelta * 1000.0f;
		}
		const auto finiteVector = [](const float (&a_vector)[3]) {
			return std::ranges::all_of(a_vector, [](float a_value) {
				return std::isfinite(a_value);
			});
		};
		const auto dot = [](const float (&a_left)[3], const float (&a_right)[3]) {
			return a_left[0] * a_right[0] +
				a_left[1] * a_right[1] +
				a_left[2] * a_right[2];
		};
		const auto approximatelyUnit = [&](const float (&a_axis)[3]) {
			return std::abs(dot(a_axis, a_axis) - 1.0f) <= 0.05f;
		};
		const auto approximatelyOrthogonal =
			[&](const float (&a_left)[3], const float (&a_right)[3]) {
				return std::abs(dot(a_left, a_right)) <= 0.05f;
			};
		if (auto* camera = cs::engine::GetWorldRootCamera()) {
			const auto& frustum = camera->viewFrustum;
			data.frustumAvailable = true;
			data.frustumOrthographic = frustum.ortho;
			const float frustumFov = std::atan(frustum.top) - std::atan(frustum.bottom);
			if (std::isfinite(frustumFov)) {
				data.frustumVerticalFov = frustumFov;
			}
			const auto& frustumPosition = camera->GetWorldTranslate();
			const float deltaX = frustumPosition.x - position.x;
			const float deltaY = frustumPosition.y - position.y;
			const float deltaZ = frustumPosition.z - position.z;
			data.frustumCameraOffset =
				std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
		}
		data.valid = finiteVector(data.right) &&
			finiteVector(data.up) &&
			finiteVector(data.forward) &&
			finiteVector(data.position) &&
			approximatelyUnit(data.right) &&
			approximatelyUnit(data.up) &&
			approximatelyUnit(data.forward) &&
			approximatelyOrthogonal(data.right, data.up) &&
			approximatelyOrthogonal(data.right, data.forward) &&
			approximatelyOrthogonal(data.up, data.forward) &&
			std::isfinite(data.nearPlane) &&
			std::isfinite(data.farPlane) &&
			std::isfinite(data.verticalFov) &&
			std::isfinite(data.frameTimeDelta) &&
			data.nearPlane > 0.0f &&
			data.farPlane > data.nearPlane &&
			data.frameTimeDelta >= 0.0f;
		if (!data.valid) {
			return false;
		}
		frameGenerationCameraData = data;
		return true;
	}

	void FidelityFX::ResetFrameGenerationCameraData() noexcept
	{
		frameGenerationCameraData = {};
	}

	bool FidelityFX::WaitForPresents() noexcept
	{
		if (!swapChainContextCreated) {
			return true;
		}
		try {
			ffx::DispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait{};
			if (ffx::Dispatch(swapChainContext, wait) != ffx::ReturnCode::Ok) {
				L->error("FidelityFX failed to wait for pending presents");
				return false;
			}
			return true;
		} catch (...) {
			L->error("FidelityFX present wait raised an exception");
			return false;
		}
	}

	bool FidelityFX::PresentFrameGeneration(
		DX12SwapChain& a_swapChain,
		bool a_enable,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight) try
	{
		if (!frameGenerationContextCreated) {
			frameGenerationActive = false;
			return false;
		}

		const bool useFrameGeneration =
			a_enable && a_renderWidth > 0 && a_renderHeight > 0 &&
			frameGenerationCameraData.valid &&
			a_swapChain.GetHudlessTexture() && a_swapChain.GetDepthTexture() &&
			a_swapChain.GetMotionTexture() && a_swapChain.GetCommandList();
		const auto currentFrameID = frameID.fetch_add(1, std::memory_order_relaxed);

		ffx::ConfigureDescFrameGeneration config{};
		config.frameGenerationEnabled = useFrameGeneration;
		config.frameGenerationCallbackUserContext =
			useFrameGeneration ? &frameGenerationContext : nullptr;
		config.frameGenerationCallback = useFrameGeneration
			? [](ffxDispatchDescFrameGeneration* a_params, void* a_context) -> ffxReturnCode_t {
				a_params->backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
				if (callbackReset.exchange(false, std::memory_order_acq_rel)) {
					a_params->reset = true;
				}
				return ffxModule.Dispatch(
					reinterpret_cast<ffxContext*>(a_context),
					&a_params->header);
			}
			: nullptr;
		config.HUDLessColor = useFrameGeneration
			? ffxApiGetResourceDX12(a_swapChain.GetHudlessTexture()->resource12.get())
			: FfxApiResource({});
		config.presentCallback = nullptr;
		config.presentCallbackUserContext = nullptr;
		config.frameID = currentFrameID;
		config.swapChain = a_swapChain.GetInnerSwapChain();
		config.onlyPresentGenerated = false;
		config.flags = 0;
		config.allowAsyncWorkloads = true;
		config.generationRect.left = 0;
		config.generationRect.top = 0;
		config.generationRect.width = static_cast<std::int32_t>(a_swapChain.GetWidth());
		config.generationRect.height = static_cast<std::int32_t>(a_swapChain.GetHeight());
		const auto disableConfiguredGeneration = [&]() {
			config.frameGenerationEnabled = false;
			config.frameGenerationCallback = nullptr;
			config.frameGenerationCallbackUserContext = nullptr;
			config.HUDLessColor = FfxApiResource({});
			ffx::Configure(frameGenerationContext, config);
			frameGenerationActive = false;
		};

		const auto configureResult = ffx::Configure(frameGenerationContext, config);
		if (configureResult != ffx::ReturnCode::Ok) {
			L->error("FidelityFX failed to configure frame generation; generated frames are disabled");
			disableConfiguredGeneration();
			return false;
		}

		ffx::ConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 uiConfig{};
		uiConfig.uiResource = FfxApiResource({});
		uiConfig.flags = 0;
		const auto uiConfigureResult = ffx::Configure(swapChainContext, uiConfig);
		if (uiConfigureResult != ffx::ReturnCode::Ok) {
			L->error("FidelityFX failed to clear the registered UI resource");
			disableConfiguredGeneration();
			return false;
		}

		if (useFrameGeneration) {
			auto* upscaling = Upscaling::GetSingleton();
			ffx::DispatchDescFrameGenerationPrepare prepare{};
			prepare.commandList = a_swapChain.GetCommandList();
			prepare.motionVectorScale = {
				static_cast<float>(a_renderWidth),
				static_cast<float>(a_renderHeight)
			};
			prepare.renderSize = { a_renderWidth, a_renderHeight };
			prepare.jitterOffset = { -upscaling->jitter.x, -upscaling->jitter.y };
			prepare.frameTimeDelta = frameGenerationCameraData.frameTimeDelta;
			prepare.cameraFar = frameGenerationCameraData.farPlane;
			prepare.cameraNear = frameGenerationCameraData.nearPlane;
			prepare.cameraFovAngleVertical = frameGenerationCameraData.verticalFov;
			prepare.viewSpaceToMetersFactor = 0.01428222656f;
			prepare.frameID = currentFrameID;
			prepare.depth = ffxApiGetResourceDX12(a_swapChain.GetDepthTexture()->resource12.get());
			prepare.motionVectors =
				ffxApiGetResourceDX12(a_swapChain.GetMotionTexture()->resource12.get());

			ffx::DispatchDescFrameGenerationPrepareCameraInfo camera{};
			std::copy_n(frameGenerationCameraData.right, 3, camera.cameraRight);
			std::copy_n(frameGenerationCameraData.up, 3, camera.cameraUp);
			std::copy_n(frameGenerationCameraData.forward, 3, camera.cameraForward);
			std::copy_n(frameGenerationCameraData.position, 3, camera.cameraPosition);

			const auto prepareResult = ffx::Dispatch(frameGenerationContext, prepare, camera);
			if (prepareResult != ffx::ReturnCode::Ok) {
				L->error("FidelityFX frame-generation prepare dispatch failed");
				disableConfiguredGeneration();
				return false;
			}
		}

		frameGenerationActive = useFrameGeneration;
		return true;
	}
	catch (...) {
		frameGenerationActive = false;
		L->error("FidelityFX frame-generation dispatch raised an exception");
		return false;
	}

	void FidelityFX::RequestFrameGenerationReset() noexcept
	{
		callbackReset.store(true, std::memory_order_release);
		frameID.fetch_add(2, std::memory_order_relaxed);
	}

	bool FidelityFX::IsFrameGenerationModuleReady() const noexcept
	{
		return frameGenerationModule && loaderModule && ffxModule.CreateContext &&
			ffxModule.DestroyContext && ffxModule.Configure && ffxModule.Query && ffxModule.Dispatch;
	}

	bool FidelityFX::IsFrameGenerationContextReady() const noexcept
	{
		return frameGenerationContextCreated;
	}

	bool FidelityFX::IsFrameGenerationActive() const noexcept
	{
		return frameGenerationActive;
	}

	bool FidelityFX::CreateFSRResources()
	{
		if (fsrScratchBuffer) {
			L->warn("FSR resources already created, skipping allocation");
			return contextCreated;
		}

		auto* device = cs::engine::GetDevice();
		auto* graphicsState = cs::engine::GetGraphicsState();
		if (!device || !graphicsState) {
			L->error("FSR resource creation ran before the renderer was ready");
			return false;
		}

		auto fsrDevice = ffxGetDeviceDX11(device);

		uint32_t numContexts = 1;
		size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(numContexts);
		fsrScratchBuffer = calloc(scratchBufferSize, 1);
		if (!fsrScratchBuffer) {
			L->critical("Failed to allocate FSR3 scratch buffer memory!");
			return false;
		}
		FfxInterface fsrInterface;
		if (const auto interfaceResult =
				ffxGetInterfaceDX11(&fsrInterface, fsrDevice, fsrScratchBuffer, scratchBufferSize, numContexts);
			interfaceResult != FFX_OK) {
			L->critical("Failed to initialize FSR3 backend interface! FfxErrorCode {:#010x}",
				static_cast<std::uint32_t>(interfaceResult));
			free(fsrScratchBuffer);
			fsrScratchBuffer = nullptr;
			return false;
		}

		const auto [renderWidth, renderHeight] = Upscaling::GetSingleton()->GetRenderSize();
		const auto displayWidth = graphicsState->screenWidth;
		const auto displayHeight = graphicsState->screenHeight;

		FfxFsr3ContextDescription contextDescription{};
		contextDescription.maxRenderSize.width = renderWidth;
		contextDescription.maxRenderSize.height = renderHeight;
		contextDescription.maxUpscaleSize.width = displayWidth;
		contextDescription.maxUpscaleSize.height = displayHeight;
		contextDescription.displaySize.width = displayWidth;
		contextDescription.displaySize.height = displayHeight;
		contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE;
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
		contextDescription.backendInterfaceUpscaling = fsrInterface;

		if (const auto createResult = ffxFsr3ContextCreate(&fsrContext, &contextDescription);
			createResult != FFX_OK) {
			L->critical(
				"Failed to initialize FSR3 context! FfxErrorCode {:#010x} (display {}x{}, render {}x{})",
				static_cast<std::uint32_t>(createResult),
				displayWidth, displayHeight, renderWidth, renderHeight);
			free(fsrScratchBuffer);
			fsrScratchBuffer = nullptr;
			return false;
		}
		contextCreated = true;
		L->info("Created FSR3 context (Display: {}x{}, Render: {}x{})",
			displayWidth, displayHeight, renderWidth, renderHeight);
		return true;
	}

	void FidelityFX::DestroyFSRResources()
	{
		cs::engine::WaitForGpuIdle(cs::engine::GetImmediateContext());

		if (!fsrScratchBuffer)
			return;

		if (contextCreated && ffxFsr3ContextDestroy(&fsrContext) != FFX_OK)
			L->critical("Failed to destroy FSR3 context!");
		contextCreated = false;

		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;

		fsrDispatchCrashLogged = false;
	}

	bool FidelityFX::Upscale(
		ID3D11Resource* a_upscalingTexture,
		ID3D11Resource* a_reactiveMask,
		ID3D11Resource* a_transparencyCompositionMask,
		ID3D11Resource* a_motionVectors,
		float a_sharpness,
		bool a_resetHistory)
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* depthTexture = cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		if (!context || !depthTexture || !contextCreated)
			return false;

		const auto& frameBuffer = cs::engine::GetFrameBuffer();
		float verticalFov = 0.0f;
		// Native TAA may already be suppressed, so a transient miss must not decline the resolve.
		const auto fovSource = superResolutionFovCache.Resolve(frameBuffer, verticalFov);
		if (fovSource == SuperResolutionFovSource::kUnavailable) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"FSR3 super-resolution skipped: no current or cached camera projection is available.");
			return false;
		}
		if (fovSource == SuperResolutionFovSource::kCached) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"FSR3 super-resolution is using the last valid camera FOV.");
		}

		auto* upscaling = Upscaling::GetSingleton();
		const auto [renderWidth, renderHeight] = upscaling->GetRenderSize();
		auto jitter = upscaling->jitter;

		FfxFsr3DispatchUpscaleDescription dispatchParameters{};
		dispatchParameters.commandList = ffxGetCommandListDX11(context);
		dispatchParameters.color = GetFfxResource(a_upscalingTexture, L"FSR3_Input_OutputColor");
		dispatchParameters.depth = GetFfxResource(depthTexture, L"FSR3_InputDepth");
		dispatchParameters.motionVectors = GetFfxResource(a_motionVectors, L"FSR3_InputMotionVectors");
		dispatchParameters.exposure = GetFfxResource(nullptr, L"FSR3_InputExposure");
		dispatchParameters.upscaleOutput = GetFfxResource(a_upscalingTexture, L"FSR3_OutputColor");
		dispatchParameters.reactive = GetFfxResource(a_reactiveMask, L"FSR3_InputReactiveMap");
		dispatchParameters.transparencyAndComposition = GetFfxResource(a_transparencyCompositionMask, L"FSR3_TransparencyAndCompositionMap");

		dispatchParameters.motionVectorScale.x = static_cast<float>(renderWidth);
		dispatchParameters.motionVectorScale.y = static_cast<float>(renderHeight);
		dispatchParameters.renderSize.width = renderWidth;
		dispatchParameters.renderSize.height = renderHeight;

		dispatchParameters.jitterOffset.x = -jitter.x;
		dispatchParameters.jitterOffset.y = -jitter.y;

		auto* timer = RE::BSTimer::GetSingleton();
		dispatchParameters.frameTimeDelta = (timer ? timer->realTimeDelta : 0.0f) * 1000.f;
		dispatchParameters.cameraFar = cs::engine::GetCameraFar();
		dispatchParameters.cameraNear = cs::engine::GetCameraNear();
		dispatchParameters.enableSharpening = true;
		dispatchParameters.sharpness = a_sharpness;
		dispatchParameters.cameraFovAngleVertical = verticalFov;
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
		dispatchParameters.reset = a_resetHistory;
		dispatchParameters.preExposure = 1.0f;
		dispatchParameters.flags = 0;

		bool dispatched = false;
		__try {
			if (ffxFsr3ContextDispatchUpscale(&fsrContext, &dispatchParameters) != FFX_OK) {
				L->critical("Failed to dispatch upscaling!");
			} else {
				dispatched = true;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			if (!fsrDispatchCrashLogged) {
				L->critical("FSR3 dispatch crashed; disable RenderDoc capture before retrying");
				fsrDispatchCrashLogged = true;
			}
		}

		return dispatched;
	}
}
