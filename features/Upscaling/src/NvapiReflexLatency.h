#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

#include <d3d11.h>
#include <winrt/base.h>

#include "Render/TemporalProvider.h"

namespace cs::features
{
	// NVIDIA Reflex through NVAPI on the D3D11 flip chain's device.
	class NvapiReflexLatency final : public render::temporal::ILatencyProvider
	{
	public:
		void BindPresentation(const render::temporal::PresentationBinding& a_binding) noexcept override;
		void UnbindPresentation() noexcept override;
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

	private:
		void ShutdownLocked() noexcept;
		void InitializeLocked() noexcept;
		[[nodiscard]] winrt::com_ptr<ID3D11Device> Device() const noexcept;
		void PollReport(ID3D11Device& a_device) noexcept;

		// Sleep runs outside this lock on a copy of the device.
		mutable std::mutex _mutex;
		winrt::com_ptr<ID3D11Device> _device;
		std::uint32_t _adapterVendorId = 0;
		bool _initialized = false;
		bool _initializationFailed = false;
		std::int32_t _initializeStatus = 0;
		std::optional<render::temporal::LatencyOptions> _applied;

		std::atomic_uint64_t _sleepCalls{ 0 };
		std::atomic_uint64_t _sleepFailures{ 0 };
		std::atomic_uint64_t _markerFailures{ 0 };
		std::atomic_bool _reportAvailable{ false };
		std::atomic_uint64_t _reportFrame{ 0 };
		std::atomic_uint64_t _simulationToPresentUs{ 0 };
		std::atomic_uint32_t _gpuFrameTimeUs{ 0 };
		std::atomic<std::int32_t> _lastStatus{ 0 };
	};
}
