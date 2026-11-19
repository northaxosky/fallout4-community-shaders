#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include "ReShade/ReShadeRuntime.h"
#include "Render/D3D12FrameGenPresenter.h"
#include "Render/DXGISwapChainProxy.h"
#include "Render/HdrCompose.h"
#include "Render/IPresenter.h"
#include "Render/InteropBridge.h"
#include "Render/PresentationCallbacks.h"
#include "Render/TemporalProvider.h"

namespace cs::render
{
	// The facade the engine sees in every hosted session, and everything on the D3D11
	// side of it: frame texture, HDR targets, ReShade, resize translation and the
	// RenderDoc frame boundary. A presenter puts the frames on screen.
	class PresentationHost : public IDXGISwapChainProxyOwner
	{
	public:
		PresentationHost() = default;
		~PresentationHost();

		// a_provider is the admitted frame-generation provider, or null for D3D11 presentation;
		// if its presenter cannot be created the D3D11 one takes over.
		[[nodiscard]] HRESULT Initialize(InteropBridge& a_bridge,
			ID3D11Device* a_device, ID3D11DeviceContext* a_context,
			const DXGI_SWAP_CHAIN_DESC& a_desc, temporal::IFrameGenerationProvider* a_provider,
			TemporalPresentationCallbacks a_callbacks);
		[[nodiscard]] HRESULT Rollback() noexcept;

		[[nodiscard]] IDXGISwapChain* GetProxy() const noexcept;
		[[nodiscard]] IDXGISwapChain* AcquireProxy() const noexcept;
		[[nodiscard]] bool Owns(IDXGISwapChain* a_swapChain) const noexcept;
		[[nodiscard]] bool IsReady() const noexcept;
		[[nodiscard]] bool IsFrameGenerationReady() const noexcept;
		[[nodiscard]] bool IsHdrContainer() const noexcept;
		// HDR was requested but the chain stayed SDR: compose could not be built.
		[[nodiscard]] bool IsHdrDegraded() const noexcept { return _hdrDegraded; }
		// The provider that owns presentation; null on the D3D11 presenter.
		[[nodiscard]] temporal::IFrameGenerationProvider* GetPresentationProvider() const noexcept;
		// Why the requested provider's presenter was not used, if it was not.
		[[nodiscard]] const std::optional<temporal::ProviderResult>& GetPresenterFallback() const noexcept { return _presenterFallback; }
		[[nodiscard]] UINT GetWidth() const noexcept;
		[[nodiscard]] UINT GetHeight() const noexcept;
		[[nodiscard]] UINT GetFrameSlot() const noexcept;

		[[nodiscard]] SharedD3D11D3D12Texture* GetHudlessTexture() const noexcept;
		[[nodiscard]] SharedD3D11D3D12Texture* GetProxyTexture() const noexcept { return _proxyBuffer.get(); }
		[[nodiscard]] SharedD3D11D3D12Texture* GetDepthTexture() const noexcept;
		[[nodiscard]] SharedD3D11D3D12Texture* GetMotionTexture() const noexcept;
		// Empty unless the HDR container is active.
		[[nodiscard]] hdr::SceneTarget GetHdrSceneTarget() const noexcept;
		// Encodes with the presenter's compose; false unless the HDR container is active.
		[[nodiscard]] bool ComposeToTarget(ID3D11ShaderResourceView& a_scene, ID3D11ShaderResourceView* a_interface,
			ID3D11UnorderedAccessView& a_output, UINT a_width, UINT a_height, const hdr::OutputSettings& a_settings) noexcept;
		[[nodiscard]] temporal::PresentInputRetirementDiagnostics
		GetInputRetirementDiagnostics() const noexcept;

		void SetOutwardD3D11Device(ID3D11Device* a_device) noexcept;
		void FailFrame(const char* a_reason) noexcept;
		void RetryFrameGeneration() noexcept;
		[[nodiscard]] bool AcquireFrameGenerationInputWrite() noexcept;
		// Creates the ReShade runtime on first use; false if absent or unusable.
		[[nodiscard]] bool ReShadeEffectsEnabled() noexcept;
		// A non-zero a_hdrWhiteNits hands ReShade the HDR color as scRGB with that white.
		void RenderReShadeEffects(ID3D11DeviceContext& a_context,
			ID3D11Resource& a_color, ID3D11ShaderResourceView* a_depth, float a_hdrWhiteNits = 0.0f) noexcept;

		HRESULT Present(UINT a_syncInterval, UINT a_flags) noexcept override;
		HRESULT
		Present1(UINT a_syncInterval, UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept override;
		HRESULT GetBuffer(UINT a_buffer, REFIID a_iid,
			void** a_surface) noexcept override;
		HRESULT GetDevice(REFIID a_iid, void** a_device) noexcept override;
		HRESULT SetFullscreenState(BOOL a_fullscreen,
			IDXGIOutput* a_target) noexcept override;
		HRESULT GetFullscreenState(BOOL* a_fullscreen,
			IDXGIOutput** a_target) noexcept override;
		HRESULT GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override;
		HRESULT ResizeBuffers(UINT a_bufferCount, UINT a_width, UINT a_height,
			DXGI_FORMAT a_format, UINT a_flags) noexcept override;
		HRESULT ResizeTarget(const DXGI_MODE_DESC* a_target) noexcept override;
		HRESULT GetDesc1(DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept override;
		HRESULT
		GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept override;
		HRESULT GetHwnd(HWND* a_window) noexcept override;
		UINT GetCurrentBackBufferIndex() noexcept override;
		HRESULT CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE a_colorSpace,
			UINT* a_support) noexcept override;
		HRESULT SetColorSpace1(DXGI_COLOR_SPACE_TYPE a_colorSpace) noexcept override;
		HRESULT ResizeBuffers1(UINT a_bufferCount, UINT a_width, UINT a_height,
			DXGI_FORMAT a_format, UINT a_flags,
			const UINT* a_creationNodeMask,
			IUnknown* const* a_presentQueue) noexcept override;
		HRESULT SetHDRMetaData(DXGI_HDR_METADATA_TYPE a_type, UINT a_size,
			void* a_metadata) noexcept override;

		HRESULT SetPrivateData(REFGUID a_name, UINT a_size,
			const void* a_data) noexcept;
		HRESULT SetPrivateDataInterface(REFGUID a_name,
			const IUnknown* a_unknown) noexcept;
		HRESULT GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data) noexcept;
		HRESULT GetParent(REFIID a_iid, void** a_parent) noexcept;
		HRESULT GetContainingOutput(IDXGIOutput** a_output) noexcept;
		HRESULT GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept;
		HRESULT GetLastPresentCount(UINT* a_count) noexcept;

	private:
		struct Surfaces
		{
			std::unique_ptr<SharedD3D11D3D12Texture> proxy;
			// HDR container only: the composed signal presented in place of the proxy.
			std::unique_ptr<SharedD3D11D3D12Texture> hdrOutput;
			// HDR container only: the scene, while the proxy collects just the interface.
			std::unique_ptr<SharedD3D11D3D12Texture> hdrScene;
			// fp16 scRGB chains only: the signal converted for the chain.
			std::unique_ptr<SharedD3D11D3D12Texture> scRgbOutput;

			[[nodiscard]] bool Complete(bool a_hdr, bool a_scRgb) const noexcept { return proxy && (!a_hdr || (hdrOutput && hdrScene)) && (!a_scRgb || scRgbOutput); }
		};
		[[nodiscard]] HRESULT CreateSurfaces(UINT a_width, UINT a_height, Surfaces& a_surfaces) const;
		void ClearSurfaces() noexcept;
		[[nodiscard]] bool IsScRgbChain() const noexcept { return _hdrContainer && _presenter && _presenter->Desc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT; }
		[[nodiscard]] IDXGISwapChain4* Inner() const noexcept { return _presenter ? _presenter->Chain() : nullptr; }
		[[nodiscard]] std::optional<features::ScRgbProxy> ScRgbFor(
			const SharedD3D11D3D12Texture* a_color, float a_whiteNits, bool a_enabled) const noexcept;
		[[nodiscard]] HRESULT ResizeBuffersImpl(UINT a_bufferCount, UINT a_width, UINT a_height,
			DXGI_FORMAT a_format, UINT a_flags);
		[[nodiscard]] HRESULT PresentInternal(UINT a_syncInterval, UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters, bool a_usePresent1) noexcept;
		[[nodiscard]] PresentFrame BeginFrame() noexcept;
		[[nodiscard]] bool PrepareReShade() noexcept;
		void ReleaseReShade() noexcept;
		void NotifyPresentationBinding() noexcept;

		InteropBridge* _bridge = nullptr;
		winrt::com_ptr<ID3D11Device5> _device11;
		winrt::com_ptr<ID3D11Device> _outwardDevice11;
		winrt::com_ptr<ID3D11DeviceContext4> _context11;
		// The device the D3D11 flip chain was created on, beneath any ReShade proxy.
		winrt::com_ptr<ID3D11Device> _chainDevice11;
		winrt::com_ptr<ID3D12Device> _captureDevice12;
		TemporalPresentationCallbacks _callbacks;
		std::unique_ptr<IPresenter> _presenter;
		// Non-owning: the presenter above, when it is the frame-generation one.
		D3D12FrameGenPresenter* _frameGeneration = nullptr;
		std::optional<temporal::ProviderResult> _presenterFallback;
		std::unique_ptr<SharedD3D11D3D12Texture> _proxyBuffer;
		std::unique_ptr<SharedD3D11D3D12Texture> _hdrOutput;
		std::unique_ptr<SharedD3D11D3D12Texture> _hdrScene;
		std::unique_ptr<SharedD3D11D3D12Texture> _scRgbOutput;
		hdr::Compose _hdrCompose;
		winrt::com_ptr<DXGISwapChainProxy> _proxy;
		features::ReShadeRuntime _reshade;
		DXGI_SWAP_CHAIN_DESC _creationDesc{};
		DXGI_SWAP_CHAIN_DESC _proxyDesc{};
		std::uint32_t _adapterVendorId = 0;
		bool _hdrContainer = false;
		bool _hdrDegraded = false;
		bool _published = false;
		bool _presentationBound = false;
		bool _captureBound = false;
	};
}  // namespace cs::render
