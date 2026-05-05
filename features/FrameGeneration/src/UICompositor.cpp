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

void UICompositor::CompositeUI(IDXGISwapChain*)
{
	// No-op: DLSS-G now presents the full proxy backbuffer (scene + UI) so a separate D3D12 UI blit is redundant.
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
