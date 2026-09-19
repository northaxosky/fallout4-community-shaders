#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include "DXGISwapChainProxy.h"
#include "RCAS/RCAS.h"
#include "Render/FrameGenerationOrchestration.h"
#include "Render/TemporalProvider.h"
#include "Utils/CSBuffer.h"

namespace cs::features
{
	class Streamline;

	struct SharedD3D11D3D12Texture
	{
		winrt::com_ptr<ID3D11Texture2D> texture11;
		winrt::com_ptr<ID3D11ShaderResourceView> srv11;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav11;
		winrt::com_ptr<ID3D11RenderTargetView> rtv11;
		winrt::com_ptr<ID3D12Resource> resource12;

		static std::unique_ptr<SharedD3D11D3D12Texture>
		Create(ID3D11Device5* a_device11, ID3D12Device* a_device12,
			const D3D11_TEXTURE2D_DESC& a_desc, std::string_view a_name);
		static std::unique_ptr<cs::buffer::Texture2D>
		CreateTexture(ID3D11Device5* a_device11, ID3D12Device* a_device12,
			const D3D11_TEXTURE2D_DESC& a_desc, std::string_view a_name);
	};

	class DX12SwapChain;

	struct TemporalPresentationCallbacks
	{
		std::function<void()> clearCapture;
		std::function<void(const char*)> recordFailure;
		std::function<render::temporal::FrameGenerationRequest()> queryFrameState;
		std::function<void(ID3D12Device*, HWND)> bindD3D12CaptureTarget;
		std::function<void(ID3D12Device*)> unbindD3D12CaptureTarget;
	};

	class DX12SwapChain : public IDXGISwapChainProxyOwner
	{
	public:
		DX12SwapChain() = default;
		~DX12SwapChain();

		HRESULT Initialize(IDXGIAdapter* a_adapter, ID3D11Device* a_device,
			ID3D11DeviceContext* a_context,
			const DXGI_SWAP_CHAIN_DESC& a_desc,
			Streamline* a_streamline,
			render::temporal::IFrameGenerationProvider* a_provider,
			TemporalPresentationCallbacks a_callbacks);
		[[nodiscard]] HRESULT Rollback() noexcept;
		[[nodiscard]] HRESULT Drain() noexcept;

		[[nodiscard]] IDXGISwapChain* GetProxy() const noexcept;
		[[nodiscard]] IDXGISwapChain* AcquireProxy() const noexcept;
		[[nodiscard]] bool Owns(IDXGISwapChain* a_swapChain) const noexcept;
		[[nodiscard]] bool IsReady() const noexcept;
		[[nodiscard]] bool IsBridgeReady() const noexcept;
		[[nodiscard]] bool IsFrameGenerationReady() const noexcept;
		[[nodiscard]] render::temporal::IFrameGenerationProvider*
		GetPresentationProvider() const noexcept;
		[[nodiscard]] render::temporal::ProviderResult
		CanReplacePresentationProvider(
			render::temporal::IFrameGenerationProvider* a_provider) const noexcept;
		[[nodiscard]] render::temporal::ProviderResult
		ReplacePresentationProvider(
			render::temporal::IFrameGenerationProvider* a_provider);
		[[nodiscard]] UINT GetWidth() const noexcept;
		[[nodiscard]] UINT GetHeight() const noexcept;
		[[nodiscard]] UINT GetFrameSlot() const noexcept;

		[[nodiscard]] SharedD3D11D3D12Texture* GetHudlessTexture() const noexcept;
		[[nodiscard]] SharedD3D11D3D12Texture* GetProxyTexture() const noexcept;
		[[nodiscard]] SharedD3D11D3D12Texture* GetDepthTexture() const noexcept;
		[[nodiscard]] SharedD3D11D3D12Texture* GetMotionTexture() const noexcept;
		[[nodiscard]] ID3D12GraphicsCommandList* GetCommandList() const noexcept;
		[[nodiscard]] ID3D12Device* GetD3D12Device() const noexcept;
		[[nodiscard]] IDXGISwapChain4* GetInnerSwapChain() const noexcept;
		[[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const noexcept;
		[[nodiscard]] render::temporal::PresentInputRetirementDiagnostics
		GetInputRetirementDiagnostics() const noexcept;
		[[nodiscard]] std::unique_ptr<cs::buffer::Texture2D>
		CreateSharedTexture(const D3D11_TEXTURE2D_DESC& a_desc,
			std::string_view a_name) const;
		[[nodiscard]] render::temporal::ProviderResult
		EvaluateD3D12SuperResolution(
			render::temporal::ISuperResolutionProvider& a_provider,
			const render::temporal::SuperResolutionRequest& a_request);

		void SetFrameGenerationInputsReady(bool a_ready) noexcept;
		void SetOutwardD3D11Device(ID3D11Device* a_device) noexcept;
		void DisableFrameGeneration(const char* a_reason) noexcept;
		void DisableFrameGeneration(
			std::string_view a_operation,
			const render::temporal::ProviderResult& a_result) noexcept;
		[[nodiscard]] bool AcquireFrameGenerationInputWrite() noexcept;

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
		HRESULT CreateDevices(IDXGIAdapter* a_adapter, ID3D11Device* a_device,
			ID3D11DeviceContext* a_context);
		HRESULT CreateSwapChain(IDXGIAdapter* a_adapter,
			const DXGI_SWAP_CHAIN_DESC& a_desc,
			bool a_allowProviderFallback = true,
			render::temporal::ProviderResult* a_providerFailure = nullptr);
		[[nodiscard]] render::temporal::ProviderResult
		RetireCurrentPresentationProvider();
		void ReleasePrivatePresentationResources() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
		CreateReplacementPresentation(
			render::temporal::IFrameGenerationProvider* a_provider,
			bool a_allowStreamlineFallback = false);
		HRESULT CreateInteropFence();
		HRESULT CreateDisplayResources(
			UINT a_width, UINT a_height,
			std::unique_ptr<SharedD3D11D3D12Texture>& a_proxy,
			std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2>& a_hudless);
		HRESULT RecreateDisplayResources(UINT a_width, UINT a_height);
		HRESULT RecreateFrameGenerationResources(UINT a_width, UINT a_height);
		HRESULT RestoreFrameGenerationProvider(UINT a_width, UINT a_height);
		HRESULT RefreshBackBuffers();
		struct SubmissionRecord
		{
			winrt::com_ptr<ID3D12CommandAllocator> allocator;
			winrt::com_ptr<ID3D12GraphicsCommandList> commandList;
			winrt::com_ptr<ID3D12DescriptorHeap> postProcessDescriptors;
			std::uint64_t completionValue = 0;
		};
		HRESULT AcquireSubmission(
			std::array<SubmissionRecord, 2>& a_records,
			UINT& a_cursor,
			SubmissionRecord*& a_record) noexcept;
		HRESULT WaitForSubmission(const SubmissionRecord& a_record) noexcept;
		HRESULT WaitForGpu() noexcept;
		HRESULT WaitForInputRetirementGpu() noexcept;
		HRESULT PresentImpl(UINT a_syncInterval, UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters,
			bool a_usePresent1);
		HRESULT PresentInternal(UINT a_syncInterval, UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters,
			bool a_usePresent1) noexcept;
		HRESULT InvokeInnerPresent(UINT a_syncInterval, UINT a_flags,
			const DXGI_PRESENT_PARAMETERS* a_parameters,
			bool a_usePresent1) noexcept;
		HRESULT ResizeBuffersImpl(UINT a_bufferCount, UINT a_width, UINT a_height,
			DXGI_FORMAT a_format, UINT a_flags);
		void ClearSharedBuffers(bool a_clearFrameGenerationInputs = true) noexcept;
		void QuarantineTransport(std::string_view a_reason) noexcept;
		void
		RecordGlobalDrain(std::string_view a_reason,
			const render::temporal::ProviderResult& a_result) noexcept;

		winrt::com_ptr<ID3D11Device5> _device11;
		winrt::com_ptr<ID3D11Device> _outwardDevice11;
		winrt::com_ptr<ID3D11DeviceContext4> _context11;
		winrt::com_ptr<IDXGIAdapter> _adapter;
		winrt::com_ptr<ID3D12DeviceFactory> _deviceFactory;
		winrt::com_ptr<ID3D12Device> _device12;
		winrt::com_ptr<ID3D12Device> _captureDevice12;
		winrt::com_ptr<ID3D12CommandQueue> _queue;
		winrt::com_ptr<ID3D12CommandQueue> _retirementQueue;
		std::array<SubmissionRecord, 2> _srSubmissions;
		std::array<SubmissionRecord, 2> _presentSubmissions;
		winrt::com_ptr<ID3D12Resource> _backBuffers[2];
		winrt::com_ptr<ID3D12Fence> _fence12;
		winrt::com_ptr<ID3D11Fence> _fence11;
		winrt::com_ptr<ID3D12Fence> _presentingCompletionFence;
		winrt::com_ptr<ID3D12Fence> _inputRetirementFence12;
		winrt::com_ptr<ID3D11Fence> _inputRetirementFence11;
		winrt::com_ptr<IDXGISwapChain4> _swapChain;
		std::unique_ptr<SharedD3D11D3D12Texture> _proxyBuffer;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> _hudlessBuffers;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> _depthBuffers;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> _motionBuffers;
		winrt::com_ptr<DXGISwapChainProxy> _proxy;
		Streamline* _streamline = nullptr;
		render::temporal::IFrameGenerationProvider* _provider = nullptr;
		RCAS _rcas;
		TemporalPresentationCallbacks _callbacks;
		DXGI_SWAP_CHAIN_DESC _creationDesc{};
		DXGI_SWAP_CHAIN_DESC _proxyDesc{};
		DXGI_SWAP_CHAIN_DESC1 _innerDesc{};
		UINT _frameIndex = 0;
		UINT _frameSlot = 0;
		UINT _nextSrSubmission = 0;
		UINT _nextPresentSubmission = 0;
		UINT64 _nextFenceValue = 1;
		UINT64 _nextInputRetirementValue = 1;
		std::array<std::uint64_t, 2> _allocatorFenceValues{};
		render::temporal::PresentInputReuseGate _inputReuseGate;
		HANDLE _fenceEvent = nullptr;
		bool _frameGenerationInputsReady = false;
		bool _frameGenerationDisabled = false;
		bool _providerGenerationEnabled = false;
		bool _providerPresentationActive = false;
		bool _presentPrepared = false;
		bool _presentSubmissionMayBeInFlight = false;
		bool _preparedFrameGeneration = false;
		bool _vendorConsumptionPossible = false;
		bool _preparedTransaction = false;
		std::uint64_t _preparedRealFrame = 0;
		std::uint64_t _inputResourceGeneration = 0;
		bool _published = false;
		bool _quarantined = false;
		render::temporal::AtomicPresentInputRetirementDiagnostics
			_retirementDiagnostics;
	};
}  // namespace cs::features
