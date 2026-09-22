#include "DXGISwapChainFacadeContract.h"
#include "DXGISwapChainProxy.h"

#include <d3d11.h>
#include <d3d12.h>
#include <winrt/base.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	class InnerSwapChain : public winrt::implements<InnerSwapChain, IDXGISwapChain4>
	{
	public:
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT a_size,
			const void*) noexcept override
		{
			privateDataSize = a_size;
			return S_FALSE;
		}
		HRESULT STDMETHODCALLTYPE
		SetPrivateDataInterface(REFGUID, const IUnknown*) noexcept override
		{
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT* a_size,
			void*) noexcept override
		{
			if (a_size) {
				*a_size = privateDataSize;
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetParent(REFIID a_iid,
			void** a_parent) noexcept override
		{
			if (!a_parent) {
				return E_POINTER;
			}
			*a_parent = nullptr;
			return parent ? parent->QueryInterface(a_iid, a_parent) : E_NOINTERFACE;
		}
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void**) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE Present(UINT, UINT) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT, REFIID, void**) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		SetFullscreenState(BOOL, IDXGIOutput*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		GetFullscreenState(BOOL*, IDXGIOutput**) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT,
			UINT) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		ResizeTarget(const DXGI_MODE_DESC*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		GetContainingOutput(IDXGIOutput** a_output) noexcept override
		{
			if (a_output) {
				*a_output = nullptr;
			}
			return DXGI_ERROR_NOT_FOUND;
		}
		HRESULT STDMETHODCALLTYPE
		GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept override
		{
			if (a_stats) {
				*a_stats = {};
				a_stats->PresentCount = 37;
			}
			return DXGI_STATUS_OCCLUDED;
		}
		HRESULT STDMETHODCALLTYPE
		GetLastPresentCount(UINT* a_count) noexcept override
		{
			if (a_count) {
				*a_count = 41;
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE
		GetDesc1(DXGI_SWAP_CHAIN_DESC1*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE GetHwnd(HWND*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID,
			void** a_window) noexcept override
		{
			if (a_window) {
				*a_window = nullptr;
			}
			return E_NOINTERFACE;
		}
		HRESULT STDMETHODCALLTYPE
		Present1(UINT, UINT, const DXGI_PRESENT_PARAMETERS*) noexcept override
		{
			return E_UNEXPECTED;
		}
		BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() noexcept override
		{
			return FALSE;
		}
		HRESULT STDMETHODCALLTYPE
		GetRestrictToOutput(IDXGIOutput** a_output) noexcept override
		{
			if (a_output) {
				*a_output = nullptr;
			}
			return DXGI_ERROR_NOT_FOUND;
		}
		HRESULT STDMETHODCALLTYPE
		SetBackgroundColor(const DXGI_RGBA*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		GetRotation(DXGI_MODE_ROTATION*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE SetSourceSize(UINT, UINT) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE GetSourceSize(UINT*, UINT*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() noexcept override
		{
			return nullptr;
		}
		HRESULT STDMETHODCALLTYPE
		SetMatrixTransform(const DXGI_MATRIX_3X2_F*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		GetMatrixTransform(DXGI_MATRIX_3X2_F*) noexcept override
		{
			return E_UNEXPECTED;
		}
		UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() noexcept override
		{
			return 0;
		}
		HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE,
			UINT*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		SetColorSpace1(DXGI_COLOR_SPACE_TYPE) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE
		ResizeBuffers1(UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*,
			IUnknown* const*) noexcept override
		{
			return E_UNEXPECTED;
		}
		HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE, UINT,
			void*) noexcept override
		{
			return E_UNEXPECTED;
		}

		IUnknown* parent = nullptr;
		UINT privateDataSize = 0;
	};

	class RecordingOwner final : public cs::features::IDXGISwapChainProxyOwner
	{
	public:
		HRESULT Present(UINT a_syncInterval, UINT a_flags) noexcept override
		{
			++presentCalls;
			lastSyncInterval = a_syncInterval;
			lastPresentFlags = a_flags;
			return presentResult;
		}
		HRESULT
		Present1(UINT a_syncInterval, UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept override
		{
			++present1Calls;
			lastSyncInterval = a_syncInterval;
			lastPresentFlags = a_flags;
			present1Parameters =
				a_parameters ? *a_parameters : DXGI_PRESENT_PARAMETERS{};
			return present1Result;
		}
		HRESULT GetBuffer(UINT a_buffer, REFIID a_iid,
			void** a_surface) noexcept override
		{
			if (!a_surface) {
				return E_POINTER;
			}
			*a_surface = nullptr;
			return a_buffer == 0 && buffer ? buffer->QueryInterface(a_iid, a_surface) : DXGI_ERROR_INVALID_CALL;
		}
		HRESULT GetDevice(REFIID a_iid, void** a_device) noexcept override
		{
			if (!a_device) {
				return E_POINTER;
			}
			*a_device = nullptr;
			return device ? device->QueryInterface(a_iid, a_device) : E_NOINTERFACE;
		}
		HRESULT SetFullscreenState(BOOL, IDXGIOutput*) noexcept override
		{
			return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
		}
		HRESULT GetFullscreenState(BOOL* a_fullscreen,
			IDXGIOutput** a_target) noexcept override
		{
			if (!a_fullscreen) {
				return E_POINTER;
			}
			*a_fullscreen = FALSE;
			if (a_target) {
				*a_target = nullptr;
			}
			return S_OK;
		}
		HRESULT GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override
		{
			if (!a_desc) {
				return E_POINTER;
			}
			*a_desc = desc;
			return S_OK;
		}
		HRESULT ResizeBuffers(UINT a_bufferCount, UINT a_width, UINT a_height,
			DXGI_FORMAT a_format, UINT a_flags) noexcept override
		{
			++resizeCalls;
			lastResize = { a_bufferCount, a_width, a_height, a_flags };
			lastFormat = a_format;
			return resizeResult;
		}
		HRESULT ResizeTarget(const DXGI_MODE_DESC*) noexcept override
		{
			return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
		}
		HRESULT GetDesc1(DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept override
		{
			if (!a_desc) {
				return E_POINTER;
			}
			*a_desc = desc1;
			return S_OK;
		}
		HRESULT
		GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept override
		{
			if (!a_desc) {
				return E_POINTER;
			}
			*a_desc = fullscreenDesc;
			return S_OK;
		}
		HRESULT GetHwnd(HWND* a_window) noexcept override
		{
			if (!a_window) {
				return E_POINTER;
			}
			*a_window = window;
			return S_OK;
		}
		UINT GetCurrentBackBufferIndex() noexcept override { return 0; }
		HRESULT CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE,
			UINT* a_support) noexcept override
		{
			if (!a_support) {
				return E_POINTER;
			}
			*a_support = DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
			return S_OK;
		}
		HRESULT SetColorSpace1(DXGI_COLOR_SPACE_TYPE) noexcept override
		{
			return S_OK;
		}
		HRESULT ResizeBuffers1(UINT a_bufferCount, UINT a_width, UINT a_height,
			DXGI_FORMAT a_format, UINT a_flags,
			const UINT* a_creationNodeMask,
			IUnknown* const* a_presentQueue) noexcept override
		{
			++resize1Calls;
			lastResize = { a_bufferCount, a_width, a_height, a_flags };
			lastFormat = a_format;
			sawNodeMask = a_creationNodeMask != nullptr;
			sawPresentQueue = a_presentQueue != nullptr;
			return resize1Result;
		}
		HRESULT SetHDRMetaData(DXGI_HDR_METADATA_TYPE, UINT,
			void*) noexcept override
		{
			return DXGI_ERROR_UNSUPPORTED;
		}

		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11Texture2D> buffer;
		DXGI_SWAP_CHAIN_DESC desc{};
		DXGI_SWAP_CHAIN_DESC1 desc1{};
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc{};
		DXGI_PRESENT_PARAMETERS present1Parameters{};
		std::array<UINT, 4> lastResize{};
		HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x5678));
		DXGI_FORMAT lastFormat = DXGI_FORMAT_UNKNOWN;
		HRESULT presentResult = S_OK;
		HRESULT present1Result = S_OK;
		HRESULT resizeResult = S_OK;
		HRESULT resize1Result = S_OK;
		UINT presentCalls = 0;
		UINT present1Calls = 0;
		UINT resizeCalls = 0;
		UINT resize1Calls = 0;
		UINT lastSyncInterval = 0;
		UINT lastPresentFlags = 0;
		bool sawNodeMask = false;
		bool sawPresentQueue = false;
	};

	bool CreateWarpResources(RecordingOwner& a_owner)
	{
		D3D_FEATURE_LEVEL featureLevel{};
		if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				nullptr, 0, D3D11_SDK_VERSION,
				a_owner.device.put(), &featureLevel, nullptr))) {
			return false;
		}
		D3D11_TEXTURE2D_DESC desc{ .Width = 64,
			.Height = 64,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_RENDER_TARGET };
		return SUCCEEDED(
			a_owner.device->CreateTexture2D(&desc, nullptr, a_owner.buffer.put()));
	}

	void InitializeFacadeDescriptions(RecordingOwner& a_owner)
	{
		DXGI_SWAP_CHAIN_DESC requested{};
		requested.BufferDesc = { 1280, 720, { 60, 1 }, DXGI_FORMAT_R8G8B8A8_UNORM };
		requested.SampleDesc = { 1, 0 };
		requested.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		requested.BufferCount = 1;
		requested.OutputWindow = a_owner.window;
		requested.Windowed = TRUE;
		requested.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		requested.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
		                  DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		const DXGI_SWAP_CHAIN_DESC1 inner{
			.Width = 1920,
			.Height = 1080,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc = { 1, 0 },
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 2,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
			.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
			         DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
		};
		a_owner.desc =
			cs::features::swap_chain_facade::BuildDescription(requested, inner);
		a_owner.desc1 =
			cs::features::swap_chain_facade::BuildDescription1(a_owner.desc);
		a_owner.fullscreenDesc =
			cs::features::swap_chain_facade::BuildFullscreenDescription(a_owner.desc);
	}

	void TestInterfacesAndIdentity(cs::features::DXGISwapChainProxy& a_proxy)
	{
		IUnknown* canonical = nullptr;
		Check(SUCCEEDED(a_proxy.QueryInterface(IID_PPV_ARGS(&canonical))) &&
				  canonical,
			"IUnknown query succeeds");
		for (const IID* iid :
			std::array{ &__uuidof(IDXGISwapChain), &__uuidof(IDXGISwapChain1),
				&__uuidof(IDXGISwapChain2), &__uuidof(IDXGISwapChain3),
				&__uuidof(IDXGISwapChain4) }) {
			void* version = nullptr;
			IUnknown* identity = nullptr;
			Check(SUCCEEDED(a_proxy.QueryInterface(*iid, &version)) && version &&
					  SUCCEEDED(static_cast<IUnknown*>(version)->QueryInterface(
						  IID_PPV_ARGS(&identity))) &&
					  identity == canonical,
				"all swap-chain versions share the facade IUnknown");
			if (identity) {
				identity->Release();
			}
			if (version) {
				static_cast<IUnknown*>(version)->Release();
			}
		}
		void* escaped = reinterpret_cast<void*>(1);
		Check(a_proxy.QueryInterface(__uuidof(IDXGISwapChainMedia), &escaped) ==
					  E_NOINTERFACE &&
				  escaped == nullptr,
			"unsupported inner interfaces cannot escape the facade");
		canonical->Release();
	}

	void TestDeviceAndBuffer(cs::features::DXGISwapChainProxy& a_proxy,
		const RecordingOwner& a_owner)
	{
		ID3D11Device* device = nullptr;
		Check(SUCCEEDED(a_proxy.GetDevice(IID_PPV_ARGS(&device))) &&
				  device == a_owner.device.get(),
			"GetDevice returns the outward D3D11 device");
		if (device) {
			device->Release();
		}
		ID3D12Device* wrongDevice = reinterpret_cast<ID3D12Device*>(1);
		Check(a_proxy.GetDevice(IID_PPV_ARGS(&wrongDevice)) == E_NOINTERFACE &&
				  wrongDevice == nullptr,
			"GetDevice does not expose a D3D12 device");
		ID3D11Texture2D* buffer = nullptr;
		Check(SUCCEEDED(a_proxy.GetBuffer(0, IID_PPV_ARGS(&buffer))) &&
				  buffer == a_owner.buffer.get(),
			"GetBuffer zero returns the outward D3D11 texture");
		if (buffer) {
			buffer->Release();
		}
		buffer = reinterpret_cast<ID3D11Texture2D*>(1);
		Check(a_proxy.GetBuffer(1, IID_PPV_ARGS(&buffer)) ==
					  DXGI_ERROR_INVALID_CALL &&
				  buffer == nullptr,
			"discard-model facade rejects nonzero buffer indexes");
	}

	void TestPresentAndResize(cs::features::DXGISwapChainProxy& a_proxy,
		RecordingOwner& a_owner)
	{
		a_owner.presentResult = DXGI_ERROR_WAS_STILL_DRAWING;
		Check(a_proxy.Present(0, DXGI_PRESENT_DO_NOT_WAIT) ==
					  DXGI_ERROR_WAS_STILL_DRAWING &&
				  a_owner.presentCalls == 1 &&
				  a_owner.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT,
			"Present routes exactly once with native result");

		DXGI_PRESENT_PARAMETERS parameters{};
		a_owner.present1Result = DXGI_STATUS_OCCLUDED;
		Check(a_proxy.Present1(0, 0, nullptr) == E_INVALIDARG &&
				  a_owner.present1Calls == 0,
			"Present1 rejects a missing parameter block");
		Check(a_proxy.Present1(1, DXGI_PRESENT_TEST, &parameters) ==
					  DXGI_STATUS_OCCLUDED &&
				  a_owner.present1Calls == 1 && a_owner.lastSyncInterval == 1 &&
				  a_owner.lastPresentFlags == DXGI_PRESENT_TEST,
			"empty Present1 metadata routes exactly once");
		parameters.DirtyRectsCount = 1;
		Check(a_proxy.Present1(0, 0, &parameters) == E_INVALIDARG &&
				  a_owner.present1Calls == 1,
			"malformed dirty metadata never reaches the owner");
		RECT dirty{ 0, 0, 16, 16 };
		parameters.pDirtyRects = &dirty;
		Check(a_proxy.Present1(0, 0, &parameters) == DXGI_ERROR_UNSUPPORTED &&
				  a_owner.present1Calls == 1,
			"partial presentation is explicitly unsupported");
		parameters = {};
		RECT scroll{ 0, 0, 16, 16 };
		parameters.pScrollRect = &scroll;
		Check(a_proxy.Present1(0, 0, &parameters) == E_INVALIDARG &&
				  a_owner.present1Calls == 1,
			"incomplete scroll metadata is rejected");
		POINT offset{};
		parameters.pScrollOffset = &offset;
		Check(a_proxy.Present1(0, 0, &parameters) == DXGI_ERROR_UNSUPPORTED &&
				  a_owner.present1Calls == 1,
			"scroll presentation is explicitly unsupported");

		a_owner.resizeResult = E_OUTOFMEMORY;
		Check(a_proxy.ResizeBuffers(2, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, 7) ==
					  E_OUTOFMEMORY &&
				  a_owner.resizeCalls == 1 &&
				  a_owner.lastResize == std::array<UINT, 4>{ 2, 1920, 1080, 7 },
			"ResizeBuffers preserves parameters and native failure");
		a_owner.resize1Result = DXGI_STATUS_MODE_CHANGED;
		Check(a_proxy.ResizeBuffers1(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0, nullptr,
				  nullptr) == DXGI_STATUS_MODE_CHANGED &&
				  a_owner.resize1Calls == 1 && !a_owner.sawNodeMask &&
				  !a_owner.sawPresentQueue,
			"ResizeBuffers1 routes the supported transaction");
		const UINT nodeMask = 1;
		Check(a_proxy.ResizeBuffers1(2, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
				  &nodeMask, nullptr) == DXGI_ERROR_UNSUPPORTED &&
				  a_owner.resize1Calls == 1,
			"ResizeBuffers1 rejects foreign queue metadata");
	}

	void TestInnerReplacement(cs::features::DXGISwapChainProxy& a_proxy,
		InnerSwapChain& a_original,
		InnerSwapChain& a_replacement)
	{
		IUnknown* before = nullptr;
		IUnknown* after = nullptr;
		Check(SUCCEEDED(a_proxy.QueryInterface(IID_PPV_ARGS(&before))) &&
				  a_proxy.SetPrivateData(
					  __uuidof(IDXGISwapChain4), 11, nullptr) == S_FALSE &&
				  a_original.privateDataSize == 11,
			"the original private chain receives direct forwarding");
		a_proxy.ReplaceInner(&a_replacement);
		Check(SUCCEEDED(a_proxy.QueryInterface(IID_PPV_ARGS(&after))) &&
				  before == after &&
				  a_proxy.SetPrivateData(
					  __uuidof(IDXGISwapChain4), 23, nullptr) == S_FALSE &&
				  a_original.privateDataSize == 11 &&
				  a_replacement.privateDataSize == 23,
			"private-chain replacement preserves facade identity and redirects "
			"direct forwarding");
		if (after) {
			after->Release();
		}
		if (before) {
			before->Release();
		}
	}

	void TestDetachSafety(cs::features::DXGISwapChainProxy& a_proxy)
	{
		Check(a_proxy.AddRef() == 2, "facade AddRef increments ownership");
		Check(a_proxy.Release() == 1, "facade Release preserves owner reference");
		a_proxy.DetachOwner();
		ID3D11Device* detachedDevice = reinterpret_cast<ID3D11Device*>(1);
		ID3D11Texture2D* detachedBuffer =
			reinterpret_cast<ID3D11Texture2D*>(1);
		Check(a_proxy.GetDevice(IID_PPV_ARGS(&detachedDevice)) ==
					  DXGI_ERROR_INVALID_CALL &&
				  detachedDevice == nullptr &&
				  a_proxy.GetBuffer(
					  0, IID_PPV_ARGS(&detachedBuffer)) ==
					  DXGI_ERROR_INVALID_CALL &&
				  detachedBuffer == nullptr &&
				  a_proxy.Present(0, 0) ==
					  DXGI_ERROR_INVALID_CALL &&
				  a_proxy.ResizeBuffers(
					  2, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, 0) ==
					  DXGI_ERROR_INVALID_CALL,
			"detached facade clears outputs and rejects runtime entry points");
	}
}  // namespace

int main()
{
	RecordingOwner owner;
	if (!CreateWarpResources(owner)) {
		std::cerr << "FAIL: WARP D3D11 resources could not be created\n";
		return 1;
	}
	InitializeFacadeDescriptions(owner);
	auto inner = winrt::make_self<InnerSwapChain>();
	inner->parent = owner.device.get();
	auto* proxy = new cs::features::DXGISwapChainProxy(owner, *inner.get());
	auto replacement = winrt::make_self<InnerSwapChain>();
	replacement->parent = owner.device.get();

	TestInterfacesAndIdentity(*proxy);
	TestDeviceAndBuffer(*proxy, owner);
	TestPresentAndResize(*proxy, owner);
	TestInnerReplacement(*proxy, *inner.get(), *replacement.get());
	TestDetachSafety(*proxy);
	proxy->Release();

	if (failures) {
		std::cerr << failures << " failure(s)\n";
		return 1;
	}
	std::cout << "DXGI swap-chain facade tests passed\n";
	return 0;
}
