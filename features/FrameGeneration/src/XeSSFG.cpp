#include "XeSSFG.h"
#include "FrameGeneration.h"

#include "Log.h"

#define LOAD_FN(module, name) pfn_##name = reinterpret_cast<decltype(&name)>(GetProcAddress(module, #name))

namespace cs::features::framegeneration
{
	namespace {
		auto* L         = cs::log::Get("cs.feature.framegeneration.xess");
		auto* kInternal = cs::log::Get("cs.feature.framegeneration.xess.internal");
	}

XeSSFG::~XeSSFG()
{
	Shutdown();
}

void XeSSFG::Shutdown()
{
	initialized = false;
	if (xefgCtx && pfn_xefgSwapChainDestroy) {
		pfn_xefgSwapChainDestroy(xefgCtx);
		xefgCtx = nullptr;
	}
	if (xellCtx && pfn_xellDestroyContext) {
		pfn_xellDestroyContext(xellCtx);
		xellCtx = nullptr;
	}
	if (xellModule) {
		FreeLibrary(xellModule);
		xellModule = nullptr;
	}
	if (fgModule) {
		FreeLibrary(fgModule);
		fgModule = nullptr;
	}

	pfn_xellD3D12CreateContext = nullptr;
	pfn_xellDestroyContext = nullptr;
	pfn_xellSetSleepMode = nullptr;
	pfn_xellAddMarkerData = nullptr;
	pfn_xellSetLoggingCallback = nullptr;
	pfn_xefgSwapChainD3D12CreateContext = nullptr;
	pfn_xefgSwapChainSetLatencyReduction = nullptr;
	pfn_xefgSwapChainSetLoggingCallback = nullptr;
	pfn_xefgSwapChainD3D12InitFromSwapChainDesc = nullptr;
	pfn_xefgSwapChainD3D12GetSwapChainPtr = nullptr;
	pfn_xefgSwapChainSetEnabled = nullptr;
	pfn_xefgSwapChainGetProperties = nullptr;
	pfn_xefgSwapChainGetLastPresentStatus = nullptr;
	pfn_xefgSwapChainD3D12TagFrameResource = nullptr;
	pfn_xefgSwapChainTagFrameConstants = nullptr;
	pfn_xefgSwapChainSetPresentId = nullptr;
	pfn_xefgSwapChainDestroy = nullptr;
}

bool XeSSFG::LoadLibraries()
{
	fgModule = LoadLibrary(L"Data\\F4SE\\Plugins\\FrameGeneration\\XeSS\\libxess_fg.dll");
	if (!fgModule) {
		L->warn("Failed to load libxess_fg.dll: {:#x}", GetLastError());
		return false;
	}

	xellModule = LoadLibrary(L"Data\\F4SE\\Plugins\\FrameGeneration\\XeSS\\libxell.dll");
	if (!xellModule) {
		L->warn("Failed to load libxell.dll: {:#x}", GetLastError());
		FreeLibrary(fgModule);
		fgModule = nullptr;
		return false;
	}

	LOAD_FN(xellModule, xellD3D12CreateContext);
	LOAD_FN(xellModule, xellDestroyContext);
	LOAD_FN(xellModule, xellSetSleepMode);
	LOAD_FN(xellModule, xellAddMarkerData);
	LOAD_FN(xellModule, xellSetLoggingCallback);

	LOAD_FN(fgModule, xefgSwapChainD3D12CreateContext);
	LOAD_FN(fgModule, xefgSwapChainSetLatencyReduction);
	LOAD_FN(fgModule, xefgSwapChainSetLoggingCallback);
	LOAD_FN(fgModule, xefgSwapChainD3D12InitFromSwapChainDesc);
	LOAD_FN(fgModule, xefgSwapChainD3D12GetSwapChainPtr);
	LOAD_FN(fgModule, xefgSwapChainSetEnabled);
	LOAD_FN(fgModule, xefgSwapChainGetProperties);
	LOAD_FN(fgModule, xefgSwapChainGetLastPresentStatus);
	LOAD_FN(fgModule, xefgSwapChainD3D12TagFrameResource);
	LOAD_FN(fgModule, xefgSwapChainTagFrameConstants);
	LOAD_FN(fgModule, xefgSwapChainSetPresentId);
	LOAD_FN(fgModule, xefgSwapChainDestroy);

	if (!pfn_xellD3D12CreateContext ||
		!pfn_xellDestroyContext ||
		!pfn_xefgSwapChainD3D12CreateContext ||
		!pfn_xefgSwapChainD3D12InitFromSwapChainDesc ||
		!pfn_xefgSwapChainD3D12GetSwapChainPtr ||
		!pfn_xefgSwapChainSetEnabled ||
		!pfn_xefgSwapChainDestroy) {
		L->warn("Failed to resolve required function pointers");
		Shutdown();
		return false;
	}

	L->info("Libraries loaded, functions resolved");
	return true;
}

#undef LOAD_FN

bool XeSSFG::CreateContexts(ID3D12Device* a_device)
{
	if (!fgModule || !xellModule) return false;

	auto xellResult = pfn_xellD3D12CreateContext(a_device, &xellCtx);
	if (xellResult != XELL_RESULT_SUCCESS) {
		L->warn("XeLL context creation failed: {}", (int)xellResult);
		Shutdown();
		return false;
	}
	L->info("XeLL context created");

	auto xefgResult = pfn_xefgSwapChainD3D12CreateContext(a_device, &xefgCtx);
	if (xefgResult != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		L->warn("Context creation failed: {}", (int)xefgResult);
		Shutdown();
		return false;
	}
	L->info("Context created");

	if (pfn_xefgSwapChainSetLatencyReduction) {
		xefgResult = pfn_xefgSwapChainSetLatencyReduction(xefgCtx, xellCtx);
		if (xefgResult != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			L->warn("Failed to set latency reduction: {}", (int)xefgResult);
		}
	}

	auto debugLogging = FrameGeneration::GetSingleton()->settings.debugLogging;
	if (debugLogging) {
		if (pfn_xefgSwapChainSetLoggingCallback) {
			pfn_xefgSwapChainSetLoggingCallback(xefgCtx,
				XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG,
				[](const char* msg, xefg_swapchain_logging_level_t, void*) {
					kInternal->info("{}", msg);
				}, nullptr);
		}
		if (pfn_xellSetLoggingCallback) {
			pfn_xellSetLoggingCallback(xellCtx,
				XELL_LOGGING_LEVEL_DEBUG,
				[](const char* msg, xell_logging_level_t) {
					kInternal->info("{}", msg);
				});
		}
	}

	if (pfn_xellSetSleepMode) {
		xell_sleep_params_t sleepParams{};
		sleepParams.bLowLatencyMode = 1;
		sleepParams.minimumIntervalUs = 0;
		pfn_xellSetSleepMode(xellCtx, &sleepParams);
	}

	if (pfn_xefgSwapChainGetProperties) {
		xefg_swapchain_properties_t props{};
		if (pfn_xefgSwapChainGetProperties(xefgCtx, &props) == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			L->info("Max supported interpolations: {}", props.maxSupportedInterpolations);
		}
	}

	return true;
}

bool XeSSFG::InitSwapChain(ID3D12CommandQueue* a_cmdQueue, IDXGIFactory5* a_factory,
	const DXGI_SWAP_CHAIN_DESC1& a_desc, HWND a_hwnd, IDXGISwapChain4** a_outSwapChain)
{
	if (!xefgCtx || !pfn_xefgSwapChainD3D12InitFromSwapChainDesc) return false;

	xefg_swapchain_d3d12_init_params_t initParams{};
	initParams.pApplicationSwapChain = nullptr;
	initParams.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;
	initParams.maxInterpolatedFrames = XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES;
	initParams.creationNodeMask = 0;
	initParams.visibleNodeMask = 0;
	initParams.pTempBufferHeap = nullptr;
	initParams.bufferHeapOffset = 0;
	initParams.pTempTextureHeap = nullptr;
	initParams.textureHeapOffset = 0;
	initParams.pPipelineLibrary = nullptr;
	initParams.uiMode = XEFG_SWAPCHAIN_UI_MODE_AUTO;

	auto result = pfn_xefgSwapChainD3D12InitFromSwapChainDesc(
		xefgCtx, a_hwnd, &a_desc, nullptr,
		a_cmdQueue, (IDXGIFactory2*)a_factory, &initParams);

	if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		L->warn("Swap chain init failed: {}", (int)result);
		Shutdown();
		return false;
	}

	result = pfn_xefgSwapChainD3D12GetSwapChainPtr(xefgCtx, IID_PPV_ARGS(a_outSwapChain));
	if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		L->warn("Failed to get proxy swap chain: {}", (int)result);
		Shutdown();
		return false;
	}

	pfn_xefgSwapChainSetEnabled(xefgCtx, 1);

	initialized = true;
	L->info("Initialized and enabled");
	return true;
}

void XeSSFG::BeginFrame(uint32_t a_frameId)
{
	if (!xellCtx) return;

	// XeLL caps cross-vendor NVIDIA rendering at 60 FPS.
	if (pfn_xellAddMarkerData)
		pfn_xellAddMarkerData(xellCtx, a_frameId, XELL_SIMULATION_START);
}

void XeSSFG::SetMarker(xell_latency_marker_type_t a_marker, uint32_t a_frameId)
{
	if (!xellCtx || !pfn_xellAddMarkerData) return;
	pfn_xellAddMarkerData(xellCtx, a_frameId, a_marker);
}

void XeSSFG::TagResources(uint32_t a_frameId,
	ID3D12GraphicsCommandList* a_cmdList,
	ID3D12Resource* a_depth,
	ID3D12Resource* a_motionVectors,
	ID3D12Resource* a_hudlessColor,
	float2 a_screenSize, float2 a_jitter,
	float a_frameTimeDelta,
	const float* a_viewMatrix, const float* a_projMatrix,
	bool a_reset)
{
	if (!xefgCtx || !pfn_xefgSwapChainD3D12TagFrameResource) return;

	// HUD-less color improves interpolation.
	if (a_hudlessColor) {
		xefg_swapchain_d3d12_resource_data_t colorData{};
		colorData.type = XEFG_SWAPCHAIN_RES_HUDLESS_COLOR;
		colorData.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
		colorData.resourceBase = { 0, 0 };
		colorData.resourceSize = { (uint32_t)a_screenSize.x, (uint32_t)a_screenSize.y };
		colorData.pResource = a_hudlessColor;
		colorData.incomingState = D3D12_RESOURCE_STATE_COMMON;
		pfn_xefgSwapChainD3D12TagFrameResource(xefgCtx, nullptr, a_frameId, &colorData);
	}

	// Tag motion vectors while they remain valid.
	if (a_motionVectors) {
		xefg_swapchain_d3d12_resource_data_t mvData{};
		mvData.type = XEFG_SWAPCHAIN_RES_MOTION_VECTOR;
		mvData.validity = XEFG_SWAPCHAIN_RV_ONLY_NOW;
		mvData.resourceBase = { 0, 0 };
		mvData.resourceSize = { (uint32_t)a_screenSize.x, (uint32_t)a_screenSize.y };
		mvData.pResource = a_motionVectors;
		mvData.incomingState = D3D12_RESOURCE_STATE_COMMON;
		pfn_xefgSwapChainD3D12TagFrameResource(xefgCtx, a_cmdList, a_frameId, &mvData);
	}

	// Tag depth while it remains valid.
	if (a_depth) {
		xefg_swapchain_d3d12_resource_data_t depthData{};
		depthData.type = XEFG_SWAPCHAIN_RES_DEPTH;
		depthData.validity = XEFG_SWAPCHAIN_RV_ONLY_NOW;
		depthData.resourceBase = { 0, 0 };
		depthData.resourceSize = { (uint32_t)a_screenSize.x, (uint32_t)a_screenSize.y };
		depthData.pResource = a_depth;
		depthData.incomingState = D3D12_RESOURCE_STATE_COMMON;
		pfn_xefgSwapChainD3D12TagFrameResource(xefgCtx, a_cmdList, a_frameId, &depthData);
	}

	// XeSS-FG expects normalized pixel-space motion vectors.
	if (pfn_xefgSwapChainTagFrameConstants) {
		xefg_swapchain_frame_constant_data_t constants{};

		if (a_viewMatrix)
			memcpy(constants.viewMatrix, a_viewMatrix, sizeof(float) * 16);
		if (a_projMatrix)
			memcpy(constants.projectionMatrix, a_projMatrix, sizeof(float) * 16);

		constants.jitterOffsetX = a_jitter.x;
		constants.jitterOffsetY = a_jitter.y;
		constants.motionVectorScaleX = 1.0f;
		constants.motionVectorScaleY = 1.0f;
		constants.resetHistory = a_reset ? 1u : 0u;
		constants.frameRenderTime = a_frameTimeDelta;

		pfn_xefgSwapChainTagFrameConstants(xefgCtx, a_frameId, &constants);
	}

	if (pfn_xefgSwapChainSetPresentId)
		pfn_xefgSwapChainSetPresentId(xefgCtx, a_frameId);
}

void XeSSFG::SetEnabled(uint32_t a_enabled)
{
	if (xefgCtx && pfn_xefgSwapChainSetEnabled)
		pfn_xefgSwapChainSetEnabled(xefgCtx, a_enabled);
}

uint32_t XeSSFG::ConsumeFramesPresented()
{
	if (!xefgCtx || !pfn_xefgSwapChainGetLastPresentStatus)
		return 0;
	xefg_swapchain_present_status_t status{};
	if (pfn_xefgSwapChainGetLastPresentStatus(xefgCtx, &status) != XEFG_SWAPCHAIN_RESULT_SUCCESS)
		return 0;
	return status.framesPresented;
}

}
