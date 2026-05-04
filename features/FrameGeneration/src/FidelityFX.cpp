#include "FidelityFX.h"

#include "Upscaling.h"

#include "DX12SwapChain.h"
#include <dx12/ffx_api_dx12.hpp>

ffxFunctions ffxModule;

namespace cs::features::FrameGeneration
{

void FidelityFX::LoadFFX()
{
	module = LoadLibrary(L"Data\\F4SE\\Plugins\\FrameGeneration\\FidelityFX\\amd_fidelityfx_dx12.dll");

	if (module)
		ffxLoadFunctions(&ffxModule, module);
}

void FidelityFX::SetupFrameGeneration()
{
	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { dx12SwapChain->swapChainDesc.Width, dx12SwapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(dx12SwapChain->swapChainDesc.Format);

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = dx12SwapChain->d3d12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, createBackend) != ffx::ReturnCode::Ok) {
		REX::CRITICAL("[FidelityFX] Failed to create frame generation context!");
	}
}

[[nodiscard]] static RE::BSGraphics::State* State_GetSingleton()
{
	REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID({ 600795, 2704621, 2704621 }) };
	return singleton.get();
}

[[nodiscard]] static RE::BSGraphics::RenderTargetManager* RenderTargetManager_GetSingleton()
{
	REL::Relocation<RE::BSGraphics::RenderTargetManager*> singleton{ REL::ID({ 1508457, 2666735, 2666735 }) };
	return singleton.get();
}

void FidelityFX::Present(bool a_useFrameGeneration)
{
	auto upscaling = Upscaling::GetSingleton();
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	auto commandList = dx12SwapChain->commandLists[dx12SwapChain->frameIndex].get();
	
	auto HUDLessColor = upscaling->HUDLessBufferShared12[dx12SwapChain->frameIndex].get();
	auto depth = upscaling->depthBufferShared12[dx12SwapChain->frameIndex].get();
	auto motionVectors = upscaling->motionVectorBufferShared12[dx12SwapChain->frameIndex].get();

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (a_useFrameGeneration) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
			};
		configParameters.frameGenerationCallbackUserContext = &frameGenContext;

		configParameters.HUDLessColor = ffxApiGetResourceDX12(HUDLessColor);

	}
	else {
		configParameters.frameGenerationEnabled = false;

		configParameters.frameGenerationCallbackUserContext = nullptr;
		configParameters.frameGenerationCallback = nullptr;

		configParameters.HUDLessColor = FfxApiResource({});
	}

	configParameters.presentCallback = nullptr;
	configParameters.presentCallbackUserContext = nullptr;

	static uint64_t frameID = 0;
	configParameters.frameID = frameID;
	configParameters.swapChain = dx12SwapChain->swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.allowAsyncWorkloads = true;
	configParameters.flags = 0;

	// Full-screen generation rect starts at origin
	configParameters.generationRect.left = 0;
	configParameters.generationRect.top = 0;
	configParameters.generationRect.width = dx12SwapChain->swapChainDesc.Width;
	configParameters.generationRect.height = dx12SwapChain->swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		REX::CRITICAL("[FidelityFX] Failed to configure frame generation!");
	}

	static LARGE_INTEGER frequency = []() {
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		return freq;
		}();

	static LARGE_INTEGER lastFrameTime = []() {
		LARGE_INTEGER time;
		QueryPerformanceCounter(&time);
		return time;
		}();

	LARGE_INTEGER currentFrameTime;
	QueryPerformanceCounter(&currentFrameTime);

	float deltaTime = static_cast<float>(currentFrameTime.QuadPart - lastFrameTime.QuadPart) / static_cast<float>(frequency.QuadPart);
	
	lastFrameTime = currentFrameTime;

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = commandList;

		static auto gameViewport = State_GetSingleton();
		static auto renderTargetManager = RenderTargetManager_GetSingleton();

		auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
		auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

		dispatchParameters.motionVectorScale.x = renderSize.x;
		dispatchParameters.motionVectorScale.y = renderSize.y;
		dispatchParameters.renderSize.width = static_cast<uint>(renderSize.x);
		dispatchParameters.renderSize.height = static_cast<uint>(renderSize.y);
		
		float2 jitter;
		jitter.x = -gameViewport->offsetX * screenSize.x / 2.0f;
		jitter.y = gameViewport->offsetY * screenSize.y / 2.0f;

		dispatchParameters.jitterOffset.x = -jitter.x / renderTargetManager->dynamicWidthRatio;
		dispatchParameters.jitterOffset.y = -jitter.y / renderTargetManager->dynamicHeightRatio;

		dispatchParameters.frameTimeDelta = deltaTime * 1000.f;

		dispatchParameters.cameraNear = *(float*)REL::ID({ 57985, 2712882, 2712882 }).address();
		dispatchParameters.cameraFar = *(float*)REL::ID({ 958877, 2712883, 2712883 }).address();

		dispatchParameters.cameraFovAngleVertical = 1.0f;
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;

		dispatchParameters.frameID = frameID;

		dispatchParameters.depth = ffxApiGetResourceDX12(depth);
		dispatchParameters.motionVectors = ffxApiGetResourceDX12(motionVectors);

		if (ffx::Dispatch(frameGenContext, dispatchParameters) != ffx::ReturnCode::Ok) {
			REX::CRITICAL("[FidelityFX] Failed to dispatch frame generation!");
		}
	}

	frameID++;
}

}
