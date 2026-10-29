#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <d3d12.h>
#include <dxgi.h>
#include <winrt/base.h>

#include "Render/LatencyBackend.h"
#include "Render/TemporalProvider.h"

namespace AMD::AntiLag2DX12
{
	struct Context;
}

namespace cs::features
{
	// AMD Radeon Anti-Lag 2 on the native D3D12 device that presents CS's proxy swap chain.
	class AntiLag2Latency final : public render::temporal::ILatencyProvider
	{
	public:
		AntiLag2Latency();
		~AntiLag2Latency() override;

		AntiLag2Latency(const AntiLag2Latency&) = delete;
		AntiLag2Latency& operator=(const AntiLag2Latency&) = delete;

		// No driver call happens until Anti-Lag 2 is requested.
		void AttachDevice(ID3D12Device* a_nativeDevice, std::uint32_t a_adapterVendorId) noexcept;
		// Call only after the presentation swap chain and any frame-generation presenter thread are gone.
		void DetachDevice() noexcept;
		// Once per frame: initializes on the first request, never on a quarantined device, and returns whether it is initialized.
		bool Prepare(bool a_requested, bool a_reflexOwnsLatency, bool a_presentationQuarantined) noexcept;

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

		// After the real frame's D3D12 submission, before the presentation Present.
		void MarkEndOfFrameRendering() noexcept;
		// Lets the FidelityFX frame-generation swap chain tag real and interpolated frames.
		void PublishFrameTypeTagging(IDXGISwapChain* a_swapChain) noexcept;
		[[nodiscard]] render::temporal::AntiLag2Diagnostics
		GetAntiLag2Diagnostics() const noexcept;

	private:
		void ShutdownLocked() noexcept;

		// Sleep holds this across Update, which can block for the driver's delay; callers run on the game thread.
		mutable std::mutex _mutex;
		// Never moved: the frame-generation presenter thread holds its address.
		const std::unique_ptr<AMD::AntiLag2DX12::Context> _context;
		winrt::com_ptr<ID3D12Device> _device;
		std::uint32_t _adapterVendorId = 0;
		IDXGISwapChain* _taggedSwapChain = nullptr;
		bool _taggedEnabled = false;
		bool _presentationQuarantined = false;
		bool _initialized = false;
		bool _initializationFailed = false;
		bool _enabled = false;
		std::uint32_t _lastSleepFrame = UINT32_MAX;
		HRESULT _initializeResult = S_OK;
		std::atomic<HRESULT> _lastUpdateResult{ S_OK };
		std::atomic_uint64_t _updateCalls{ 0 };
		std::atomic_uint64_t _updateFailures{ 0 };
		std::atomic_uint64_t _endOfFrameMarks{ 0 };
	};
}
