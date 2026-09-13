#include "DXGISwapChainProxy.h"
#include "DXGISwapChainFacadeContract.h"

#include <d3d11.h>
#include <d3d12.h>
#include <winrt/base.h>

#include <array>
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

	class RecordingSwapChain final : public IDXGISwapChain4
	{
	public:
		HRESULT STDMETHODCALLTYPE QueryInterface(
			REFIID a_iid, void** a_object) noexcept override
		{
			if (!a_object) {
				return E_POINTER;
			}
			*a_object = nullptr;
			if (a_iid == __uuidof(IUnknown) ||
				a_iid == __uuidof(IDXGIObject) ||
				a_iid == __uuidof(IDXGIDeviceSubObject) ||
				a_iid == __uuidof(IDXGISwapChain) ||
				a_iid == __uuidof(IDXGISwapChain1) ||
				a_iid == __uuidof(IDXGISwapChain2) ||
				a_iid == __uuidof(IDXGISwapChain3) ||
				a_iid == __uuidof(IDXGISwapChain4)) {
				*a_object = static_cast<IDXGISwapChain4*>(this);
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() noexcept override
		{
			return ++references;
		}

		ULONG STDMETHODCALLTYPE Release() noexcept override
		{
			return --references;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(
			REFGUID, UINT a_size, const void*) noexcept override
		{
			privateDataSize = a_size;
			return privateDataResult;
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
			REFGUID, const IUnknown* a_unknown) noexcept override
		{
			privateDataInterface = a_unknown;
			return privateDataInterfaceResult;
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(
			REFGUID, UINT* a_size, void*) noexcept override
		{
			if (a_size) {
				*a_size = privateDataSize;
			}
			return getPrivateDataResult;
		}

		HRESULT STDMETHODCALLTYPE GetParent(
			REFIID, void** a_parent) noexcept override
		{
			if (!a_parent) {
				return E_POINTER;
			}
			*a_parent = parent;
			if (parent) {
				parent->AddRef();
			}
			return parentResult;
		}

		HRESULT STDMETHODCALLTYPE GetDevice(
			REFIID, void**) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE Present(UINT, UINT) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE GetBuffer(
			UINT, REFIID, void**) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE SetFullscreenState(
			BOOL, IDXGIOutput*) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE GetFullscreenState(
			BOOL*, IDXGIOutput**) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE GetDesc(
			DXGI_SWAP_CHAIN_DESC*) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffers(
			UINT, UINT, UINT, DXGI_FORMAT, UINT) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE ResizeTarget(
			const DXGI_MODE_DESC*) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE GetContainingOutput(
			IDXGIOutput** a_output) noexcept override
		{
			if (a_output) {
				*a_output = nullptr;
			}
			return containingOutputResult;
		}

		HRESULT STDMETHODCALLTYPE GetFrameStatistics(
			DXGI_FRAME_STATISTICS* a_stats) noexcept override
		{
			if (a_stats) {
				*a_stats = {};
				a_stats->PresentCount = 37;
			}
			return frameStatisticsResult;
		}

		HRESULT STDMETHODCALLTYPE GetLastPresentCount(
			UINT* a_count) noexcept override
		{
			if (a_count) {
				*a_count = 41;
			}
			return lastPresentCountResult;
		}

		HRESULT STDMETHODCALLTYPE GetDesc1(
			DXGI_SWAP_CHAIN_DESC1*) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE GetFullscreenDesc(
			DXGI_SWAP_CHAIN_FULLSCREEN_DESC*) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE GetHwnd(HWND*) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE GetCoreWindow(
			REFIID, void** a_window) noexcept override
		{
			if (a_window) {
				*a_window = nullptr;
			}
			return coreWindowResult;
		}

		HRESULT STDMETHODCALLTYPE Present1(
			UINT,
			UINT,
			const DXGI_PRESENT_PARAMETERS*) noexcept override
		{
			return E_UNEXPECTED;
		}

		BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() noexcept override
		{
			return temporaryMonoSupported;
		}

		HRESULT STDMETHODCALLTYPE GetRestrictToOutput(
			IDXGIOutput** a_output) noexcept override
		{
			if (a_output) {
				*a_output = nullptr;
			}
			return restrictOutputResult;
		}

		HRESULT STDMETHODCALLTYPE SetBackgroundColor(
			const DXGI_RGBA* a_color) noexcept override
		{
			if (a_color) {
				backgroundColor = *a_color;
			}
			return backgroundColorResult;
		}

		HRESULT STDMETHODCALLTYPE GetBackgroundColor(
			DXGI_RGBA* a_color) noexcept override
		{
			if (a_color) {
				*a_color = backgroundColor;
			}
			return backgroundColorResult;
		}

		HRESULT STDMETHODCALLTYPE SetRotation(
			DXGI_MODE_ROTATION a_rotation) noexcept override
		{
			rotation = a_rotation;
			return rotationResult;
		}

		HRESULT STDMETHODCALLTYPE GetRotation(
			DXGI_MODE_ROTATION* a_rotation) noexcept override
		{
			if (a_rotation) {
				*a_rotation = rotation;
			}
			return rotationResult;
		}

		HRESULT STDMETHODCALLTYPE SetSourceSize(
			UINT a_width, UINT a_height) noexcept override
		{
			sourceWidth = a_width;
			sourceHeight = a_height;
			return sourceSizeResult;
		}

		HRESULT STDMETHODCALLTYPE GetSourceSize(
			UINT* a_width, UINT* a_height) noexcept override
		{
			if (a_width) {
				*a_width = sourceWidth;
			}
			if (a_height) {
				*a_height = sourceHeight;
			}
			return sourceSizeResult;
		}

		HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(
			UINT a_maxLatency) noexcept override
		{
			maximumFrameLatency = a_maxLatency;
			return frameLatencyResult;
		}

		HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(
			UINT* a_maxLatency) noexcept override
		{
			if (a_maxLatency) {
				*a_maxLatency = maximumFrameLatency;
			}
			return frameLatencyResult;
		}

		HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject()
			noexcept override
		{
			return frameLatencyHandle;
		}

		HRESULT STDMETHODCALLTYPE SetMatrixTransform(
			const DXGI_MATRIX_3X2_F* a_matrix) noexcept override
		{
			if (a_matrix) {
				matrix = *a_matrix;
			}
			return matrixResult;
		}

		HRESULT STDMETHODCALLTYPE GetMatrixTransform(
			DXGI_MATRIX_3X2_F* a_matrix) noexcept override
		{
			if (a_matrix) {
				*a_matrix = matrix;
			}
			return matrixResult;
		}

		UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() noexcept override
		{
			return 1;
		}

		HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(
			DXGI_COLOR_SPACE_TYPE, UINT*) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE SetColorSpace1(
			DXGI_COLOR_SPACE_TYPE) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffers1(
			UINT,
			UINT,
			UINT,
			DXGI_FORMAT,
			UINT,
			const UINT*,
			IUnknown* const*) noexcept override
		{
			return E_UNEXPECTED;
		}

		HRESULT STDMETHODCALLTYPE SetHDRMetaData(
			DXGI_HDR_METADATA_TYPE,
			UINT,
			void*) noexcept override
		{
			return E_UNEXPECTED;
		}

		ULONG references = 1;
		IUnknown* parent = nullptr;
		const IUnknown* privateDataInterface = nullptr;
		UINT privateDataSize = 0;
		DXGI_RGBA backgroundColor{};
		DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
		DXGI_MATRIX_3X2_F matrix{};
		UINT sourceWidth = 0;
		UINT sourceHeight = 0;
		UINT maximumFrameLatency = 0;
		HANDLE frameLatencyHandle =
			reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x1234));
		BOOL temporaryMonoSupported = TRUE;
		HRESULT privateDataResult = S_FALSE;
		HRESULT privateDataInterfaceResult = DXGI_STATUS_OCCLUDED;
		HRESULT getPrivateDataResult = S_OK;
		HRESULT parentResult = S_OK;
		HRESULT containingOutputResult = DXGI_ERROR_NOT_FOUND;
		HRESULT frameStatisticsResult = DXGI_STATUS_OCCLUDED;
		HRESULT lastPresentCountResult = S_OK;
		HRESULT coreWindowResult = E_NOINTERFACE;
		HRESULT restrictOutputResult = DXGI_ERROR_NOT_FOUND;
		HRESULT backgroundColorResult = S_OK;
		HRESULT rotationResult = S_FALSE;
		HRESULT sourceSizeResult = DXGI_STATUS_MODE_CHANGED;
		HRESULT frameLatencyResult = S_OK;
		HRESULT matrixResult = DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;
	};

	class RecordingOwner final :
		public cs::features::IDXGISwapChainProxyOwner
	{
	public:
		HRESULT Present(UINT a_syncInterval, UINT a_flags) noexcept override
		{
			++presentCalls;
			lastSyncInterval = a_syncInterval;
			lastPresentFlags = a_flags;
			return presentResult;
		}

		HRESULT Present1(
			UINT a_syncInterval,
			UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept override
		{
			++present1Calls;
			lastSyncInterval = a_syncInterval;
			lastPresentFlags = a_flags;
			present1Parameters = a_parameters ? *a_parameters :
				DXGI_PRESENT_PARAMETERS{};
			return present1Result;
		}

		HRESULT GetBuffer(
			UINT a_buffer, REFIID a_iid, void** a_surface) noexcept override
		{
			if (!a_surface) {
				return E_POINTER;
			}
			*a_surface = nullptr;
			if (a_buffer != 0 || !buffer) {
				return DXGI_ERROR_INVALID_CALL;
			}
			return buffer->QueryInterface(a_iid, a_surface);
		}

		HRESULT GetDevice(
			REFIID a_iid, void** a_device) noexcept override
		{
			if (!a_device) {
				return E_POINTER;
			}
			*a_device = nullptr;
			return device
				? device->QueryInterface(a_iid, a_device)
				: E_NOINTERFACE;
		}

		HRESULT SetFullscreenState(
			BOOL a_fullscreen, IDXGIOutput* a_target) noexcept override
		{
			fullscreen = a_fullscreen;
			fullscreenTarget = a_target;
			return fullscreenResult;
		}

		HRESULT GetFullscreenState(
			BOOL* a_fullscreen, IDXGIOutput** a_target) noexcept override
		{
			if (a_fullscreen) {
				*a_fullscreen = fullscreen;
			}
			if (a_target) {
				*a_target = nullptr;
			}
			return fullscreenResult;
		}

		HRESULT GetDesc(
			DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override
		{
			if (!a_desc) {
				return E_POINTER;
			}
			*a_desc = desc;
			return descResult;
		}

		HRESULT ResizeBuffers(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept override
		{
			++resizeCalls;
			lastBufferCount = a_bufferCount;
			lastWidth = a_width;
			lastHeight = a_height;
			lastFormat = a_format;
			lastResizeFlags = a_flags;
			return resizeResult;
		}

		HRESULT ResizeTarget(
			const DXGI_MODE_DESC* a_target) noexcept override
		{
			resizeTarget = a_target;
			return resizeTargetResult;
		}

		HRESULT GetDesc1(
			DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept override
		{
			if (!a_desc) {
				return E_POINTER;
			}
			*a_desc = desc1;
			return desc1Result;
		}

		HRESULT GetFullscreenDesc(
			DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept override
		{
			if (!a_desc) {
				return E_POINTER;
			}
			*a_desc = fullscreenDesc;
			return fullscreenDescResult;
		}

		HRESULT GetHwnd(HWND* a_window) noexcept override
		{
			if (!a_window) {
				return E_POINTER;
			}
			*a_window = window;
			return hwndResult;
		}

		UINT GetCurrentBackBufferIndex() noexcept override
		{
			return currentBackBufferIndex;
		}

		HRESULT CheckColorSpaceSupport(
			DXGI_COLOR_SPACE_TYPE a_colorSpace,
			UINT* a_support) noexcept override
		{
			lastColorSpace = a_colorSpace;
			if (a_support) {
				*a_support = colorSpaceSupport;
			}
			return colorSpaceResult;
		}

		HRESULT SetColorSpace1(
			DXGI_COLOR_SPACE_TYPE a_colorSpace) noexcept override
		{
			lastColorSpace = a_colorSpace;
			return setColorSpaceResult;
		}

		HRESULT ResizeBuffers1(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags,
			const UINT* a_creationNodeMask,
			IUnknown* const* a_presentQueue) noexcept override
		{
			++resize1Calls;
			lastBufferCount = a_bufferCount;
			lastWidth = a_width;
			lastHeight = a_height;
			lastFormat = a_format;
			lastResizeFlags = a_flags;
			sawNodeMask = a_creationNodeMask != nullptr;
			sawPresentQueue = a_presentQueue != nullptr;
			return resize1Result;
		}

		HRESULT SetHDRMetaData(
			DXGI_HDR_METADATA_TYPE a_type,
			UINT a_size,
			void* a_metadata) noexcept override
		{
			lastHdrType = a_type;
			lastHdrSize = a_size;
			lastHdrMetadata = a_metadata;
			return hdrResult;
		}

		winrt::com_ptr<ID3D11Device> device;
		winrt::com_ptr<ID3D11Texture2D> buffer;
		DXGI_SWAP_CHAIN_DESC desc{};
		DXGI_SWAP_CHAIN_DESC1 desc1{};
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc{};
		DXGI_PRESENT_PARAMETERS present1Parameters{};
		const DXGI_MODE_DESC* resizeTarget = nullptr;
		IDXGIOutput* fullscreenTarget = nullptr;
		HWND window = reinterpret_cast<HWND>(
			static_cast<std::uintptr_t>(0x5678));
		UINT presentCalls = 0;
		UINT present1Calls = 0;
		UINT resizeCalls = 0;
		UINT resize1Calls = 0;
		UINT lastSyncInterval = 0;
		UINT lastPresentFlags = 0;
		UINT lastBufferCount = 0;
		UINT lastWidth = 0;
		UINT lastHeight = 0;
		UINT lastResizeFlags = 0;
		UINT currentBackBufferIndex = 0;
		UINT colorSpaceSupport =
			DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
		UINT lastHdrSize = 0;
		DXGI_FORMAT lastFormat = DXGI_FORMAT_UNKNOWN;
		DXGI_COLOR_SPACE_TYPE lastColorSpace =
			DXGI_COLOR_SPACE_CUSTOM;
		DXGI_HDR_METADATA_TYPE lastHdrType =
			DXGI_HDR_METADATA_TYPE_NONE;
		void* lastHdrMetadata = nullptr;
		BOOL fullscreen = FALSE;
		bool sawNodeMask = false;
		bool sawPresentQueue = false;
		HRESULT presentResult = S_OK;
		HRESULT present1Result = S_OK;
		HRESULT resizeResult = S_OK;
		HRESULT resize1Result = S_OK;
		HRESULT fullscreenResult = S_OK;
		HRESULT resizeTargetResult = DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
		HRESULT descResult = S_OK;
		HRESULT desc1Result = S_OK;
		HRESULT fullscreenDescResult = S_OK;
		HRESULT hwndResult = S_OK;
		HRESULT colorSpaceResult = S_OK;
		HRESULT setColorSpaceResult = S_OK;
		HRESULT hdrResult = S_OK;
	};

	bool CreateWarpResources(RecordingOwner& a_owner)
	{
		D3D_FEATURE_LEVEL featureLevel{};
		if (FAILED(D3D11CreateDevice(
				nullptr,
				D3D_DRIVER_TYPE_WARP,
				nullptr,
				0,
				nullptr,
				0,
				D3D11_SDK_VERSION,
				a_owner.device.put(),
				&featureLevel,
				nullptr))) {
			return false;
		}
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = 64;
		desc.Height = 64;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		return SUCCEEDED(a_owner.device->CreateTexture2D(
			&desc, nullptr, a_owner.buffer.put()));
	}

	void InitializeFacadeDescriptions(RecordingOwner& a_owner)
	{
		DXGI_SWAP_CHAIN_DESC requested{};
		requested.BufferDesc.Width = 1280;
		requested.BufferDesc.Height = 720;
		requested.BufferDesc.RefreshRate = { 60, 1 };
		requested.BufferDesc.Format =
			DXGI_FORMAT_R8G8B8A8_UNORM;
		requested.SampleDesc = { 1, 0 };
		requested.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		requested.BufferCount = 1;
		requested.OutputWindow = a_owner.window;
		requested.Windowed = TRUE;
		requested.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		requested.Flags =
			DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
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
			.Flags =
				DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
				DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
		};
		a_owner.desc =
			cs::features::swap_chain_facade::BuildDescription(
				requested, inner);
		a_owner.desc1 =
			cs::features::swap_chain_facade::BuildDescription1(
				a_owner.desc);
		a_owner.fullscreenDesc =
			cs::features::swap_chain_facade::
				BuildFullscreenDescription(a_owner.desc);
		Check(
			a_owner.desc.BufferCount == 2 &&
				a_owner.desc.SwapEffect ==
					DXGI_SWAP_EFFECT_DISCARD &&
				a_owner.desc.Flags == 0 &&
				a_owner.desc.BufferDesc.Width == 1920 &&
				a_owner.desc.BufferDesc.Height == 1080,
			"production facade descriptor keeps two discard-model buffers and strips flip-only flags");
		Check(
			cs::features::swap_chain_facade::
					SupportsResizeBufferCount(0, a_owner.desc) &&
				cs::features::swap_chain_facade::
					SupportsResizeBufferCount(2, a_owner.desc) &&
				!cs::features::swap_chain_facade::
					SupportsResizeBufferCount(1, a_owner.desc),
			"production resize validation preserves zero and two-buffer callers");
		Check(
			cs::features::swap_chain_facade::PreservePrivateResizeFlags(
				a_owner.desc.Flags, inner) == inner.Flags,
			"resizing from the outward descriptor retains hidden creation-only flip flags");
		Check(
			cs::features::swap_chain_facade::PreservePrivateResizeFlags(
				DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH, inner) ==
				(inner.Flags | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH),
			"private flag preservation does not discard other native resize requests");
		auto nonWaitableInner = inner;
		nonWaitableInner.Flags = 0;
		Check(
			cs::features::swap_chain_facade::PreservePrivateResizeFlags(
				0, nonWaitableInner) == 0,
			"private resize translation does not introduce unused capabilities");
	}

	void TestInterfacesAndIdentity(
		cs::features::DXGISwapChainProxy* a_proxy)
	{
		IUnknown* canonical = nullptr;
		Check(
			SUCCEEDED(a_proxy->QueryInterface(
				IID_PPV_ARGS(&canonical))) && canonical,
			"IUnknown query succeeds");
		for (const IID* iid : std::array{
				 &__uuidof(IDXGISwapChain),
				 &__uuidof(IDXGISwapChain1),
				 &__uuidof(IDXGISwapChain2),
				 &__uuidof(IDXGISwapChain3),
				 &__uuidof(IDXGISwapChain4) }) {
			void* version = nullptr;
			Check(
				SUCCEEDED(a_proxy->QueryInterface(*iid, &version)) &&
					version,
				"every swap-chain version is exposed");
			IUnknown* identity = nullptr;
			if (version) {
				static_cast<IUnknown*>(version)->QueryInterface(
					IID_PPV_ARGS(&identity));
			}
			Check(
				identity == canonical,
				"every swap-chain version has the facade IUnknown identity");
			if (identity) {
				identity->Release();
			}
			if (version) {
				static_cast<IUnknown*>(version)->Release();
			}
		}
		Check(
			a_proxy != nullptr,
			"version queries retain the facade object");
		const IID unsupported{
			0x57b53c9f,
			0x04a5,
			0x4e2f,
			{ 0x86, 0x20, 0x56, 0x7a, 0xa9, 0x80, 0x9b, 0x70 }
		};
		void* escaped = reinterpret_cast<void*>(1);
		Check(
			a_proxy->QueryInterface(unsupported, &escaped) ==
					E_NOINTERFACE &&
				escaped == nullptr,
			"unknown QueryInterface cannot escape the facade");
		escaped = reinterpret_cast<void*>(1);
		Check(
			a_proxy->QueryInterface(
				__uuidof(IDXGISwapChainMedia), &escaped) ==
					E_NOINTERFACE &&
				escaped == nullptr,
			"adjacent inner swap-chain interfaces cannot bypass facade ownership");
		canonical->Release();
	}

	void TestD3D11DeviceAndBufferCoherence(
		cs::features::DXGISwapChainProxy* a_proxy,
		RecordingOwner& a_owner)
	{
		ID3D11Device* device = nullptr;
		Check(
			SUCCEEDED(a_proxy->GetDevice(IID_PPV_ARGS(&device))) &&
				device == a_owner.device.get(),
			"GetDevice returns the outward D3D11 device");
		if (device) {
			device->Release();
		}
		ID3D12Device* wrongDevice =
			reinterpret_cast<ID3D12Device*>(1);
		Check(
			a_proxy->GetDevice(
				IID_PPV_ARGS(&wrongDevice)) == E_NOINTERFACE &&
				wrongDevice == nullptr,
			"GetDevice does not expose the inner D3D12 device");
		ID3D11Texture2D* buffer = nullptr;
		Check(
			SUCCEEDED(a_proxy->GetBuffer(
				0, IID_PPV_ARGS(&buffer))) &&
				buffer == a_owner.buffer.get(),
			"GetBuffer zero returns the outward D3D11 proxy texture");
		if (buffer) {
			buffer->Release();
		}
		buffer = reinterpret_cast<ID3D11Texture2D*>(1);
		Check(
			a_proxy->GetBuffer(
				1, IID_PPV_ARGS(&buffer)) ==
					DXGI_ERROR_INVALID_CALL &&
				buffer == nullptr,
			"discard-model GetBuffer rejects nonzero indexes");
	}

	void TestPresentRoutes(
		cs::features::DXGISwapChainProxy* a_proxy,
		RecordingOwner& a_owner)
	{
		a_owner.presentResult = DXGI_ERROR_WAS_STILL_DRAWING;
		Check(
			a_proxy->Present(0, DXGI_PRESENT_DO_NOT_WAIT) ==
					DXGI_ERROR_WAS_STILL_DRAWING &&
				a_owner.presentCalls == 1 &&
				a_owner.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT,
			"Present routes once and preserves flags and HRESULT");

		Check(
			a_proxy->Present1(0, 0, nullptr) == E_INVALIDARG &&
				a_owner.present1Calls == 0,
			"Present1 rejects a null parameter block without presenting");
		DXGI_PRESENT_PARAMETERS parameters{};
		a_owner.present1Result = DXGI_STATUS_OCCLUDED;
		Check(
			a_proxy->Present1(
				1, DXGI_PRESENT_TEST, &parameters) ==
					DXGI_STATUS_OCCLUDED &&
				a_owner.present1Calls == 1 &&
				a_owner.lastSyncInterval == 1 &&
				a_owner.lastPresentFlags == DXGI_PRESENT_TEST,
			"empty Present1 metadata routes exactly once");

		parameters.DirtyRectsCount = 1;
		Check(
			a_proxy->Present1(0, 0, &parameters) == E_INVALIDARG &&
				a_owner.present1Calls == 1,
			"Present1 rejects malformed dirty-rectangle metadata");
		RECT dirty{ 0, 0, 16, 16 };
		parameters.pDirtyRects = &dirty;
		Check(
			a_proxy->Present1(0, 0, &parameters) ==
					DXGI_ERROR_UNSUPPORTED &&
				a_owner.present1Calls == 1,
			"Present1 explicitly rejects partial-presentation metadata");
		parameters = {};
		RECT scroll{ 0, 0, 16, 16 };
		parameters.pScrollRect = &scroll;
		Check(
			a_proxy->Present1(0, 0, &parameters) == E_INVALIDARG &&
				a_owner.present1Calls == 1,
			"Present1 rejects an incomplete scroll pair");
	}

	void TestResizeRoutes(
		cs::features::DXGISwapChainProxy* a_proxy,
		RecordingOwner& a_owner)
	{
		a_owner.resizeResult = E_OUTOFMEMORY;
		Check(
			a_proxy->ResizeBuffers(
				2, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, 7) ==
					E_OUTOFMEMORY &&
				a_owner.resizeCalls == 1 &&
				a_owner.lastBufferCount == 2 &&
				a_owner.lastWidth == 1920 &&
				a_owner.lastHeight == 1080 &&
				a_owner.lastResizeFlags == 7,
			"ResizeBuffers preserves the transaction parameters and HRESULT");

		a_owner.resize1Result = DXGI_STATUS_MODE_CHANGED;
		Check(
			a_proxy->ResizeBuffers1(
				0, 0, 0, DXGI_FORMAT_UNKNOWN, 0, nullptr, nullptr) ==
					DXGI_STATUS_MODE_CHANGED &&
				a_owner.resize1Calls == 1 &&
				!a_owner.sawNodeMask &&
				!a_owner.sawPresentQueue,
			"ResizeBuffers1 null metadata routes through the owner transaction");
		const UINT nodeMask = 1;
		Check(
			a_proxy->ResizeBuffers1(
				1, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM,
				0, &nodeMask, nullptr) == DXGI_ERROR_UNSUPPORTED &&
				a_owner.resize1Calls == 1,
			"ResizeBuffers1 rejects node-mask metadata");
		IUnknown* queue = a_owner.device.get();
		Check(
			a_proxy->ResizeBuffers1(
				1, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM,
				0, nullptr, &queue) == DXGI_ERROR_UNSUPPORTED &&
				a_owner.resize1Calls == 1,
			"ResizeBuffers1 rejects a foreign presentation queue");
	}

	void TestForwardedMethods(
		cs::features::DXGISwapChainProxy* a_proxy,
		RecordingSwapChain& a_inner,
		RecordingOwner& a_owner)
	{
		Check(
			a_proxy->SetPrivateData(__uuidof(IDXGISwapChain4), 19, nullptr) ==
					a_inner.privateDataResult &&
				a_inner.privateDataSize == 19,
			"IDXGIObject private data forwards unchanged");
		Check(
			a_proxy->SetPrivateDataInterface(
				__uuidof(IDXGISwapChain4), a_owner.device.get()) ==
					a_inner.privateDataInterfaceResult &&
				a_inner.privateDataInterface == a_owner.device.get(),
			"IDXGIObject interface private data forwards unchanged");
		UINT privateDataSize = 0;
		Check(
			a_proxy->GetPrivateData(
				__uuidof(IDXGISwapChain4),
				&privateDataSize,
				nullptr) == a_inner.getPrivateDataResult &&
				privateDataSize == 19,
			"IDXGIObject private-data reads preserve output and HRESULT");
		IUnknown* parent = nullptr;
		Check(
			SUCCEEDED(a_proxy->GetParent(
				IID_PPV_ARGS(&parent))) &&
				parent == a_owner.device.get(),
			"GetParent forwards with caller-owned output");
		if (parent) {
			parent->Release();
		}
		IDXGIOutput* output = reinterpret_cast<IDXGIOutput*>(1);
		Check(
			a_proxy->GetContainingOutput(&output) ==
					a_inner.containingOutputResult &&
				output == nullptr,
			"GetContainingOutput forwards its HRESULT and null output");
		DXGI_FRAME_STATISTICS statistics{};
		Check(
			a_proxy->GetFrameStatistics(&statistics) ==
					a_inner.frameStatisticsResult &&
				statistics.PresentCount == 37,
			"GetFrameStatistics forwards output and HRESULT");
		UINT presentCount = 0;
		Check(
			SUCCEEDED(a_proxy->GetLastPresentCount(&presentCount)) &&
				presentCount == 41,
			"GetLastPresentCount forwards output and HRESULT");
		void* coreWindow = reinterpret_cast<void*>(1);
		Check(
			a_proxy->GetCoreWindow(
				__uuidof(IUnknown), &coreWindow) ==
					a_inner.coreWindowResult &&
				coreWindow == nullptr,
			"GetCoreWindow forwards failure with a null output");
		Check(
			a_proxy->IsTemporaryMonoSupported() ==
				a_inner.temporaryMonoSupported,
			"temporary-mono capability forwards");
		output = reinterpret_cast<IDXGIOutput*>(1);
		Check(
			a_proxy->GetRestrictToOutput(&output) ==
					a_inner.restrictOutputResult &&
				output == nullptr,
			"restrict-output query forwards failure with a null output");
		const DXGI_RGBA color{ 0.1f, 0.2f, 0.3f, 0.4f };
		Check(
			SUCCEEDED(a_proxy->SetBackgroundColor(&color)) &&
				a_inner.backgroundColor.g == color.g,
			"background color forwards unchanged");
		Check(
			a_proxy->SetRotation(DXGI_MODE_ROTATION_ROTATE90) ==
					DXGI_ERROR_INVALID_CALL &&
				a_inner.rotation == DXGI_MODE_ROTATION_IDENTITY,
			"discard-model facade rejects flip-only rotation");
		DXGI_MODE_ROTATION rotation =
			DXGI_MODE_ROTATION_UNSPECIFIED;
		Check(
			SUCCEEDED(a_proxy->GetRotation(&rotation)) &&
				rotation == DXGI_MODE_ROTATION_IDENTITY,
			"discard-model facade reports identity rotation");
		Check(
			a_proxy->SetSourceSize(800, 600) ==
					DXGI_ERROR_INVALID_CALL &&
				a_inner.sourceWidth == 0 &&
				a_inner.sourceHeight == 0,
			"discard-model facade rejects source-region mutation");
		UINT sourceWidth = 1;
		UINT sourceHeight = 1;
		Check(
			a_proxy->GetSourceSize(
				&sourceWidth, &sourceHeight) ==
					DXGI_ERROR_INVALID_CALL &&
				sourceWidth == 0 && sourceHeight == 0,
			"discard-model source query fails with initialized outputs");
		Check(
			a_proxy->SetMaximumFrameLatency(3) ==
					DXGI_ERROR_INVALID_CALL &&
				a_inner.maximumFrameLatency == 0,
			"facade rejects waitable latency without advertising its flag");
		UINT maximumLatency = 1;
		Check(
			a_proxy->GetMaximumFrameLatency(&maximumLatency) ==
					DXGI_ERROR_INVALID_CALL &&
				maximumLatency == 0,
			"frame-latency query fails with an initialized output");
		Check(
			a_proxy->GetFrameLatencyWaitableObject() == nullptr,
			"facade exposes no waitable handle when its flag is absent");
		const DXGI_MATRIX_3X2_F matrix{
			1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f
		};
		Check(
			a_proxy->SetMatrixTransform(&matrix) ==
					DXGI_ERROR_INVALID_CALL &&
				a_inner.matrix._31 == 0.0f,
			"HWND discard-model facade rejects composition transforms");
		DXGI_MATRIX_3X2_F observedMatrix{ 1, 1, 1, 1, 1, 1 };
		Check(
			a_proxy->GetMatrixTransform(&observedMatrix) ==
					DXGI_ERROR_INVALID_CALL &&
				observedMatrix._11 == 0.0f,
			"composition transform query fails with an initialized output");
		DXGI_SWAP_CHAIN_DESC desc{};
		Check(
			SUCCEEDED(a_proxy->GetDesc(&desc)) &&
				desc.BufferCount == 2 &&
				desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD &&
				desc.Flags == 0,
			"legacy description exposes a coherent discard-model facade");
		DXGI_SWAP_CHAIN_DESC1 desc1{};
		Check(
			SUCCEEDED(a_proxy->GetDesc1(&desc1)) &&
				desc1.BufferCount == 2 &&
				desc1.SwapEffect == DXGI_SWAP_EFFECT_DISCARD &&
				desc1.Flags == 0,
			"versioned description matches the discard-model facade");
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc{};
		Check(
			SUCCEEDED(a_proxy->GetFullscreenDesc(&fullscreenDesc)) &&
				fullscreenDesc.Windowed,
			"fullscreen description comes from the outward facade");
		HWND window = nullptr;
		Check(
			SUCCEEDED(a_proxy->GetHwnd(&window)) &&
				window == a_owner.window,
			"GetHwnd returns the outward window");
		BOOL fullscreen = TRUE;
		Check(
			SUCCEEDED(a_proxy->GetFullscreenState(
				&fullscreen, nullptr)) &&
				!fullscreen,
			"fullscreen state follows the outward windowed facade");
		Check(
			a_proxy->GetFullscreenState(nullptr, nullptr) == E_POINTER,
			"fullscreen state requires its primary output");
		Check(
			a_proxy->GetCurrentBackBufferIndex() ==
				a_owner.currentBackBufferIndex,
			"back-buffer index comes from the outward facade contract");
		UINT support = 0;
		Check(
			SUCCEEDED(a_proxy->CheckColorSpaceSupport(
				DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
				&support)) &&
				support == a_owner.colorSpaceSupport,
			"color-space capability is filtered by the facade owner");
		a_owner.setColorSpaceResult = DXGI_STATUS_MODE_CHANGED;
		Check(
			a_proxy->SetColorSpace1(
				DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) ==
					DXGI_STATUS_MODE_CHANGED &&
				a_owner.lastColorSpace ==
					DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
			"SetColorSpace1 preserves the facade result");
		a_owner.hdrResult = DXGI_ERROR_UNSUPPORTED;
		Check(
			a_proxy->SetHDRMetaData(
				DXGI_HDR_METADATA_TYPE_HDR10, 1, &support) ==
					DXGI_ERROR_UNSUPPORTED &&
				a_owner.lastHdrType == DXGI_HDR_METADATA_TYPE_HDR10,
			"HDR metadata result and parameters are preserved");
	}
}

int main()
{
	RecordingOwner owner;
	if (!CreateWarpResources(owner)) {
		std::cerr << "FAIL: WARP D3D11 resources could not be created\n";
		return 1;
	}
	InitializeFacadeDescriptions(owner);
	RecordingSwapChain inner;
	inner.parent = owner.device.get();
	auto* proxy = new cs::features::DXGISwapChainProxy(owner, inner);

	TestInterfacesAndIdentity(proxy);
	TestD3D11DeviceAndBufferCoherence(proxy, owner);
	TestPresentRoutes(proxy, owner);
	TestResizeRoutes(proxy, owner);
	TestForwardedMethods(proxy, inner, owner);

	Check(proxy->AddRef() == 2, "facade AddRef increments ownership");
	Check(proxy->Release() == 1, "facade Release preserves the owner reference");
	proxy->DetachOwner();
	ID3D11Device* detachedDevice =
		reinterpret_cast<ID3D11Device*>(1);
	Check(
		proxy->GetDevice(
			IID_PPV_ARGS(&detachedDevice)) ==
				DXGI_ERROR_INVALID_CALL &&
			detachedDevice == nullptr,
		"a surviving external facade reference cannot access a destroyed owner");
	proxy->Release();

	if (failures) {
		std::cerr << failures << " failure(s)\n";
		return 1;
	}
	std::cout << "DXGI swap-chain facade tests passed\n";
	return 0;
}
