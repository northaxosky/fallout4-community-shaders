#pragma once

#include <cstdint>
#include <string_view>

namespace cs::render::temporal
{
	inline constexpr std::uint32_t kAmdVendorId = 0x1002;

	enum class LatencyBackend : std::uint8_t
	{
		kNone,
		kReflex,
		kAntiLag2
	};

	[[nodiscard]] constexpr std::string_view Name(LatencyBackend a_backend) noexcept
	{
		switch (a_backend) {
		case LatencyBackend::kReflex:
			return "NVIDIA Reflex";
		case LatencyBackend::kAntiLag2:
			return "AMD Radeon Anti-Lag 2";
		case LatencyBackend::kNone:
			break;
		}
		return "none";
	}

	// Streamline reports its Reflex plugin as available on any GPU; only the driver's low-latency report makes it usable.
	[[nodiscard]] constexpr bool ReflexOwnsLatency(
		bool a_reflexFeatureAvailable,
		bool a_reflexLowLatencyAvailable) noexcept
	{
		return a_reflexFeatureAvailable && a_reflexLowLatencyAvailable;
	}

	// Anti-Lag 2 stays selected once initialized so that a disable still reaches the driver, until presentation is quarantined.
	[[nodiscard]] constexpr LatencyBackend SelectLatencyBackend(
		bool a_reflexOwnsLatency,
		bool a_antiLag2Initialized,
		bool a_presentationQuarantined) noexcept
	{
		if (a_reflexOwnsLatency) {
			return LatencyBackend::kReflex;
		}
		return a_antiLag2Initialized && !a_presentationQuarantined ? LatencyBackend::kAntiLag2 : LatencyBackend::kNone;
	}

	[[nodiscard]] constexpr bool ShouldInitializeAntiLag2(
		bool a_requested,
		bool a_reflexOwnsLatency,
		std::uint32_t a_adapterVendorId,
		bool a_deviceAttached,
		bool a_presentationQuarantined,
		bool a_initialized,
		bool a_initializationFailed) noexcept
	{
		return a_requested && !a_reflexOwnsLatency && a_adapterVendorId == kAmdVendorId &&
		       a_deviceAttached && !a_presentationQuarantined && !a_initialized && !a_initializationFailed;
	}

	struct AntiLag2Diagnostics
	{
		bool deviceAttached = false;
		bool presentationQuarantined = false;
		bool initialized = false;
		bool initializationFailed = false;
		bool enabled = false;
		bool frameTypeTagging = false;
		std::int32_t initializeResult = 0;
		std::int32_t lastUpdateResult = 0;
		std::uint64_t updateCalls = 0;
		std::uint64_t updateFailures = 0;
		std::uint64_t endOfFrameMarks = 0;
	};

	enum class AntiLag2Status : std::uint8_t
	{
		kReady,
		kReflexOwnsLatency,
		kUnavailable,
		kNeedsD3D12Presentation,
		kPresentationFailed,
		kUpdateFailing
	};

	// Update's HRESULT comes from the driver's delay call; a failure is assumed to insert no delay.
	[[nodiscard]] constexpr AntiLag2Status GetAntiLag2Status(
		bool a_reflexOwnsLatency,
		const AntiLag2Diagnostics& a_diagnostics) noexcept
	{
		if (a_reflexOwnsLatency) {
			return AntiLag2Status::kReflexOwnsLatency;
		}
		if (a_diagnostics.presentationQuarantined) {
			return AntiLag2Status::kPresentationFailed;
		}
		if (a_diagnostics.initializationFailed) {
			return AntiLag2Status::kUnavailable;
		}
		if (!a_diagnostics.deviceAttached) {
			return AntiLag2Status::kNeedsD3D12Presentation;
		}
		if (a_diagnostics.enabled && a_diagnostics.lastUpdateResult < 0) {
			return AntiLag2Status::kUpdateFailing;
		}
		return AntiLag2Status::kReady;
	}
}
