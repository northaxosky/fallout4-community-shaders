#include "FidelityFX.h"

#include "Log.h"
#include "Upscaling.h"
#include "Util.h"

namespace cs::features::upscaling
{
	namespace { auto* L = cs::log::Get("cs.feature.upscaling.fsr"); }

FfxResource ffxGetResource(ID3D11Resource* dx11Resource,
	[[maybe_unused]] wchar_t const* ffxResName,
	FfxResourceStates state /*=FFX_RESOURCE_STATE_COMPUTE_READ*/)
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

void FidelityFX::CreateFSRResources()
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	static auto gameViewport = Util::State_GetSingleton();

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	auto fsrDevice = ffxGetDeviceDX11(device);

	size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(FFX_FSR3UPSCALER_CONTEXT_COUNT);
	fsrScratchBuffer = calloc(scratchBufferSize, 1);
	if (!fsrScratchBuffer) {
		L->critical("Failed to allocate FSR3 scratch buffer memory!");
		return;
	}
	memset(fsrScratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface{};
	if (ffxGetInterfaceDX11(&fsrInterface, fsrDevice, fsrScratchBuffer, scratchBufferSize, FFX_FSR3UPSCALER_CONTEXT_COUNT) != FFX_OK) {
		L->critical("Failed to initialize FSR3 backend interface!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		return;
	}

	FfxFsr3ContextDescription contextDescription{};
	contextDescription.maxRenderSize.width = gameViewport->screenWidth;
	contextDescription.maxRenderSize.height = gameViewport->screenHeight;
	contextDescription.maxUpscaleSize.width = gameViewport->screenWidth;
	contextDescription.maxUpscaleSize.height = gameViewport->screenHeight;
	contextDescription.displaySize.width = gameViewport->screenWidth;
	contextDescription.displaySize.height = gameViewport->screenHeight;
	contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY;
	contextDescription.backendInterfaceUpscaling = fsrInterface;

	auto renderer = RE::BSGraphics::GetRendererData();
	auto& main = renderer->renderTargets[(uint)Util::RenderTarget::kMainTemp];

	D3D11_TEXTURE2D_DESC texDesc{};
	reinterpret_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&texDesc);
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	colorOpaqueOnlyTexture = std::make_unique<Texture2D>(texDesc);

	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	reactiveMaskTexture = std::make_unique<Texture2D>(texDesc);

	if (ffxFsr3ContextCreate(&fsrContext, &contextDescription) != FFX_OK) {
		L->critical("Failed to initialize FSR3 context!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		return;
	}

	L->info("FSR3 context created: maxRender={}x{}, maxUpscale={}x{}, display={}x{}",
		contextDescription.maxRenderSize.width, contextDescription.maxRenderSize.height,
		contextDescription.maxUpscaleSize.width, contextDescription.maxUpscaleSize.height,
		contextDescription.displaySize.width, contextDescription.displaySize.height);
}

void FidelityFX::DestroyFSRResources()
{
	if (ffxFsr3ContextDestroy(&fsrContext) != FFX_OK)
		L->critical("Failed to destroy FSR3 context!");

	free(fsrScratchBuffer);
	fsrScratchBuffer = nullptr;

	colorOpaqueOnlyTexture.reset();
	reactiveMaskTexture.reset();
}

void FidelityFX::CopyOpaqueTexture()
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	auto mainTexture = reinterpret_cast<ID3D11Texture2D*>(rendererData->renderTargets[(uint)Util::RenderTarget::kMainTemp].texture);

	context->CopyResource(colorOpaqueOnlyTexture->resource.get(), mainTexture);
}

#define FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_APPLY_TONEMAP                                    1
#define FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_APPLY_INVERSETONEMAP                             2
#define FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_APPLY_THRESHOLD                                  4
#define FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX                               8

void FidelityFX::GenerateReactiveMask()
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto mainTexture = reinterpret_cast<ID3D11Texture2D*>(rendererData->renderTargets[(uint)Util::RenderTarget::kMainTemp].texture);

	FfxFsr3GenerateReactiveDescription dispatchParameters{};

	dispatchParameters.commandList = ffxGetCommandListDX11(context);

	dispatchParameters.colorOpaqueOnly = ffxGetResource(colorOpaqueOnlyTexture->resource.get(), L"FSR3_Input_ColorOpaqueOnly", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchParameters.colorPreUpscale = ffxGetResource(mainTexture, L"FSR3_Input_ColorPreUpscale", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchParameters.outReactive = ffxGetResource(reactiveMaskTexture->resource.get(), L"FSR3_Output_OutReactive", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	static auto gameViewport = Util::State_GetSingleton();
	static auto renderTargetManager = Util::RenderTargetManager_GetSingleton();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * Util::GetGameDynamicWidthRatio(renderTargetManager), screenSize.y * Util::GetGameDynamicHeightRatio(renderTargetManager));

	dispatchParameters.renderSize.width = static_cast<uint>(renderSize.x);
	dispatchParameters.renderSize.height = static_cast<uint>(renderSize.y);

	dispatchParameters.scale = 0.5f;
	dispatchParameters.flags = FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX;

	if (ffxFsr3ContextGenerateReactiveMask(&fsrContext, &dispatchParameters) != FFX_OK)
		L->critical("Failed to dispatch reactive mask!");
}

void FidelityFX::Upscale(Texture2D* a_color, float2 a_jitter, float2 a_renderSize)
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);


	auto& depthTexture = rendererData->depthStencilTargets[(uint)Util::DepthStencilTarget::kMain];
	auto& motionVectorTexture = rendererData->renderTargets[(uint)Util::RenderTarget::kMotionVectors];

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

	{
		FfxFsr3DispatchUpscaleDescription dispatchParameters{};

		dispatchParameters.commandList = ffxGetCommandListDX11(context);
		dispatchParameters.color = ffxGetResource(a_color->resource.get(), L"FSR3_Input_OutputColor", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.depth = ffxGetResource(reinterpret_cast<ID3D11Texture2D*>(depthTexture.texture), L"FSR3_InputDepth", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.motionVectors = ffxGetResource(reinterpret_cast<ID3D11Texture2D*>(motionVectorTexture.texture), L"FSR3_InputMotionVectors", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.upscaleOutput = dispatchParameters.color;
		dispatchParameters.reactive = ffxGetResource(reactiveMaskTexture->resource.get(), L"FSR3_InputReactiveMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.transparencyAndComposition = ffxGetResource(nullptr, L"FSR3_TransparencyAndCompositionMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

		dispatchParameters.motionVectorScale.x = a_renderSize.x;
		dispatchParameters.motionVectorScale.y = a_renderSize.y;
		dispatchParameters.renderSize.width = static_cast<uint>(a_renderSize.x);
		dispatchParameters.renderSize.height = static_cast<uint>(a_renderSize.y);
		dispatchParameters.jitterOffset.x = -a_jitter.x;
		dispatchParameters.jitterOffset.y = -a_jitter.y;

		dispatchParameters.frameTimeDelta = deltaTime * 1000.f;

		dispatchParameters.cameraNear = *(float*)REL::ID({ 57985, 2712882, 2712882 }).address();
		dispatchParameters.cameraFar = *(float*)REL::ID({ 958877, 2712883, 2712883 }).address();

		dispatchParameters.enableSharpening = true;
		dispatchParameters.sharpness = 0.0f;

		dispatchParameters.cameraFovAngleVertical = 1.0f;
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
		dispatchParameters.reset = false;
		dispatchParameters.preExposure = 1.0f;

		dispatchParameters.flags = 0;

		static bool loggedOnce = false;
		if (!loggedOnce) {
			L->info("First FSR3 dispatch: renderSize={}x{}, jitter=({}, {}), deltaTime={:.3f}ms, cameraNear={}, cameraFar={}",
				dispatchParameters.renderSize.width, dispatchParameters.renderSize.height,
				dispatchParameters.jitterOffset.x, dispatchParameters.jitterOffset.y,
				dispatchParameters.frameTimeDelta,
				dispatchParameters.cameraNear, dispatchParameters.cameraFar);
			loggedOnce = true;
		}

		if (ffxFsr3ContextDispatchUpscale(&fsrContext, &dispatchParameters) != FFX_OK)
			L->critical("Failed to dispatch upscaling!");
	}
}

}
