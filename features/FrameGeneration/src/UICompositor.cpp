#include "UICompositor.h"
#include "Upscaling.h"
#include "DX12SwapChain.h"

#include <d3dcompiler.h>
#include <directx/d3dx12.h>
#include <dxgi1_2.h>

#include "Log.h"
#include "UITextureIsolation.h"

namespace cs::features::FrameGeneration
{
	namespace { auto* L = cs::log::Get("cs.feature.framegen.ui"); }

// Inline shader source - compiled at runtime with D3DCompile
static const char* s_vsSource = R"(
float4 mainVS(uint vertexId : SV_VertexID) : SV_POSITION {
    float2 uv = float2((vertexId & 1) * 2.0, (vertexId & 2) * 1.0);
    return float4(uv.x * 2.0 - 1.0, -(uv.y * 2.0 - 1.0), 0.5, 1.0);
}
)";

static const char* s_psSource = R"(
Texture2D<float4> UITexture : register(t0);
float4 mainPS(float4 pos : SV_POSITION) : SV_Target {
    return UITexture[int2(pos.xy)];
}
)";

UICompositor::UICompositor()
{
	InitializeCriticalSection(&initCS);
}

UICompositor::~UICompositor()
{
	if (fenceEvent) CloseHandle(fenceEvent);
	DeleteCriticalSection(&initCS);
}

void UICompositor::SetRealSwapChain(IDXGISwapChain4* a_swapChain, ID3D12CommandQueue* a_queue)
{
	realSwapChain = a_swapChain;
	commandQueue = a_queue;

	// Get device from queue
	a_queue->GetDevice(IID_PPV_ARGS(&device));

	// Get swap chain info
	DXGI_SWAP_CHAIN_DESC1 desc{};
	a_swapChain->GetDesc1(&desc);
	backbufferFormat = desc.Format;
	backbufferCount = desc.BufferCount;

	L->info("Real swap chain set: {:#x}, queue={:#x}, format={}, {}x{}, {} buffers",
		(uintptr_t)a_swapChain, (uintptr_t)a_queue,
		(int)backbufferFormat, desc.Width, desc.Height, backbufferCount);
}

void UICompositor::InitResources()
{
	if (!device || !commandQueue) {
		L->error("InitResources: no device or command queue");
		return;
	}

	L->info("Initializing D3D12 UI compositor resources");

	// Compile shaders
	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	HRESULT hr = D3DCompile(s_vsSource, strlen(s_vsSource), "UICompositeVS", nullptr, nullptr,
		"mainVS", "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);
	if (FAILED(hr)) {
		L->error("VS compile failed: {}", errorBlob ? (char*)errorBlob->GetBufferPointer() : "unknown");
		if (errorBlob) errorBlob->Release();
		return;
	}

	hr = D3DCompile(s_psSource, strlen(s_psSource), "UICompositePS", nullptr, nullptr,
		"mainPS", "ps_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errorBlob);
	if (FAILED(hr)) {
		L->error("PS compile failed: {}", errorBlob ? (char*)errorBlob->GetBufferPointer() : "unknown");
		if (errorBlob) errorBlob->Release();
		vsBlob->Release();
		return;
	}

	L->info("Shaders compiled (VS={} bytes, PS={} bytes)", vsBlob->GetBufferSize(), psBlob->GetBufferSize());

	// Root signature: 1 descriptor table with 1 SRV
	D3D12_DESCRIPTOR_RANGE1 range{};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	range.BaseShaderRegister = 0;
	range.RegisterSpace = 0;
	range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
	range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER1 rootParam{};
	rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParam.DescriptorTable.NumDescriptorRanges = 1;
	rootParam.DescriptorTable.pDescriptorRanges = &range;

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc{};
	rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	rsDesc.Desc_1_1.NumParameters = 1;
	rsDesc.Desc_1_1.pParameters = &rootParam;
	rsDesc.Desc_1_1.NumStaticSamplers = 0;
	rsDesc.Desc_1_1.pStaticSamplers = nullptr;
	rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ID3DBlob* sigBlob = nullptr;
	hr = D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &errorBlob);
	if (FAILED(hr)) {
		L->error("Root signature serialize failed: {:#x}", (uint32_t)hr);
		vsBlob->Release(); psBlob->Release();
		return;
	}

	hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.put()));
	sigBlob->Release();
	if (FAILED(hr)) {
		L->error("Root signature create failed: {:#x}", (uint32_t)hr);
		vsBlob->Release(); psBlob->Release();
		return;
	}

	// PSO with premultiplied alpha blending
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature.get();
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	// Premultiplied alpha blend: output = src*1 + dst*(1-srcAlpha)
	psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
	psoDesc.BlendState.IndependentBlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;

	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = backbufferFormat;
	psoDesc.SampleDesc = { 1, 0 };

	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.put()));
	vsBlob->Release();
	psBlob->Release();
	if (FAILED(hr)) {
		L->error("PSO create failed: {:#x}", (uint32_t)hr);
		return;
	}

	L->info("PSO created successfully");

	// SRV descriptor heap (GPU-visible)
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.NumDescriptors = 4;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap.put()));

	// RTV descriptor heap (CPU-only)
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = 4;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.put()));

	srvIncSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	rtvIncSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Command infrastructure
	for (int i = 0; i < 2; i++) {
		device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmdAllocators[i].put()));
		device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAllocators[i].get(), nullptr,
			IID_PPV_ARGS(cmdLists[i].put()));
		cmdLists[i]->Close();
	}

	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	initialized = true;
	L->info("Compositor fully initialized");
}

void UICompositor::CompositeUI(IDXGISwapChain* a_swapChain)
{
	auto upscaling = Upscaling::GetSingleton();
	if (!upscaling->d3d12Interop || !upscaling->setupBuffers) return;
	if (upscaling->activeFrameGenType != Upscaling::FrameGenType::kDLSSG) return;

	// Lazy init
	if (!initialized) {
		EnterCriticalSection(&initCS);
		if (!initialized) InitResources();
		LeaveCriticalSection(&initCS);
		if (!initialized) return;
	}

	// Get current backbuffer from real swap chain
	IDXGISwapChain4* sc4 = nullptr;
	a_swapChain->QueryInterface(IID_PPV_ARGS(&sc4));
	if (!sc4) return;

	UINT bufIdx = sc4->GetCurrentBackBufferIndex();
	ID3D12Resource* backbuffer = nullptr;
	sc4->GetBuffer(bufIdx, IID_PPV_ARGS(&backbuffer));
	sc4->Release();

	if (!backbuffer) return;

	// Prefer the engine-snapshot path; fall back to the colour-diff buffer if not yet shared.
	UINT uiIdx = uiFrameIndex.load();
	ID3D12Resource* uiResource = GetOrOpenPrivateUIResource12();
	const bool fromPrivate = (uiResource != nullptr);
	if (!uiResource)
		uiResource = upscaling->UIColorAlphaShared12[uiIdx].get();
	if (!uiResource) {
		backbuffer->Release();
		static int warnCount = 0;
		if (++warnCount <= 3) L->warn("CompositeUI: no UI resource (private and fallback both unavailable, frame idx={})", uiIdx);
		return;
	}

	// Wait for previous blit on this allocator slot
	UINT allocIdx = bufIdx % 2;
	if (fence->GetCompletedValue() < fenceValues[allocIdx]) {
		fence->SetEventOnCompletion(fenceValues[allocIdx], fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// Reset and record
	cmdAllocators[allocIdx]->Reset();
	cmdLists[allocIdx]->Reset(cmdAllocators[allocIdx].get(), pipelineState.get());
	auto cmdList = cmdLists[allocIdx].get();

	// Barriers: backbuffer PRESENT→RENDER_TARGET
	D3D12_RESOURCE_BARRIER preBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
	};
	cmdList->ResourceBarrier(_countof(preBarriers), preBarriers);

	// Create SRV for UI texture (ring buffer)
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
	srvCpu.ptr += srvRingIndex * srvIncSize;
	srvGpu.ptr += srvRingIndex * srvIncSize;
	srvRingIndex = (srvRingIndex + 1) % 4;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = fromPrivate
		? cs::features::UITextureIsolation::Get()->GetFormat()
		: DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(uiResource, &srvDesc, srvCpu);

	// Create RTV for backbuffer (ring buffer)
	D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtvCpu.ptr += rtvRingIndex * rtvIncSize;
	rtvRingIndex = (rtvRingIndex + 1) % 4;

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = backbufferFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(backbuffer, &rtvDesc, rtvCpu);

	// Set pipeline state
	cmdList->SetGraphicsRootSignature(rootSignature.get());
	ID3D12DescriptorHeap* heaps[] = { srvHeap.get() };
	cmdList->SetDescriptorHeaps(1, heaps);
	cmdList->SetGraphicsRootDescriptorTable(0, srvGpu);
	cmdList->OMSetRenderTargets(1, &rtvCpu, FALSE, nullptr);

	// Viewport + scissor
	D3D12_RESOURCE_DESC bbDesc = backbuffer->GetDesc();
	D3D12_VIEWPORT vp = { 0, 0, (float)bbDesc.Width, (float)bbDesc.Height, 0, 1 };
	D3D12_RECT sr = { 0, 0, (LONG)bbDesc.Width, (LONG)bbDesc.Height };
	cmdList->RSSetViewports(1, &vp);
	cmdList->RSSetScissorRects(1, &sr);

	// Draw fullscreen triangle
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);

	// Barriers: backbuffer RENDER_TARGET→PRESENT
	D3D12_RESOURCE_BARRIER postBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
	};
	cmdList->ResourceBarrier(_countof(postBarriers), postBarriers);

	cmdList->Close();

	ID3D12CommandList* lists[] = { cmdList };
	commandQueue->ExecuteCommandLists(1, lists);

	// Signal fence
	fenceValues[allocIdx]++;
	commandQueue->Signal(fence.get(), fenceValues[allocIdx]);

	backbuffer->Release();

	// Logging
	static int compCount = 0;
	compCount++;
	if (compCount <= 5) {
		L->info("CompositeUI executed (frame {}): backbuffer idx={}, uiIdx={}, bb={}x{}, source={}",
			compCount, bufIdx, uiIdx, (uint32_t)bbDesc.Width, (uint32_t)bbDesc.Height,
			fromPrivate ? "private-snapshot" : "color-diff");
	}
}

// Heap-pointer recycling can hand us the same ID3D11Texture2D address after a Reallocate, so cache by metadata too.
static bool IsTypelessFormat(DXGI_FORMAT a_fmt) noexcept
{
	switch (a_fmt) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R8_TYPELESS:
			return true;
		default:
			return false;
	}
}

ID3D12Resource* UICompositor::GetOrOpenPrivateUIResource12()
{
	if (!device)
		return nullptr;

	auto* feature  = cs::features::UITextureIsolation::Get();
	auto* d3d11Tex = feature->GetD3D11Texture();
	if (!d3d11Tex)
		return nullptr;

	const UINT       w   = feature->GetWidth();
	const UINT       h   = feature->GetHeight();
	const DXGI_FORMAT fmt = feature->GetFormat();
	if (IsTypelessFormat(fmt))
		return nullptr;

	if (d3d11Tex == lastSeenUITexture && w == lastSeenWidth && h == lastSeenHeight && fmt == lastSeenFormat && privateUIResource12)
		return privateUIResource12.get();

	privateUIResource12 = nullptr;
	lastSeenUITexture   = d3d11Tex;
	lastSeenWidth       = w;
	lastSeenHeight      = h;
	lastSeenFormat      = fmt;

	IDXGIResource1* dxgiRes = nullptr;
	if (FAILED(d3d11Tex->QueryInterface(IID_PPV_ARGS(&dxgiRes))) || !dxgiRes)
		return nullptr;

	HANDLE sharedHandle = nullptr;
	HRESULT hr = dxgiRes->CreateSharedHandle(
		nullptr,
		DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
		nullptr,
		&sharedHandle);
	dxgiRes->Release();
	if (FAILED(hr) || !sharedHandle) {
		L->warn("CreateSharedHandle on UITextureIsolation D3D11 texture failed: {:#x}", static_cast<uint32_t>(hr));
		return nullptr;
	}

	hr = device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(privateUIResource12.put()));
	CloseHandle(sharedHandle);
	if (FAILED(hr)) {
		L->warn("D3D12 OpenSharedHandle failed: {:#x}", static_cast<uint32_t>(hr));
		return nullptr;
	}

	L->info("Private UI resource opened on D3D12 ({:#x})", reinterpret_cast<uintptr_t>(privateUIResource12.get()));
	return privateUIResource12.get();
}

}
