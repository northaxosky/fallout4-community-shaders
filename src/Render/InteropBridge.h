#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include "RCAS/RCAS.h"
#include "Render/D3D12GpuTimer.h"
#include "Render/FailurePolicy.h"
#include "Render/FrameGenerationOrchestration.h"
#include "Render/TemporalProvider.h"
#include "Utils/CSBuffer.h"

namespace cs::render
{
	// resource12 is null for textures that never leave the D3D11 device.
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

	struct SubmissionRecord
	{
		winrt::com_ptr<ID3D12CommandAllocator> allocator;
		winrt::com_ptr<ID3D12GraphicsCommandList> commandList;
		winrt::com_ptr<ID3D12DescriptorHeap> postProcessDescriptors;
		std::uint64_t completionValue = 0;
		D3D12GpuTimer::Record timing;
	};
	using SubmissionRing = std::array<SubmissionRecord, 2>;

	// The vendor runtime that wraps the bridge's D3D12 device.
	class IInteropRuntime
	{
	public:
		virtual ~IInteropRuntime() = default;

		[[nodiscard]] virtual std::filesystem::path D3D12SdkDirectory() const = 0;
		// Swaps in the runtime's wrapper; on failure the device stays raw and the runtime is off.
		virtual void UpgradeDevice(winrt::com_ptr<ID3D12Device>& a_device, IDXGIAdapter& a_adapter) = 0;
		// The device beneath the wrapper, for native tooling.
		[[nodiscard]] virtual bool NativeDevice(ID3D12Device* a_device, ID3D12Device** a_native) const = 0;
		// Chains the bridge creates never carry the runtime's presentation hooks.
		[[nodiscard]] virtual temporal::ProviderResult DisablePresentationHooks() = 0;
		[[nodiscard]] virtual bool SharpensSuperResolution() const noexcept = 0;
		// A real host Present with no runtime chain behind it, so the runtime's own frame bookkeeping runs.
		virtual void OnHostFramePresented(std::uint32_t a_presentFlags) noexcept = 0;
		// Before the host reallocates textures shared with the runtime.
		virtual void OnHostResize() noexcept = 0;
	};

	// The private D3D12 device shared with the engine's D3D11 device: super resolution
	// runs here in every session, and the frame-generation presenter borrows its queue.
	class InteropBridge
	{
	public:
		InteropBridge() = default;
		~InteropBridge();
		InteropBridge(const InteropBridge&) = delete;
		InteropBridge& operator=(const InteropBridge&) = delete;

		// a_onQuarantine runs once when the bridge stops trusting its GPU state.
		[[nodiscard]] HRESULT Initialize(IDXGIAdapter* a_adapter, ID3D11Device* a_device,
			ID3D11DeviceContext* a_context, IInteropRuntime& a_runtime,
			std::function<void()> a_onQuarantine);
		[[nodiscard]] HRESULT Rollback() noexcept;
		[[nodiscard]] HRESULT Drain() noexcept;
		// For sessions where the runtime's own chain does not present.
		void NotifyHostFramePresented(std::uint32_t a_presentFlags) noexcept;
		void NotifyHostResize() noexcept;

		[[nodiscard]] bool IsReady() const noexcept;
		[[nodiscard]] bool IsQuarantined() const noexcept { return _quarantined; }
		// The renderer reports each world frame's super-resolution outcome here.
		[[nodiscard]] temporal::FailurePolicy& SuperResolutionPolicy() noexcept { return _superResolution; }
		// A quarantined GPU may still read these; they live until the process ends.
		static void Retain(IUnknown* a_object);
		static void Retain(const std::unique_ptr<SharedD3D11D3D12Texture>& a_texture);
		void Quarantine(std::string_view a_reason) noexcept;
		void MarkQuarantined() noexcept;

		[[nodiscard]] ID3D11Device5* GetD3D11Device() const noexcept { return _device11.get(); }
		[[nodiscard]] ID3D11DeviceContext4* GetD3D11Context() const noexcept { return _context11.get(); }
		[[nodiscard]] IDXGIAdapter* GetAdapter() const noexcept { return _adapter.get(); }
		[[nodiscard]] ID3D12Device* GetD3D12Device() const noexcept { return _device12.get(); }
		// The device beneath the vendor wrapper; null if it could not be unwrapped.
		[[nodiscard]] ID3D12Device* GetCaptureDevice() const noexcept { return _captureDevice12.get(); }
		[[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const noexcept { return _queue.get(); }
		[[nodiscard]] ID3D12CommandQueue* GetRetirementQueue() const noexcept { return _retirementQueue.get(); }
		[[nodiscard]] ID3D12Fence* GetRetirementFence12() const noexcept { return _inputRetirementFence12.get(); }
		[[nodiscard]] ID3D11Fence* GetRetirementFence11() const noexcept { return _inputRetirementFence11.get(); }
		[[nodiscard]] ID3D12Fence* GetPresentingCompletionFence() const noexcept { return _presentingCompletionFence.get(); }
		[[nodiscard]] std::uint64_t NextRetirementValue() noexcept { return _nextInputRetirementValue++; }
		[[nodiscard]] D3D12GpuTimer& GetGpuTimer() noexcept { return _gpuTimer; }

		// Command lists for the presenter's own submissions share the bridge's timer and fence.
		void CreateSubmissions(SubmissionRing& a_ring, std::string_view a_phase);
		[[nodiscard]] HRESULT AcquireSubmission(
			SubmissionRing& a_ring, UINT& a_cursor, SubmissionRecord*& a_record) noexcept;

		// D3D11 work recorded so far finishes before the next queue submission.
		void QueueWaitsForD3D11();
		// Signals the queue after a submission; the value retires that submission.
		[[nodiscard]] std::uint64_t SignalQueue();
		[[nodiscard]] HRESULT D3D11WaitsForQueue(std::uint64_t a_value) noexcept;

		[[nodiscard]] std::unique_ptr<cs::buffer::Texture2D>
		CreateSharedTexture(const D3D11_TEXTURE2D_DESC& a_desc,
			std::string_view a_name) const;
		[[nodiscard]] temporal::ProviderResult EvaluateD3D12SuperResolution(
			temporal::ISuperResolutionProvider& a_provider,
			const temporal::SuperResolutionRequest& a_request);

	private:
		void CreateDevices(IDXGIAdapter* a_adapter, ID3D11Device* a_device,
			ID3D11DeviceContext* a_context);
		void CreateFences();
		[[nodiscard]] HRESULT WaitForSubmission(const SubmissionRecord& a_record) noexcept;
		[[nodiscard]] HRESULT WaitForGpu() noexcept;
		[[nodiscard]] HRESULT WaitForInputRetirementGpu() noexcept;

		winrt::com_ptr<ID3D11Device5> _device11;
		winrt::com_ptr<ID3D11DeviceContext4> _context11;
		winrt::com_ptr<IDXGIAdapter> _adapter;
		winrt::com_ptr<ID3D12DeviceFactory> _deviceFactory;
		winrt::com_ptr<ID3D12Device> _device12;
		winrt::com_ptr<ID3D12Device> _captureDevice12;
		winrt::com_ptr<ID3D12CommandQueue> _queue;
		winrt::com_ptr<ID3D12CommandQueue> _retirementQueue;
		D3D12GpuTimer _gpuTimer;
		SubmissionRing _srSubmissions;
		winrt::com_ptr<ID3D12Fence> _fence12;
		winrt::com_ptr<ID3D11Fence> _fence11;
		winrt::com_ptr<ID3D12Fence> _presentingCompletionFence;
		winrt::com_ptr<ID3D12Fence> _inputRetirementFence12;
		winrt::com_ptr<ID3D11Fence> _inputRetirementFence11;
		features::RCAS _rcas;
		IInteropRuntime* _runtime = nullptr;
		std::function<void()> _onQuarantine;
		temporal::FailurePolicy _superResolution{ temporal::kSuperResolutionFailureLimit };
		HANDLE _fenceEvent = nullptr;
		std::uint32_t _nextSrSubmission = 0;
		std::uint64_t _nextFenceValue = 1;
		std::uint64_t _nextInputRetirementValue = 1;
		bool _quarantined = false;
	};
}  // namespace cs::render
