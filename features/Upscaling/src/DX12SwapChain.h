#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include "SuperResolutionContext.h"
#include "Render/FrameGenerationOrchestration.h"
#include "Render/TemporalProvider.h"

namespace cs::features
{
	struct SharedD3D11D3D12Texture
	{
		winrt::com_ptr<ID3D11Texture2D> texture11;
		winrt::com_ptr<ID3D11ShaderResourceView> srv11;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav11;
		winrt::com_ptr<ID3D11RenderTargetView> rtv11;
		winrt::com_ptr<ID3D12Resource> resource12;

		static std::unique_ptr<SharedD3D11D3D12Texture> Create(
			ID3D11Device5* a_device11,
			ID3D12Device* a_device12,
			const D3D11_TEXTURE2D_DESC& a_desc,
			std::string_view a_name);
	};

	class DX12SwapChain;

	struct FrameGenerationFrameState
	{
		std::uint64_t realFrame = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		float jitterX = 0.0f;
		float jitterY = 0.0f;
		float frameTimeMilliseconds = 0.0f;
		bool enable = false;
		bool resetHistory = false;
		ColorMetadata color;
		render::temporal::FrameGenerationCamera camera;
	};

	struct TemporalPresentationCallbacks
	{
		std::function<void()> clearCapture;
		std::function<void(const char*)> recordFailure;
		std::function<FrameGenerationFrameState()> queryFrameState;
	};

	struct FrameGenerationInputRetirementDiagnostics
	{
		std::uint64_t acquisitions = 0;
		std::uint64_t immediateAcquisitions = 0;
		std::uint64_t gpuWaits = 0;
		std::uint64_t providerDrains = 0;
		std::uint64_t globalDrainAttempts = 0;
		std::uint64_t globalDrainFailures = 0;
		std::uint64_t waitFailures = 0;
		std::uint64_t signals = 0;
		std::uint64_t signalFailures = 0;
		std::uint64_t violations = 0;
		std::uint64_t startupDrains = 0;
		std::uint64_t disableDrains = 0;
		std::uint64_t resizeDrains = 0;
		std::uint64_t teardownDrains = 0;
		std::uint64_t steadyDrains = 0;
		std::uint64_t lastRealFrame = 0;
		std::uint64_t lastResourceGeneration = 0;
		std::uint64_t lastRequiredFence = 0;
		std::uint64_t lastCompletedFence = 0;
		std::uint64_t waitCpuMicroseconds = 0;
		std::uint32_t lastSlot = 0;
		bool lastAcquireQueuedGpuWait = false;
	};

	class DXGISwapChainProxy final : public IDXGISwapChain
	{
	public:
		explicit DXGISwapChainProxy(DX12SwapChain& a_owner) noexcept;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_iid, void** a_object) noexcept override;
		ULONG STDMETHODCALLTYPE AddRef() noexcept override;
		ULONG STDMETHODCALLTYPE Release() noexcept override;
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a_name, UINT a_size, const void* a_data) noexcept override;
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a_name, const IUnknown* a_unknown) noexcept override;
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data) noexcept override;
		HRESULT STDMETHODCALLTYPE GetParent(REFIID a_iid, void** a_parent) noexcept override;
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID a_iid, void** a_device) noexcept override;
		HRESULT STDMETHODCALLTYPE Present(UINT a_syncInterval, UINT a_flags) noexcept override;
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface) noexcept override;
		HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL a_fullscreen, IDXGIOutput* a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* a_fullscreen, IDXGIOutput** a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override;
		HRESULT STDMETHODCALLTYPE ResizeBuffers(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept override;
		HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* a_target) noexcept override;
		HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** a_output) noexcept override;
		HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept override;
		HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* a_count) noexcept override;

	private:
		std::atomic<ULONG> _references{ 1 };
		DX12SwapChain& _owner;
	};

	class DX12SwapChain
	{
	public:
		DX12SwapChain() = default;
		~DX12SwapChain();

		HRESULT Initialize(
			IDXGIAdapter* a_adapter,
			ID3D11Device* a_device,
			ID3D11DeviceContext* a_context,
			const DXGI_SWAP_CHAIN_DESC& a_desc,
			render::temporal::IFrameGenerationProvider& a_provider,
			TemporalPresentationCallbacks a_callbacks);
		[[nodiscard]] HRESULT Rollback() noexcept;

		[[nodiscard]] IDXGISwapChain* GetProxy() const noexcept;
		[[nodiscard]] bool Owns(IDXGISwapChain* a_swapChain) const noexcept;
		[[nodiscard]] bool IsReady() const noexcept;
		[[nodiscard]] bool IsBridgeReady() const noexcept;
		[[nodiscard]] bool IsFrameGenerationReady() const noexcept;
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
		[[nodiscard]] FrameGenerationInputRetirementDiagnostics
			GetInputRetirementDiagnostics() const noexcept;
		void SetInputRetirementDetailedTracing(bool a_enabled) noexcept;
		bool EvaluateD3D12SuperResolution(
			render::temporal::ISuperResolutionProvider& a_provider,
			const SuperResolutionExecutionContext& a_context);

		void SetFrameGenerationInputsReady(bool a_ready) noexcept;
		void SetOutwardD3D11Device(ID3D11Device* a_device) noexcept;
		void DisableFrameGeneration(const char* a_reason) noexcept;
		void DisableFrameGeneration(
			std::string_view a_operation,
			const render::temporal::ProviderResult& a_result) noexcept;
		[[nodiscard]] bool AcquireFrameGenerationInputWrite() noexcept;

		HRESULT Present(UINT a_syncInterval, UINT a_flags) noexcept;
		HRESULT GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface) noexcept;
		HRESULT GetDevice(REFIID a_iid, void** a_device) noexcept;
		HRESULT GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept;
		HRESULT ResizeBuffers(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags) noexcept;

		HRESULT SetPrivateData(REFGUID a_name, UINT a_size, const void* a_data) noexcept;
		HRESULT SetPrivateDataInterface(REFGUID a_name, const IUnknown* a_unknown) noexcept;
		HRESULT GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data) noexcept;
		HRESULT GetParent(REFIID a_iid, void** a_parent) noexcept;
		HRESULT GetContainingOutput(IDXGIOutput** a_output) noexcept;
		HRESULT GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept;
		HRESULT GetLastPresentCount(UINT* a_count) noexcept;

	private:
		HRESULT CreateDevices(
			IDXGIAdapter* a_adapter,
			ID3D11Device* a_device,
			ID3D11DeviceContext* a_context);
		HRESULT CreateSwapChain(IDXGIAdapter* a_adapter, const DXGI_SWAP_CHAIN_DESC& a_desc);
		HRESULT CreateInteropFence();
		HRESULT CreateDisplayResources(
			UINT a_width,
			UINT a_height,
			std::unique_ptr<SharedD3D11D3D12Texture>& a_proxy,
			std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2>& a_hudless);
		HRESULT RecreateDisplayResources(UINT a_width, UINT a_height);
		HRESULT RecreateFrameGenerationResources(UINT a_width, UINT a_height);
		HRESULT RestoreFrameGenerationProvider(UINT a_width, UINT a_height);
		HRESULT RecreateSuperResolutionBridge(
			const SuperResolutionExecutionContext& a_context);
		HRESULT RefreshBackBuffers();
		HRESULT WaitForFrame(UINT a_slot) noexcept;
		HRESULT WaitForGpu() noexcept;
		HRESULT PresentImpl(UINT a_syncInterval, UINT a_flags);
		HRESULT ResizeBuffersImpl(
			UINT a_bufferCount,
			UINT a_width,
			UINT a_height,
			DXGI_FORMAT a_format,
			UINT a_flags);
		void ClearSharedBuffers(bool a_clearFrameGenerationInputs = true) noexcept;
		void RearmInputRetirementLogBudget() noexcept;
		void RecordGlobalDrain(
			std::string_view a_reason,
			const render::temporal::ProviderResult& a_result) noexcept;
		void LogInputRetirement(
			render::temporal::PresentInputRetirementLogKind a_kind,
			std::string_view a_operation,
			const render::temporal::PresentInputRetirementToken& a_token,
			std::uint32_t a_slot,
			std::uint64_t a_completedValue,
			bool a_cpuWait,
			bool a_gpuWait,
			std::uint64_t a_waitMicroseconds,
			HRESULT a_result,
			std::string_view a_violationCode = "none",
			bool a_violation = false) noexcept;

		winrt::com_ptr<ID3D11Device5> _device11;
		winrt::com_ptr<ID3D11Device> _outwardDevice11;
		winrt::com_ptr<ID3D11DeviceContext4> _context11;
		winrt::com_ptr<ID3D12Device> _device12;
		winrt::com_ptr<ID3D12CommandQueue> _queue;
		winrt::com_ptr<ID3D12CommandAllocator> _allocators[2];
		winrt::com_ptr<ID3D12GraphicsCommandList> _commandLists[2];
		winrt::com_ptr<ID3D12Resource> _backBuffers[2];
		winrt::com_ptr<ID3D12Fence> _fence12;
		winrt::com_ptr<ID3D11Fence> _fence11;
		winrt::com_ptr<ID3D12Fence> _inputRetirementFence12;
		winrt::com_ptr<ID3D11Fence> _inputRetirementFence11;
		winrt::com_ptr<IDXGISwapChain4> _swapChain;
		std::unique_ptr<SharedD3D11D3D12Texture> _proxyBuffer;
		std::array<std::unique_ptr<SharedD3D11D3D12Texture>, 2> _hudlessBuffers;
		std::unique_ptr<SharedD3D11D3D12Texture> _depthBuffer;
		std::unique_ptr<SharedD3D11D3D12Texture> _motionBuffer;
		std::unique_ptr<SharedD3D11D3D12Texture> _srColorInput;
		std::unique_ptr<SharedD3D11D3D12Texture> _srOutput;
		std::unique_ptr<SharedD3D11D3D12Texture> _srDepth;
		std::unique_ptr<SharedD3D11D3D12Texture> _srMotion;
		std::unique_ptr<SharedD3D11D3D12Texture> _srReactive;
		std::unique_ptr<SharedD3D11D3D12Texture> _srTransparency;
		std::unique_ptr<DXGISwapChainProxy> _proxy;
		render::temporal::IFrameGenerationProvider* _provider = nullptr;
		TemporalPresentationCallbacks _callbacks;
		DXGI_SWAP_CHAIN_DESC _proxyDesc{};
		DXGI_SWAP_CHAIN_DESC1 _innerDesc{};
		UINT _frameIndex = 0;
		UINT _frameSlot = 0;
		UINT64 _nextFenceValue = 1;
		UINT64 _nextInputRetirementValue = 1;
		std::array<std::uint64_t, 2> _allocatorFenceValues{};
		render::temporal::PresentInputReuseGate _inputReuseGate;
		HANDLE _fenceEvent = nullptr;
		bool _frameGenerationInputsReady = false;
		bool _frameGenerationDisabled = false;
		bool _presentPrepared = false;
		bool _preparedFrameGeneration = false;
		bool _vendorConsumptionPossible = false;
		bool _preparedTransaction = false;
		std::uint64_t _preparedRealFrame = 0;
		std::uint64_t _inputResourceGeneration = 0;
		bool _published = false;
		bool _bridgeReady = false;
		std::atomic_uint64_t _retirementAcquisitions{ 0 };
		std::atomic_uint64_t _retirementImmediateAcquisitions{ 0 };
		std::atomic_uint64_t _retirementGpuWaits{ 0 };
		std::atomic_uint64_t _retirementProviderDrains{ 0 };
		std::atomic_uint64_t _retirementGlobalDrainAttempts{ 0 };
		std::atomic_uint64_t _retirementGlobalDrainFailures{ 0 };
		std::atomic_uint64_t _retirementWaitFailures{ 0 };
		std::atomic_uint64_t _retirementSignals{ 0 };
		std::atomic_uint64_t _retirementSignalFailures{ 0 };
		std::atomic_uint64_t _retirementViolations{ 0 };
		std::atomic_uint64_t _retirementStartupDrains{ 0 };
		std::atomic_uint64_t _retirementDisableDrains{ 0 };
		std::atomic_uint64_t _retirementResizeDrains{ 0 };
		std::atomic_uint64_t _retirementTeardownDrains{ 0 };
		std::atomic_uint64_t _retirementSteadyDrains{ 0 };
		std::atomic_uint64_t _retirementLastRealFrame{ 0 };
		std::atomic_uint64_t _retirementLastResourceGeneration{ 0 };
		std::atomic_uint64_t _retirementLastRequiredFence{ 0 };
		std::atomic_uint64_t _retirementLastCompletedFence{ 0 };
		std::atomic_uint64_t _retirementWaitCpuMicroseconds{ 0 };
		std::atomic_uint32_t _retirementLastSlot{ 0 };
		std::atomic_bool _retirementLastAcquireQueuedGpuWait{ false };
		render::temporal::PresentInputRetirementLogBudget
			_retirementLogBudget;
		std::atomic_bool _retirementLogRearmRequested{ false };
		bool _retirementEpochActive = false;
		std::uint64_t _retirementLastResetFrame = UINT64_MAX;
	};
}
