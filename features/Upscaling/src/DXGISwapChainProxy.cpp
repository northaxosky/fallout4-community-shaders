#include "DXGISwapChainProxy.h"

namespace cs::features
{
	DXGISwapChainProxy::DXGISwapChainProxy(
		IDXGISwapChainProxyOwner& a_owner,
		IDXGISwapChain4& a_inner) noexcept :
		_owner(&a_owner)
	{
		_inner.copy_from(&a_inner);
	}

	void DXGISwapChainProxy::DetachOwner() noexcept
	{
		_owner = nullptr;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(
		REFIID a_iid,
		void** a_object) noexcept
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

	ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef() noexcept
	{
		return _references.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release() noexcept
	{
		const auto remaining =
			_references.fetch_sub(1, std::memory_order_acq_rel) - 1;
		if (!remaining) {
			delete this;
		}
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(
		REFGUID a_name,
		UINT a_size,
		const void* a_data) noexcept
	{
		return _inner
			? _inner->SetPrivateData(a_name, a_size, a_data)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(
		REFGUID a_name,
		const IUnknown* a_unknown) noexcept
	{
		return _inner
			? _inner->SetPrivateDataInterface(a_name, a_unknown)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(
		REFGUID a_name,
		UINT* a_size,
		void* a_data) noexcept
	{
		return _inner
			? _inner->GetPrivateData(a_name, a_size, a_data)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(
		REFIID a_iid,
		void** a_parent) noexcept
	{
		if (!a_parent) {
			return E_POINTER;
		}
		*a_parent = nullptr;
		return _inner
			? _inner->GetParent(a_iid, a_parent)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(
		REFIID a_iid,
		void** a_device) noexcept
	{
		if (!a_device) {
			return E_POINTER;
		}
		*a_device = nullptr;
		return _owner
			? _owner->GetDevice(a_iid, a_device)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(
		UINT a_syncInterval,
		UINT a_flags) noexcept
	{
		return _owner
			? _owner->Present(a_syncInterval, a_flags)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(
		UINT a_buffer,
		REFIID a_iid,
		void** a_surface) noexcept
	{
		if (!a_surface) {
			return E_POINTER;
		}
		*a_surface = nullptr;
		return _owner
			? _owner->GetBuffer(a_buffer, a_iid, a_surface)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(
		BOOL a_fullscreen,
		IDXGIOutput* a_target) noexcept
	{
		return _owner
			? _owner->SetFullscreenState(a_fullscreen, a_target)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(
		BOOL* a_fullscreen,
		IDXGIOutput** a_target) noexcept
	{
		if (!a_fullscreen) {
			if (a_target) {
				*a_target = nullptr;
			}
			return E_POINTER;
		}
		*a_fullscreen = FALSE;
		if (a_target) {
			*a_target = nullptr;
		}
		return _owner
			? _owner->GetFullscreenState(a_fullscreen, a_target)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(
		DXGI_SWAP_CHAIN_DESC* a_desc) noexcept
	{
		return _owner
			? _owner->GetDesc(a_desc)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags) noexcept
	{
		return _owner
			? _owner->ResizeBuffers(
				a_bufferCount, a_width, a_height, a_format, a_flags)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(
		const DXGI_MODE_DESC* a_target) noexcept
	{
		return _owner
			? _owner->ResizeTarget(a_target)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(
		IDXGIOutput** a_output) noexcept
	{
		if (!a_output) {
			return E_POINTER;
		}
		*a_output = nullptr;
		return _inner
			? _inner->GetContainingOutput(a_output)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(
		DXGI_FRAME_STATISTICS* a_stats) noexcept
	{
		return _inner
			? _inner->GetFrameStatistics(a_stats)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(
		UINT* a_count) noexcept
	{
		return _inner
			? _inner->GetLastPresentCount(a_count)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc1(
		DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept
	{
		return _owner
			? _owner->GetDesc1(a_desc)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenDesc(
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept
	{
		return _owner
			? _owner->GetFullscreenDesc(a_desc)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetHwnd(HWND* a_window) noexcept
	{
		return _owner
			? _owner->GetHwnd(a_window)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetCoreWindow(
		REFIID a_iid,
		void** a_window) noexcept
	{
		if (!a_window) {
			return E_POINTER;
		}
		*a_window = nullptr;
		return _inner
			? _inner->GetCoreWindow(a_iid, a_window)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present1(
		UINT a_syncInterval,
		UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept
	{
		if (!a_parameters) {
			return E_INVALIDARG;
		}
		if ((a_parameters->DirtyRectsCount && !a_parameters->pDirtyRects) ||
			((a_parameters->pScrollRect == nullptr) !=
				(a_parameters->pScrollOffset == nullptr))) {
			return E_INVALIDARG;
		}
		if (a_parameters->DirtyRectsCount ||
			a_parameters->pScrollRect ||
			a_parameters->pScrollOffset) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		return _owner
			? _owner->Present1(
				a_syncInterval, a_flags, a_parameters)
			: DXGI_ERROR_INVALID_CALL;
	}

	BOOL STDMETHODCALLTYPE DXGISwapChainProxy::IsTemporaryMonoSupported() noexcept
	{
		return _inner
			? _inner->IsTemporaryMonoSupported()
			: FALSE;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRestrictToOutput(
		IDXGIOutput** a_output) noexcept
	{
		if (!a_output) {
			return E_POINTER;
		}
		*a_output = nullptr;
		return _inner
			? _inner->GetRestrictToOutput(a_output)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetBackgroundColor(
		const DXGI_RGBA* a_color) noexcept
	{
		return _inner
			? _inner->SetBackgroundColor(a_color)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBackgroundColor(
		DXGI_RGBA* a_color) noexcept
	{
		return _inner
			? _inner->GetBackgroundColor(a_color)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetRotation(
		DXGI_MODE_ROTATION) noexcept
	{
		return DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRotation(
		DXGI_MODE_ROTATION* a_rotation) noexcept
	{
		if (!a_rotation) {
			return E_INVALIDARG;
		}
		*a_rotation = DXGI_MODE_ROTATION_IDENTITY;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetSourceSize(
		UINT,
		UINT) noexcept
	{
		return DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetSourceSize(
		UINT* a_width,
		UINT* a_height) noexcept
	{
		if (!a_width || !a_height) {
			return E_INVALIDARG;
		}
		*a_width = 0;
		*a_height = 0;
		return DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMaximumFrameLatency(
		UINT) noexcept
	{
		return DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMaximumFrameLatency(
		UINT* a_maxLatency) noexcept
	{
		if (!a_maxLatency) {
			return E_INVALIDARG;
		}
		*a_maxLatency = 0;
		return DXGI_ERROR_INVALID_CALL;
	}

	HANDLE STDMETHODCALLTYPE
		DXGISwapChainProxy::GetFrameLatencyWaitableObject() noexcept
	{
		return nullptr;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMatrixTransform(
		const DXGI_MATRIX_3X2_F*) noexcept
	{
		return DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMatrixTransform(
		DXGI_MATRIX_3X2_F* a_matrix) noexcept
	{
		if (!a_matrix) {
			return E_INVALIDARG;
		}
		*a_matrix = {};
		return DXGI_ERROR_INVALID_CALL;
	}

	UINT STDMETHODCALLTYPE
		DXGISwapChainProxy::GetCurrentBackBufferIndex() noexcept
	{
		return _owner
			? _owner->GetCurrentBackBufferIndex()
			: 0;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::CheckColorSpaceSupport(
		DXGI_COLOR_SPACE_TYPE a_colorSpace,
		UINT* a_support) noexcept
	{
		return _owner
			? _owner->CheckColorSpaceSupport(a_colorSpace, a_support)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetColorSpace1(
		DXGI_COLOR_SPACE_TYPE a_colorSpace) noexcept
	{
		return _owner
			? _owner->SetColorSpace1(a_colorSpace)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers1(
		UINT a_bufferCount,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_flags,
		const UINT* a_creationNodeMask,
		IUnknown* const* a_presentQueue) noexcept
	{
		if (a_creationNodeMask || a_presentQueue) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		return _owner
			? _owner->ResizeBuffers1(
				a_bufferCount,
				a_width,
				a_height,
				a_format,
				a_flags,
				nullptr,
				nullptr)
			: DXGI_ERROR_INVALID_CALL;
	}

	HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetHDRMetaData(
		DXGI_HDR_METADATA_TYPE a_type,
		UINT a_size,
		void* a_metadata) noexcept
	{
		return _owner
			? _owner->SetHDRMetaData(a_type, a_size, a_metadata)
			: DXGI_ERROR_INVALID_CALL;
	}
}
