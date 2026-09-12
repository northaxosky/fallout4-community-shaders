#pragma once

#include "Render/TemporalProvider.h"

#include <xefg_swapchain.h>
#include <xefg_swapchain_d3d12.h>
#include <xell.h>
#include <xell_d3d12.h>

namespace cs::features
{
	class FidelityFX;
	class Streamline;

	class FidelityFXPresentation final :
		public render::temporal::IFrameGenerationProvider
	{
	public:
		explicit FidelityFXPresentation(FidelityFX& a_runtime) noexcept;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult PrepareDevice(
			ID3D12Device** a_device) override;
		[[nodiscard]] render::temporal::ProviderResult PrepareFactory(
			IDXGIFactory4** a_factory) override;
		[[nodiscard]] render::temporal::ProviderResult CreatePresentation(
			const render::temporal::PresentationCreateContext& a_context,
			IDXGISwapChain4** a_swapChain) override;
		[[nodiscard]] render::temporal::ProviderResult CreateDisplayResources(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format,
			std::uint32_t a_bufferCount) override;
		[[nodiscard]] render::temporal::ProviderResult PrepareFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult CancelFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult SetGenerationEnabled(
			bool a_enabled) override;
		[[nodiscard]] render::temporal::ProviderResult AcquirePresentInputs() override;
		[[nodiscard]] render::temporal::ProviderResult CollectPresentStatus(
			UINT a_presentFlags,
			HRESULT a_presentResult) override;
		[[nodiscard]] render::temporal::ProviderResult Sleep(
			std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult SetLatencyMarker(
			render::temporal::LatencyMarker a_marker,
			std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult Quiesce() override;
		[[nodiscard]] render::temporal::ProviderResult ReleaseDisplayResources() noexcept override;
		[[nodiscard]] render::temporal::ProviderResult DestroyAfterDrain() noexcept override;
		[[nodiscard]] bool IsReady() const noexcept override;

	private:
		FidelityFX& _runtime;
		ID3D12Device* _device = nullptr;
		IDXGISwapChain4* _swapChain = nullptr;
		bool _enabled = false;
	};

	class StreamlinePresentation final :
		public render::temporal::IFrameGenerationProvider
	{
	public:
		explicit StreamlinePresentation(Streamline& a_runtime) noexcept;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult PrepareDevice(
			ID3D12Device** a_device) override;
		[[nodiscard]] render::temporal::ProviderResult PrepareFactory(
			IDXGIFactory4** a_factory) override;
		[[nodiscard]] render::temporal::ProviderResult CreatePresentation(
			const render::temporal::PresentationCreateContext& a_context,
			IDXGISwapChain4** a_swapChain) override;
		[[nodiscard]] render::temporal::ProviderResult CreateDisplayResources(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format,
			std::uint32_t a_bufferCount) override;
		[[nodiscard]] render::temporal::ProviderResult PrepareFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult CancelFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult SetGenerationEnabled(
			bool a_enabled) override;
		[[nodiscard]] render::temporal::ProviderResult AcquirePresentInputs() override;
		[[nodiscard]] render::temporal::ProviderResult CollectPresentStatus(
			UINT a_presentFlags,
			HRESULT a_presentResult) override;
		[[nodiscard]] std::optional<std::uint32_t> ConsumeGeneratedFrameCount() noexcept override;
		[[nodiscard]] std::optional<std::uint32_t> ConsumePresentedFrameCount() noexcept override;
		[[nodiscard]] render::temporal::ProviderResult Sleep(
			std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult SetLatencyMarker(
			render::temporal::LatencyMarker a_marker,
			std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult Quiesce() override;
		[[nodiscard]] render::temporal::ProviderResult ReleaseDisplayResources() noexcept override;
		[[nodiscard]] render::temporal::ProviderResult DestroyAfterDrain() noexcept override;
		[[nodiscard]] bool IsReady() const noexcept override;

	private:
		Streamline& _runtime;
		ID3D12CommandQueue* _queue = nullptr;
		std::uint32_t _width = 0;
		std::uint32_t _height = 0;
		std::uint32_t _bufferCount = 0;
		bool _enabled = false;
		bool _ready = false;
	};

	class XeSSPresentation final :
		public render::temporal::IFrameGenerationProvider
	{
	public:
		~XeSSPresentation() override;

		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] render::temporal::ProviderResult PrepareDevice(
			ID3D12Device** a_device) override;
		[[nodiscard]] render::temporal::ProviderResult PrepareFactory(
			IDXGIFactory4** a_factory) override;
		[[nodiscard]] render::temporal::ProviderResult CreatePresentation(
			const render::temporal::PresentationCreateContext& a_context,
			IDXGISwapChain4** a_swapChain) override;
		[[nodiscard]] render::temporal::ProviderResult CreateDisplayResources(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format,
			std::uint32_t a_bufferCount) override;
		[[nodiscard]] render::temporal::ProviderResult PrepareFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult CancelFrame(
			const render::temporal::FrameGenerationRequest& a_request) override;
		[[nodiscard]] render::temporal::ProviderResult SetGenerationEnabled(
			bool a_enabled) override;
		[[nodiscard]] render::temporal::ProviderResult AcquirePresentInputs() override;
		[[nodiscard]] render::temporal::ProviderResult CollectPresentStatus(
			UINT a_presentFlags,
			HRESULT a_presentResult) override;
		[[nodiscard]] std::optional<std::uint32_t> ConsumeGeneratedFrameCount() noexcept override;
		[[nodiscard]] std::optional<std::uint32_t> ConsumePresentedFrameCount() noexcept override;
		[[nodiscard]] render::temporal::ProviderResult Sleep(
			std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult SetLatencyMarker(
			render::temporal::LatencyMarker a_marker,
			std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult Quiesce() override;
		[[nodiscard]] render::temporal::ProviderResult ReleaseDisplayResources() noexcept override;
		[[nodiscard]] render::temporal::ProviderResult DestroyAfterDrain() noexcept override;
		[[nodiscard]] bool IsReady() const noexcept override;

	private:
		template <class T>
		[[nodiscard]] static T Load(HMODULE a_module, const char* a_name)
		{
			return reinterpret_cast<T>(GetProcAddress(a_module, a_name));
		}
		[[nodiscard]] bool LoadRuntimes();

		HMODULE _frameGenerationModule = nullptr;
		HMODULE _latencyModule = nullptr;
		xefg_swapchain_handle_t _context = nullptr;
		xell_context_handle_t _latency = nullptr;
		ID3D12CommandQueue* _queue = nullptr;
		bool _ready = false;
		bool _enabled = false;
		std::uint32_t _generatedFrames = 0;
		std::uint32_t _presentedFrames = 0;
		bool _generatedCountAvailable = false;
		bool _presentedCountAvailable = false;

		decltype(&xefgSwapChainD3D12CreateContext) _createContext = nullptr;
		decltype(&xefgSwapChainD3D12InitFromSwapChainDesc) _initialize = nullptr;
		decltype(&xefgSwapChainD3D12GetSwapChainPtr) _getSwapChain = nullptr;
		decltype(&xefgSwapChainD3D12TagFrameResource) _tagResource = nullptr;
		decltype(&xefgSwapChainTagFrameConstants) _tagConstants = nullptr;
		decltype(&xefgSwapChainSetPresentId) _setPresentId = nullptr;
		decltype(&xefgSwapChainSetEnabled) _setEnabled = nullptr;
		decltype(&xefgSwapChainGetLastPresentStatus) _getPresentStatus = nullptr;
		decltype(&xefgSwapChainSetLatencyReduction) _setLatencyReduction = nullptr;
		decltype(&xefgSwapChainDestroy) _destroyContext = nullptr;
		decltype(&xefgSwapChainSetLoggingCallback) _setLoggingCallback = nullptr;
		decltype(&xellD3D12CreateContext) _createLatency = nullptr;
		decltype(&xellSetSleepMode) _setSleepMode = nullptr;
		decltype(&xellSleep) _sleep = nullptr;
		decltype(&xellAddMarkerData) _addMarker = nullptr;
		decltype(&xellDestroyContext) _destroyLatency = nullptr;
		decltype(&xellSetLoggingCallback) _setLatencyLoggingCallback = nullptr;
	};
}
