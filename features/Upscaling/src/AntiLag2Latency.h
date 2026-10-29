#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <d3d12.h>
#include <dxgi.h>
#include <winrt/base.h>

#include "Render/TemporalProvider.h"

namespace AMD::AntiLag2DX12
{
	struct Context;
}

namespace cs::features
{
	// AMD Radeon Anti-Lag 2 on the native D3D12 device that presents CS's proxy swap chain.
	// No driver call happens until Configure first asks for a mode other than off.
	class AntiLag2Latency final : public render::temporal::ILatencyProvider
	{
	public:
		AntiLag2Latency();
		~AntiLag2Latency() override;

		AntiLag2Latency(const AntiLag2Latency&) = delete;
		AntiLag2Latency& operator=(const AntiLag2Latency&) = delete;

		[[nodiscard]] render::temporal::ProviderResult
		Configure(const render::temporal::LatencyOptions& a_options) override;
		[[nodiscard]] render::temporal::ProviderResult
		Sleep(std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::ProviderResult SetLatencyMarker(
			render::temporal::LatencyMarker a_marker,
			std::uint32_t a_frame) override;
		[[nodiscard]] render::temporal::LatencyDiagnostics
		GetLatencyDiagnostics() const noexcept override;
		[[nodiscard]] bool IsAvailable() const noexcept override;
		void BindPresentation(const render::temporal::PresentationBinding& a_binding) noexcept override;
		void UnbindPresentation() noexcept override;
		void QuarantinePresentation() noexcept override;

	private:
		void InitializeLocked() noexcept;
		void ShutdownLocked() noexcept;
		void MarkEndOfFrameRenderingLocked() noexcept;
		// Lets the FidelityFX FG swap chain tag real vs interpolated frames.
		void PublishFrameTypeTaggingLocked() noexcept;

		// Sleep holds this across Update, which can block for the driver's delay; callers run on the game thread.
		mutable std::mutex _mutex;
		// Never moved: the frame-generation presenter thread holds its address.
		const std::unique_ptr<AMD::AntiLag2DX12::Context> _context;
		winrt::com_ptr<ID3D12Device> _device;
		std::uint32_t _adapterVendorId = 0;
		IDXGISwapChain* _swapChain = nullptr;
		IDXGISwapChain* _taggedSwapChain = nullptr;
		bool _taggedEnabled = false;
		bool _presentationQuarantined = false;
		bool _initialized = false;
		bool _initializationFailed = false;
		bool _enabled = false;
		std::uint32_t _maxFps = 0;
		std::uint32_t _lastSleepFrame = UINT32_MAX;
		HRESULT _initializeResult = S_OK;
		std::atomic<HRESULT> _lastUpdateResult{ S_OK };
		std::atomic_uint64_t _updateCalls{ 0 };
		std::atomic_uint64_t _updateFailures{ 0 };
		std::atomic_uint64_t _markersDelivered{ 0 };
	};
}
