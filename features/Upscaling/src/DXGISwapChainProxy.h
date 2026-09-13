#pragma once

#include <atomic>

#include <dxgi1_6.h>
#include <winrt/base.h>

namespace cs::features
{
	class IDXGISwapChainProxyOwner
	{
	public:
		virtual ~IDXGISwapChainProxyOwner() = default;

		virtual HRESULT Present(
			UINT a_syncInterval, UINT a_flags) noexcept = 0;
		virtual HRESULT Present1(
			UINT a_syncInterval,
			UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept = 0;
		virtual HRESULT GetBuffer(
			UINT a_buffer, REFIID a_iid, void** a_surface) noexcept = 0;
		virtual HRESULT GetDevice(
			REFIID a_iid, void** a_device) noexcept = 0;
		virtual HRESULT SetFullscreenState(
			BOOL a_fullscreen, IDXGIOutput* a_target) noexcept = 0;
		virtual HRESULT GetFullscreenState(
			BOOL* a_fullscreen, IDXGIOutput** a_target) noexcept = 0;
		virtual HRESULT GetDesc(
			DXGI_SWAP_CHAIN_DESC* a_desc) noexcept = 0;
		virtual HRESULT ResizeBuffers(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept = 0;
		virtual HRESULT ResizeTarget(
			const DXGI_MODE_DESC* a_target) noexcept = 0;
		virtual HRESULT GetDesc1(
			DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept = 0;
		virtual HRESULT GetFullscreenDesc(
			DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept = 0;
		virtual HRESULT GetHwnd(HWND* a_window) noexcept = 0;
		virtual UINT GetCurrentBackBufferIndex() noexcept = 0;
		virtual HRESULT CheckColorSpaceSupport(
			DXGI_COLOR_SPACE_TYPE a_colorSpace,
			UINT* a_support) noexcept = 0;
		virtual HRESULT SetColorSpace1(
			DXGI_COLOR_SPACE_TYPE a_colorSpace) noexcept = 0;
		virtual HRESULT ResizeBuffers1(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags,
			const UINT* a_creationNodeMask,
			IUnknown* const* a_presentQueue) noexcept = 0;
		virtual HRESULT SetHDRMetaData(
			DXGI_HDR_METADATA_TYPE a_type,
			UINT a_size,
			void* a_metadata) noexcept = 0;
	};

	class DXGISwapChainProxy final : public IDXGISwapChain4
	{
	public:
		DXGISwapChainProxy(
			IDXGISwapChainProxyOwner& a_owner,
			IDXGISwapChain4& a_inner) noexcept;
		void DetachOwner() noexcept;

		HRESULT STDMETHODCALLTYPE QueryInterface(
			REFIID a_iid, void** a_object) noexcept override;
		ULONG STDMETHODCALLTYPE AddRef() noexcept override;
		ULONG STDMETHODCALLTYPE Release() noexcept override;
		HRESULT STDMETHODCALLTYPE SetPrivateData(
			REFGUID a_name,
			UINT a_size,
			const void* a_data) noexcept override;
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
			REFGUID a_name,
			const IUnknown* a_unknown) noexcept override;
		HRESULT STDMETHODCALLTYPE GetPrivateData(
			REFGUID a_name,
			UINT* a_size,
			void* a_data) noexcept override;
		HRESULT STDMETHODCALLTYPE GetParent(
			REFIID a_iid, void** a_parent) noexcept override;
		HRESULT STDMETHODCALLTYPE GetDevice(
			REFIID a_iid, void** a_device) noexcept override;
		HRESULT STDMETHODCALLTYPE Present(
			UINT a_syncInterval, UINT a_flags) noexcept override;
		HRESULT STDMETHODCALLTYPE GetBuffer(
			UINT a_buffer,
			REFIID a_iid,
			void** a_surface) noexcept override;
		HRESULT STDMETHODCALLTYPE SetFullscreenState(
			BOOL a_fullscreen,
			IDXGIOutput* a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetFullscreenState(
			BOOL* a_fullscreen,
			IDXGIOutput** a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetDesc(
			DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override;
		HRESULT STDMETHODCALLTYPE ResizeBuffers(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept override;
		HRESULT STDMETHODCALLTYPE ResizeTarget(
			const DXGI_MODE_DESC* a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetContainingOutput(
			IDXGIOutput** a_output) noexcept override;
		HRESULT STDMETHODCALLTYPE GetFrameStatistics(
			DXGI_FRAME_STATISTICS* a_stats) noexcept override;
		HRESULT STDMETHODCALLTYPE GetLastPresentCount(
			UINT* a_count) noexcept override;

		HRESULT STDMETHODCALLTYPE GetDesc1(
			DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept override;
		HRESULT STDMETHODCALLTYPE GetFullscreenDesc(
			DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept override;
		HRESULT STDMETHODCALLTYPE GetHwnd(HWND* a_window) noexcept override;
		HRESULT STDMETHODCALLTYPE GetCoreWindow(
			REFIID a_iid, void** a_window) noexcept override;
		HRESULT STDMETHODCALLTYPE Present1(
			UINT a_syncInterval,
			UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept override;
		BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() noexcept override;
		HRESULT STDMETHODCALLTYPE GetRestrictToOutput(
			IDXGIOutput** a_output) noexcept override;
		HRESULT STDMETHODCALLTYPE SetBackgroundColor(
			const DXGI_RGBA* a_color) noexcept override;
		HRESULT STDMETHODCALLTYPE GetBackgroundColor(
			DXGI_RGBA* a_color) noexcept override;
		HRESULT STDMETHODCALLTYPE SetRotation(
			DXGI_MODE_ROTATION a_rotation) noexcept override;
		HRESULT STDMETHODCALLTYPE GetRotation(
			DXGI_MODE_ROTATION* a_rotation) noexcept override;

		HRESULT STDMETHODCALLTYPE SetSourceSize(
			UINT a_width, UINT a_height) noexcept override;
		HRESULT STDMETHODCALLTYPE GetSourceSize(
			UINT* a_width, UINT* a_height) noexcept override;
		HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(
			UINT a_maxLatency) noexcept override;
		HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(
			UINT* a_maxLatency) noexcept override;
		HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject()
			noexcept override;
		HRESULT STDMETHODCALLTYPE SetMatrixTransform(
			const DXGI_MATRIX_3X2_F* a_matrix) noexcept override;
		HRESULT STDMETHODCALLTYPE GetMatrixTransform(
			DXGI_MATRIX_3X2_F* a_matrix) noexcept override;

		UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() noexcept override;
		HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(
			DXGI_COLOR_SPACE_TYPE a_colorSpace,
			UINT* a_support) noexcept override;
		HRESULT STDMETHODCALLTYPE SetColorSpace1(
			DXGI_COLOR_SPACE_TYPE a_colorSpace) noexcept override;
		HRESULT STDMETHODCALLTYPE ResizeBuffers1(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags,
			const UINT* a_creationNodeMask,
			IUnknown* const* a_presentQueue) noexcept override;

		HRESULT STDMETHODCALLTYPE SetHDRMetaData(
			DXGI_HDR_METADATA_TYPE a_type,
			UINT a_size,
			void* a_metadata) noexcept override;

	private:
		std::atomic<ULONG> _references{ 1 };
		IDXGISwapChainProxyOwner* _owner;
		winrt::com_ptr<IDXGISwapChain4> _inner;
	};
}
