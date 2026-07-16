#include "DX12SwapChain.h"

#include <dx12/ffx_api_dx12.hpp>
#include <dxgi1_6.h>

#include "Env.h"
#include "Render/Engine.h"
#include "FidelityFX.h"
#include "Log.h"
#include "Streamline.h"
#include "FrameGeneration.h"
#include "XeSSFG.h"

namespace cs::features::framegeneration
{
	namespace { auto* L = cs::log::Get("cs.feature.framegen.dx12"); }


void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));
	L->info("D3D12 device created");
}

void DX12SwapChain::CreateD3D12CommandQueues()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	for (int i = 0; i < 2; i++) {
		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(), nullptr, IID_PPV_ARGS(&commandLists[i])));
		commandLists[i]->Close();
	}
	L->info("D3D12 command queues created");
}

void DX12SwapChain::CreateSwapChain(IDXGIFactory5* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
	swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
	swapChainDesc.Format = a_swapChainDesc.BufferDesc.Format;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	BOOL allowTearing = FALSE;
	DX::ThrowIfFailed(a_dxgiFactory->CheckFeatureSupport(
		DXGI_FEATURE_PRESENT_ALLOW_TEARING,
		&allowTearing,
		sizeof(allowTearing)
	));

	swapChainDesc.Flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	auto frameGen = FrameGeneration::GetSingleton();

	if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
		CreateSwapChainDLSSG(a_dxgiFactory, a_swapChainDesc);
	} else if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kXeSSFG) {
		if (!CreateSwapChainXeSS(a_dxgiFactory, a_swapChainDesc)) {
			L->warn("XeSS-FG swap chain failed, falling back to FSR3");
			frameGen->activeFrameGenType = FrameGeneration::FrameGenType::kFSR3;
			CreateSwapChainFSR3(a_dxgiFactory, a_swapChainDesc);
		}
	} else {
		CreateSwapChainFSR3(a_dxgiFactory, a_swapChainDesc);
	}

	// Backbuffer acquisition lives in RecreateWrappedBuffers, called from CreateInterop and on resize.
	swapChainProxy = new DXGISwapChainProxy(swapChain);
}

void DX12SwapChain::CreateSwapChainFSR3(IDXGIFactory5* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	L->info("Creating FSR3 swap chain via FFX");

	ffx::CreateContextDescFrameGenerationSwapChainForHwndDX12 ffxSwapChainDesc{};
	ffxSwapChainDesc.desc = &swapChainDesc;
	ffxSwapChainDesc.dxgiFactory = a_dxgiFactory;
	ffxSwapChainDesc.fullscreenDesc = nullptr;
	ffxSwapChainDesc.gameQueue = commandQueue.get();
	ffxSwapChainDesc.hwnd = a_swapChainDesc.OutputWindow;
	ffxSwapChainDesc.swapchain = &swapChain;

	auto fidelityFX = FidelityFX::GetSingleton();

	if (ffx::CreateContext(fidelityFX->swapChainContext, nullptr, ffxSwapChainDesc) != ffx::ReturnCode::Ok) {
		L->critical("Failed to create swap chain context!");
	}

	fidelityFX->SetupFrameGeneration();
	L->info("FSR3 swap chain created: {}x{}", swapChainDesc.Width, swapChainDesc.Height);
}

void DX12SwapChain::CreateSwapChainDLSSG(IDXGIFactory5* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	L->info("Creating standard D3D12 swap chain for Streamline interception");

	winrt::com_ptr<IDXGISwapChain1> swapChain1;
	DX::ThrowIfFailed(a_dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.get(),
		a_swapChainDesc.OutputWindow,
		&swapChainDesc,
		nullptr,
		nullptr,
		swapChain1.put()
	));

	DX::ThrowIfFailed(swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain)));

	L->info("D3D12 swap chain created: {}x{}", swapChainDesc.Width, swapChainDesc.Height);
}

bool DX12SwapChain::CreateSwapChainXeSS(IDXGIFactory5* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	L->info("Creating XeSS-FG proxy swap chain");

	auto xess = XeSSFG::GetSingleton();
	if (!xess->InitSwapChain(commandQueue.get(), a_dxgiFactory, swapChainDesc, a_swapChainDesc.OutputWindow, &swapChain)) {
		return false;
	}

	L->info("Swap chain created: {}x{}", swapChainDesc.Width, swapChainDesc.Height);
	return true;
}

void DX12SwapChain::CreateInterop()
{
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);

	RecreateWrappedBuffers();
}

void DX12SwapChain::OnPreResize()
{
	// DXGI requires no outstanding refs on the back buffers before ResizeBuffers.
	swapChainBuffers[0] = nullptr;
	swapChainBuffers[1] = nullptr;
}

void DX12SwapChain::WaitForGPU()
{
	if (!commandQueue || !d3d12Fence)
		return;
	const UINT64 value = ++fenceValue;
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), value));
	if (d3d12Fence->GetCompletedValue() < value) {
		HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(value, evt));
		WaitForSingleObject(evt, INFINITE);
		CloseHandle(evt);
	}
}

void DX12SwapChain::RecreateWrappedBuffers()
{
	// Drain outstanding GPU work so the wrapped-resource deletes below don't race a still-in-flight frame.
	WaitForGPU();

	// Refresh swapChainDesc from the current chain so wrapped buffers match the live size.
	DX::ThrowIfFailed(swapChain->GetDesc1(&swapChainDesc));

	// Idempotent release before re-acquire; safe whether buffers are populated or empty.
	OnPreResize();
	DX::ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainBuffers[0])));
	DX::ThrowIfFailed(swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainBuffers[1])));
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	D3D11_TEXTURE2D_DESC texDesc11{};
	texDesc11.Width = swapChainDesc.Width;
	texDesc11.Height = swapChainDesc.Height;
	texDesc11.MipLevels = 1;
	texDesc11.ArraySize = 1;
	texDesc11.Format = swapChainDesc.Format;
	texDesc11.SampleDesc.Count = 1;
	texDesc11.SampleDesc.Quality = 0;
	texDesc11.Usage = D3D11_USAGE_DEFAULT;
	texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	texDesc11.CPUAccessFlags = 0;
	texDesc11.MiscFlags = 0;

	// Drop any prior wrapped resources before re-allocation.
	delete swapChainBufferProxy;
	swapChainBufferProxy = nullptr;
	delete swapChainBufferWrapped[0];
	swapChainBufferWrapped[0] = nullptr;
	delete swapChainBufferWrapped[1];
	swapChainBufferWrapped[1] = nullptr;

	swapChainBufferProxy = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
	swapChainBufferWrapped[0] = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
	swapChainBufferWrapped[1] = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
}

DXGISwapChainProxy* DX12SwapChain::GetSwapChainProxy()
{
	return swapChainProxy;
}

void DX12SwapChain::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(REFIID riid, void** ppSurface)
{
	if (!swapChainBufferProxy || !swapChainBufferProxy->resource11) {
		*ppSurface = nullptr;
		return DXGI_ERROR_INVALID_CALL;
	}
	// QueryInterface returns the owning ref required by IDXGISwapChain::GetBuffer.
	return swapChainBufferProxy->resource11->QueryInterface(riid, ppSurface);
}

void DX12SwapChain::PrepareAndCopyBackbuffer()
{
	// DLSS-G recomposition needs a single-channel UI alpha tag; derive it before the cross-API fence.
	if (FrameGeneration::GetSingleton()->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG)
		FrameGeneration::GetSingleton()->GenerateUIAlphaMask();

	// Present full proxy backbuffer (scene + UI); DLSS-G/FFX warp it as one image.
	d3d11Context->CopyResource(swapChainBufferWrapped[frameIndex]->resource11, swapChainBufferProxy->resource11);

	// Fence D3D11 work before D3D12 reads the shared texture.
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
	fenceValue++;

	DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
	DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));

	// Copy the shared D3D11 texture into the D3D12 swap-chain backbuffer.
	{
		auto srcResource = swapChainBufferWrapped[frameIndex]->resource.get();
		auto dstResource = swapChainBuffers[frameIndex].get();

		D3D12_RESOURCE_BARRIER barriers[2] = {
			CD3DX12_RESOURCE_BARRIER::Transition(srcResource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(dstResource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
		};
		commandLists[frameIndex]->ResourceBarrier(2, barriers);

		commandLists[frameIndex]->CopyResource(dstResource, srcResource);

		D3D12_RESOURCE_BARRIER postBarriers[2] = {
			CD3DX12_RESOURCE_BARRIER::Transition(srcResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
		};
		commandLists[frameIndex]->ResourceBarrier(2, postBarriers);
	}
}

bool DX12SwapChain::ShouldUseFrameGeneration()
{
	auto frameGen = FrameGeneration::GetSingleton();

	bool useFrameGenerationThisFrame = false;

	if (auto main = RE::Main::GetSingleton()) {
		if (auto ui = RE::UI::GetSingleton()) {
			bool menuBlock = frameGen->settings.disableInMenus && main->inMenuMode;
			useFrameGenerationThisFrame = frameGen->settings.frameGenerationMode && main->gameActive && !menuBlock && !ui->movementToDirectionalCount;
		}
	}

	return useFrameGenerationThisFrame;
}

void DX12SwapChain::PublishFrameStatistics(bool a_useFrameGen)
{
	auto frameGen = FrameGeneration::GetSingleton();

	// Publish displayed-FPS multiplier; use the active backend after init fallback.
	int multiplier = 1;
	if (a_useFrameGen) {
		switch (frameGen->activeFrameGenType) {
			case FrameGeneration::FrameGenType::kDLSSG:
				multiplier = 1 + std::max(1, frameGen->settings.frameGenFrames);
				break;
			default:
				multiplier = 2;
				break;
		}
	}
	cs::env::SetDisplayedFrameMultiplier(multiplier);

	// Accumulate actual presented frames; FSR3 falls back to multiplier to avoid hijacking presentCallback.
	uint32_t actualFrames = 0;
	if (a_useFrameGen) {
		switch (frameGen->activeFrameGenType) {
			case FrameGeneration::FrameGenType::kDLSSG:
				actualFrames = StreamlineFG::GetSingleton()->ConsumeFramesPresented();
				break;
			case FrameGeneration::FrameGenType::kXeSSFG:
				actualFrames = XeSSFG::GetSingleton()->ConsumeFramesPresented();
				break;
			default:
				break;
		}
		if (actualFrames == 0)
			actualFrames = static_cast<uint32_t>(multiplier);
	} else {
		actualFrames = 1;
	}
	cs::env::AddDisplayedFrames(actualFrames);
}

void DX12SwapChain::DispatchFrameGeneration(bool a_useFrameGen, bool& a_isDLSSGFrame, bool& a_isXeSSFrame)
{
	auto frameGen = FrameGeneration::GetSingleton();

	if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kDLSSG) {
		auto dlssg = StreamlineFG::GetSingleton();

		// Toggle DLSS-G only on state changes, matching XeSS.
		static bool dlssgWasEnabled = false;
		if (a_useFrameGen != dlssgWasEnabled) {
			dlssg->SetEnabled(a_useFrameGen);
			dlssgWasEnabled = a_useFrameGen;
		}

		// NVIDIA order: token -> sleep -> sim markers -> constants/tags -> render markers -> present markers.
		dlssg->AcquireFrameToken();

		if (dlssg->slReflexSleep && dlssg->frameToken)
			dlssg->slReflexSleep(*dlssg->frameToken);

		dlssg->SetPCLMarker(sl::PCLMarker::eSimulationStart);
		dlssg->SetPCLMarker(sl::PCLMarker::eSimulationEnd);

		static auto gameViewport = cs::engine::GetGraphicsState();

		auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));

		float2 jitter;
		jitter.x = -gameViewport->offsetX * screenSize.x / 2.0f;
		jitter.y = gameViewport->offsetY * screenSize.y / 2.0f;

		float cameraNear = cs::engine::GetCameraNear();
		float cameraFar = cs::engine::GetCameraFar();

		auto& camView = gameViewport->cameraState.camViewData;
		auto& camState = gameViewport->cameraState;

		StreamlineFG::CameraData camera;
		camera.viewMat = camView.viewMat;
		camera.viewProjUnjittered = camView.viewProjUnjittered;
		camera.currentViewProjUnjittered = camView.currentViewProjUnjittered;
		camera.previousViewProjUnjittered = camView.previousViewProjUnjittered;
		camera.viewUp = &camView.viewUp;
		camera.viewRight = &camView.viewRight;
		camera.viewDir = &camView.viewDir;
		camera.posX = camState.posAdjust.x;
		camera.posY = camState.posAdjust.y;
		camera.posZ = camState.posAdjust.z;

		// Full composite on swap chain; UI alpha mask drives DLSS-G recomposition for sharp UI on interpolated frames.
		dlssg->Present(
			commandLists[frameIndex].get(),
			frameGen->depthBufferShared12[frameIndex].get(),
			frameGen->motionVectorBufferShared12[frameIndex].get(),
			frameGen->HUDLessBufferShared12[frameIndex].get(),
			nullptr,
			frameGen->UIAlphaBufferShared12[frameIndex].get(),
			screenSize, jitter,
			cameraNear, cameraFar, camera);

		a_isDLSSGFrame = true;
	} else if (frameGen->activeFrameGenType == FrameGeneration::FrameGenType::kXeSSFG) {
		auto xess = XeSSFG::GetSingleton();
		if (xess->initialized) {
			// Toggle XeSS-FG only on state changes.
			static bool xessFGWasEnabled = false;
			if (a_useFrameGen != xessFGWasEnabled) {
				xess->SetEnabled(a_useFrameGen ? 1 : 0);
				xessFGWasEnabled = a_useFrameGen;
			}

			xess->BeginFrame(xessFrameId);
			xess->SetMarker(XELL_SIMULATION_END, xessFrameId);

			a_isXeSSFrame = true;
			xessFrameId++;
		}
	} else {
		FidelityFX::GetSingleton()->Present(a_useFrameGen);
	}
}

void DX12SwapChain::TagXeSSResourcesIfNeeded(bool a_useFrameGen, bool a_isXeSSFrame)
{
	// XeSS-FG tags resources after the main list, using the other command-list slot before Present.
	if (a_isXeSSFrame && a_useFrameGen) {
		auto frameGen = FrameGeneration::GetSingleton();
		auto xess = XeSSFG::GetSingleton();
		int tagListIdx = (frameIndex + 1) % 2;

		// Safe without an explicit fence: Present sync retires last frame's tag list before reuse.
		DX::ThrowIfFailed(commandAllocators[tagListIdx]->Reset());
		DX::ThrowIfFailed(commandLists[tagListIdx]->Reset(commandAllocators[tagListIdx].get(), nullptr));

		static auto gameViewport = cs::engine::GetGraphicsState();
		auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));

		float2 jitterNorm;
		jitterNorm.x = -gameViewport->offsetX / 2.0f;
		jitterNorm.y = gameViewport->offsetY / 2.0f;

		static LARGE_INTEGER frequency = []() {
			LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f;
		}();
		static LARGE_INTEGER lastXeSSFrame = []() {
			LARGE_INTEGER t; QueryPerformanceCounter(&t); return t;
		}();
		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);
		float deltaMs = static_cast<float>(currentTime.QuadPart - lastXeSSFrame.QuadPart) /
			static_cast<float>(frequency.QuadPart) * 1000.0f;
		lastXeSSFrame = currentTime;

		auto& camView = gameViewport->cameraState.camViewData;
		alignas(16) float viewMat[16];
		alignas(16) float projMat[16];
		for (int i = 0; i < 4; i++)
			_mm_store_ps(&viewMat[i * 4], camView.viewMat[i]);

		DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4((DirectX::XMFLOAT4X4*)camView.viewMat);
		DirectX::XMMATRIX vpUnjittered = DirectX::XMLoadFloat4x4((DirectX::XMFLOAT4X4*)camView.viewProjUnjittered);
		DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
		DirectX::XMMATRIX proj = DirectX::XMMatrixMultiply(invView, vpUnjittered);
		DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)projMat, proj);

		xess->TagResources(xessFrameId - 1,
			commandLists[tagListIdx].get(),
			frameGen->depthBufferShared12[frameIndex].get(),
			frameGen->motionVectorBufferShared12[frameIndex].get(),
			frameGen->HUDLessBufferShared12[frameIndex].get(),
			screenSize, jitterNorm,
			deltaMs, viewMat, projMat, false);

		DX::ThrowIfFailed(commandLists[tagListIdx]->Close());

		ID3D12CommandList* tagCmdLists[] = { commandLists[tagListIdx].get() };
		commandQueue->ExecuteCommandLists(1, tagCmdLists);
	}
}

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT Flags)
{
	PrepareAndCopyBackbuffer();
	bool useFrameGenerationThisFrame = ShouldUseFrameGeneration();
	PublishFrameStatistics(useFrameGenerationThisFrame);
	bool isDLSSGFrame = false, isXeSSFrame = false;
	DispatchFrameGeneration(useFrameGenerationThisFrame, isDLSSGFrame, isXeSSFrame);

	DX::ThrowIfFailed(commandLists[frameIndex]->Close());

	// Bracket GPU submission with render markers.
	if (isDLSSGFrame) StreamlineFG::GetSingleton()->SetPCLMarker(sl::PCLMarker::eRenderSubmitStart);
	if (isXeSSFrame) XeSSFG::GetSingleton()->SetMarker(XELL_RENDERSUBMIT_START, xessFrameId - 1);

	ID3D12CommandList* commandListsToExecute[] = { commandLists[frameIndex].get() };
	commandQueue->ExecuteCommandLists(1, commandListsToExecute);

	TagXeSSResourcesIfNeeded(useFrameGenerationThisFrame, isXeSSFrame);

	if (isDLSSGFrame) StreamlineFG::GetSingleton()->SetPCLMarker(sl::PCLMarker::eRenderSubmitEnd);
	if (isXeSSFrame) XeSSFG::GetSingleton()->SetMarker(XELL_RENDERSUBMIT_END, xessFrameId - 1);

	auto frameGen = FrameGeneration::GetSingleton();

	if (!frameGen->highFPSPhysicsFixLoaded && SyncInterval > 0)
		SyncInterval = 1;

	// Bracket Present with latency markers.
	if (isDLSSGFrame) StreamlineFG::GetSingleton()->SetPCLMarker(sl::PCLMarker::ePresentStart);
	if (isXeSSFrame) XeSSFG::GetSingleton()->SetMarker(XELL_PRESENT_START, xessFrameId - 1);

	DX::ThrowIfFailed(swapChain->Present(SyncInterval, Flags));

	if (isDLSSGFrame) StreamlineFG::GetSingleton()->SetPCLMarker(sl::PCLMarker::ePresentEnd);
	if (isXeSSFrame) XeSSFG::GetSingleton()->SetMarker(XELL_PRESENT_END, xessFrameId - 1);

	// Null when wrapped swap chains manage latency internally.
	auto frameLatencyWaitableObject = swapChain->GetFrameLatencyWaitableObject();
	if (frameLatencyWaitableObject)
		WaitForSingleObjectEx(frameLatencyWaitableObject, INFINITE, TRUE);

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	frameGen->Reset();

	if (!frameGen->highFPSPhysicsFixLoaded)
		frameGen->GameFrameLimiter();

	// Use our limiter only when VSync and HighFPSPhysicsFix pacing are both absent.
	if (SyncInterval == 0 && !frameGen->highFPSPhysicsFixLoaded)
		frameGen->FrameLimiter(useFrameGenerationThisFrame);

	return S_OK;
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void** ppDevice)
{
	if (uuid == __uuidof(ID3D11Device) || uuid == __uuidof(ID3D11Device1) || uuid == __uuidof(ID3D11Device2) || uuid == __uuidof(ID3D11Device3) || uuid == __uuidof(ID3D11Device4) || uuid == __uuidof(ID3D11Device5)) {
		// QueryInterface AddRefs; IDXGIDeviceSubObject::GetDevice transfers an owning reference.
		return d3d11Device->QueryInterface(uuid, ppDevice);
	}

	return swapChain->GetDevice(uuid, ppDevice);
}

WrappedResource::WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device)
{
	// Share D3D11-created textures with D3D12; wrapping D3D12 resources broke interop.
	a_texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	DX::ThrowIfFailed(a_d3d11Device->CreateTexture2D(&a_texDesc, nullptr, &resource11));

	// CreateSharedHandle transfers cross-API access; close the HANDLE after OpenSharedHandle.
	winrt::com_ptr<IDXGIResource1> dxgiResource;
	DX::ThrowIfFailed(resource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
	HANDLE sharedHandle = nullptr;
	DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle));

	DX::ThrowIfFailed(a_d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(resource.put())));
	CloseHandle(sharedHandle);

	if (a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = a_texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		DX::ThrowIfFailed(a_d3d11Device->CreateShaderResourceView(resource11, &srvDesc, &srv));
	}

	if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
		if (a_texDesc.ArraySize > 1) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = a_texDesc.ArraySize;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		}
		else {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		}
	}

	if (a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = a_texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		DX::ThrowIfFailed(a_d3d11Device->CreateRenderTargetView(resource11, &rtvDesc, &rtv));
	}
}

WrappedResource::~WrappedResource()
{
	if (rtv) { rtv->Release(); rtv = nullptr; }
	if (uav) { uav->Release(); uav = nullptr; }
	if (srv) { srv->Release(); srv = nullptr; }
	if (resource11) { resource11->Release(); resource11 = nullptr; }
}

DXGISwapChainProxy::DXGISwapChainProxy(IDXGISwapChain4* a_swapChain)
{
	swapChain = a_swapChain;
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
	if (!ppvObj)
		return E_INVALIDARG;

	// Most-derived first so a request for IDXGISwapChain4 returns the v4 vtable, not a v0 stub.
	if (riid == __uuidof(IDXGISwapChain4)) {
		*ppvObj = static_cast<IDXGISwapChain4*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == __uuidof(IDXGISwapChain3)) {
		*ppvObj = static_cast<IDXGISwapChain3*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == __uuidof(IDXGISwapChain2)) {
		*ppvObj = static_cast<IDXGISwapChain2*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == __uuidof(IDXGISwapChain1)) {
		*ppvObj = static_cast<IDXGISwapChain1*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == __uuidof(IDXGISwapChain)) {
		*ppvObj = static_cast<IDXGISwapChain*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == __uuidof(IDXGIDeviceSubObject)) {
		*ppvObj = static_cast<IDXGIDeviceSubObject*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == __uuidof(IDXGIObject)) {
		*ppvObj = static_cast<IDXGIObject*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == __uuidof(IUnknown)) {
		*ppvObj = static_cast<IUnknown*>(this);
		AddRef();
		return S_OK;
	}

	// Unknown GUID (driver/ENB/overlay private interface): forward so it reaches the real chain.
	return swapChain->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
	return refCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
	const ULONG ref = refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
	if (ref == 0) {
		// The inner chain is owned by DX12SwapChain, not the proxy; only free the proxy node.
		DX12SwapChain::GetSingleton()->swapChainProxy = nullptr;
		delete this;
	}
	return ref;
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData)
{
	return swapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown)
{
	return swapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData)
{
	return swapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent)
{
	return swapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice)
{
	return DX12SwapChain::GetSingleton()->GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
	return DX12SwapChain::GetSingleton()->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT, _In_ REFIID riid, _COM_Outptr_ void** ppSurface)
{
	return DX12SwapChain::GetSingleton()->GetBuffer(riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget)
{
	return swapChain->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget)
{
	return swapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
	return swapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	auto& dx12 = *DX12SwapChain::GetSingleton();
	dx12.OnPreResize();
	HRESULT hr = swapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
	if (SUCCEEDED(hr)) {
		dx12.RecreateWrappedBuffers();
		// Reflex state is swap-chain-bound; rebuilds can drop it, so re-push cheaply.
		StreamlineFG::GetSingleton()->ReapplyReflexOptions();
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters)
{
	return swapChain->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput)
{
	return swapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats)
{
	return swapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT* pLastPresentCount)
{
	return swapChain->GetLastPresentCount(pLastPresentCount);
}

/****IDXGISwapChain1****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc1(_Out_ DXGI_SWAP_CHAIN_DESC1* pDesc) { return swapChain->GetDesc1(pDesc); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenDesc(_Out_ DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) { return swapChain->GetFullscreenDesc(pDesc); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetHwnd(_Out_ HWND* pHwnd) { return swapChain->GetHwnd(pHwnd); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetCoreWindow(_In_ REFIID refiid, _COM_Outptr_ void** ppUnk) { return swapChain->GetCoreWindow(refiid, ppUnk); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present1(UINT SyncInterval, UINT PresentFlags, _In_ const DXGI_PRESENT_PARAMETERS* pPresentParameters) { return swapChain->Present1(SyncInterval, PresentFlags, pPresentParameters); }
BOOL STDMETHODCALLTYPE DXGISwapChainProxy::IsTemporaryMonoSupported() { return swapChain->IsTemporaryMonoSupported(); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRestrictToOutput(_Out_ IDXGIOutput** ppRestrictToOutput) { return swapChain->GetRestrictToOutput(ppRestrictToOutput); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetBackgroundColor(_In_ const DXGI_RGBA* pColor) { return swapChain->SetBackgroundColor(pColor); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBackgroundColor(_Out_ DXGI_RGBA* pColor) { return swapChain->GetBackgroundColor(pColor); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetRotation(DXGI_MODE_ROTATION Rotation) { return swapChain->SetRotation(Rotation); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRotation(_Out_ DXGI_MODE_ROTATION* pRotation) { return swapChain->GetRotation(pRotation); }

/****IDXGISwapChain2****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetSourceSize(UINT Width, UINT Height) { return swapChain->SetSourceSize(Width, Height); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetSourceSize(_Out_ UINT* pWidth, _Out_ UINT* pHeight) { return swapChain->GetSourceSize(pWidth, pHeight); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMaximumFrameLatency(UINT MaxLatency) { return swapChain->SetMaximumFrameLatency(MaxLatency); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMaximumFrameLatency(_Out_ UINT* pMaxLatency) { return swapChain->GetMaximumFrameLatency(pMaxLatency); }
HANDLE STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameLatencyWaitableObject() { return swapChain->GetFrameLatencyWaitableObject(); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) { return swapChain->SetMatrixTransform(pMatrix); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMatrixTransform(_Out_ DXGI_MATRIX_3X2_F* pMatrix) { return swapChain->GetMatrixTransform(pMatrix); }

/****IDXGISwapChain3****/
UINT STDMETHODCALLTYPE DXGISwapChainProxy::GetCurrentBackBufferIndex() { return swapChain->GetCurrentBackBufferIndex(); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, _Out_ UINT* pColorSpaceSupport) { return swapChain->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) { return swapChain->SetColorSpace1(ColorSpace); }
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, _In_reads_(BufferCount) const UINT* pCreationNodeMask, _In_reads_(BufferCount) IUnknown* const* ppPresentQueue)
{
	auto& dx12 = *DX12SwapChain::GetSingleton();
	dx12.OnPreResize();
	HRESULT hr = swapChain->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
	if (SUCCEEDED(hr)) {
		dx12.RecreateWrappedBuffers();
		// See ResizeBuffers: Reflex options need re-pushing after swap rebuild.
		StreamlineFG::GetSingleton()->ReapplyReflexOptions();
	}
	return hr;
}

/****IDXGISwapChain4****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, _In_reads_opt_(Size) void* pMetaData) { return swapChain->SetHDRMetaData(Type, Size, pMetaData); }

}
