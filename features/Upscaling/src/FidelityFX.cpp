#include "FidelityFX.h"

#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Log.h"
#include "Upscaling.h"
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
	static auto gameViewport = cs::engine::GetGraphicsState();

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
}

#define FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_APPLY_TONEMAP                                    1
#define FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_APPLY_INVERSETONEMAP                             2
#define FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_APPLY_THRESHOLD                                  4
#define FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX                               8

void FidelityFX::GenerateReactiveMask()
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Unbind + restore engine OM around the reactive-mask dispatch; clears CS slots on exit.
	cs::engine::ComputeOMScope omcs(context);

	auto mainTexture = reinterpret_cast<ID3D11Texture2D*>(rendererData->renderTargets[(uint)cs::engine::RenderTarget::kMainTemp].texture);

	auto* upscaling = Upscaling::GetSingleton();
	if (!upscaling->colorOpaqueOnlyTexture || !upscaling->reactiveMaskTexture)
		return;

	FfxFsr3GenerateReactiveDescription dispatchParameters{};

	dispatchParameters.commandList = ffxGetCommandListDX11(context);

	dispatchParameters.colorOpaqueOnly = ffxGetResource(upscaling->colorOpaqueOnlyTexture->resource.get(), L"FSR3_Input_ColorOpaqueOnly", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchParameters.colorPreUpscale = ffxGetResource(mainTexture, L"FSR3_Input_ColorPreUpscale", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchParameters.outReactive = ffxGetResource(upscaling->reactiveMaskTexture->resource.get(), L"FSR3_Output_OutReactive", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	static auto gameViewport = cs::engine::GetGraphicsState();
	static auto renderTargetManager = cs::engine::GetRenderTargetManager();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * renderTargetManager->GetDynamicWidthRatio(), screenSize.y * renderTargetManager->GetDynamicHeightRatio());

	dispatchParameters.renderSize.width = static_cast<uint>(renderSize.x);
	dispatchParameters.renderSize.height = static_cast<uint>(renderSize.y);

	dispatchParameters.scale = 0.5f;
	dispatchParameters.flags = FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX;

	if (ffxFsr3ContextGenerateReactiveMask(&fsrContext, &dispatchParameters) != FFX_OK)
		L->critical("Failed to dispatch reactive mask!");
}

void FidelityFX::Upscale(Texture2D* a_color, Texture2D* a_reactiveMask, Texture2D* a_transparencyMask,
	float2 a_jitter, float2 a_renderSize)
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);


	auto& depthTexture = rendererData->depthStencilTargets[(uint)cs::engine::DepthStencilTarget::kMain];
	auto& motionVectorTexture = rendererData->renderTargets[(uint)cs::engine::RenderTarget::kMotionVectors];

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
		// FFX-generated reactive mask stays the FSR reactive channel; keep it independent of the encode pass.
		dispatchParameters.reactive = a_reactiveMask
			? ffxGetResource(a_reactiveMask->resource.get(), L"FSR3_InputReactiveMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ)
			: ffxGetResource(nullptr, L"FSR3_InputReactiveMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		// Encode-pass transparency mask only when valid this frame; otherwise submit null (no hint).
		const bool transparencyValid = Upscaling::GetSingleton()->masksValidThisFrame && a_transparencyMask;
		dispatchParameters.transparencyAndComposition = transparencyValid
			? ffxGetResource(a_transparencyMask->resource.get(), L"FSR3_TransparencyAndCompositionMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ)
			: ffxGetResource(nullptr, L"FSR3_TransparencyAndCompositionMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

		dispatchParameters.motionVectorScale.x = a_renderSize.x;
		dispatchParameters.motionVectorScale.y = a_renderSize.y;
		dispatchParameters.renderSize.width = static_cast<uint>(a_renderSize.x);
		dispatchParameters.renderSize.height = static_cast<uint>(a_renderSize.y);
		dispatchParameters.jitterOffset.x = -a_jitter.x;
		dispatchParameters.jitterOffset.y = -a_jitter.y;

		dispatchParameters.frameTimeDelta = deltaTime * 1000.f;

		dispatchParameters.cameraNear = cs::engine::GetCameraNear();
		dispatchParameters.cameraFar = cs::engine::GetCameraFar();

		const auto& upSettings = Upscaling::GetSingleton()->settings;
		dispatchParameters.enableSharpening = upSettings.sharpnessFSR > 0.0f;
		dispatchParameters.sharpness = upSettings.sharpnessFSR;

		dispatchParameters.cameraFovAngleVertical = cs::engine::GetVerticalFOV();
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
