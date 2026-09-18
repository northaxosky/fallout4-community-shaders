#pragma once

#include "Render/TemporalPipeline.h"

#include <format>
#include <string>
#include <string_view>

namespace cs::render::temporal::presentation
{
	enum class AvailabilityKind : std::uint8_t
	{
		kAvailable,
		kChecking,
		kUnavailable
	};

	struct Availability
	{
		AvailabilityKind kind = AvailabilityKind::kAvailable;
		std::string reason;

		[[nodiscard]] bool Selectable() const noexcept
		{
			return kind == AvailabilityKind::kAvailable;
		}
	};

	[[nodiscard]] constexpr std::string_view Name(
		SuperResolutionMethod a_method) noexcept
	{
		switch (a_method) {
		case SuperResolutionMethod::kNone:
			return "Off";
		case SuperResolutionMethod::kTAA:
			return "TAA";
		case SuperResolutionMethod::kFSR3:
			return "FSR 3";
		case SuperResolutionMethod::kDLSS:
			return "DLSS";
		case SuperResolutionMethod::kFSR4:
			return "FSR 4";
		case SuperResolutionMethod::kCount:
			break;
		}
		return "Unknown";
	}

	[[nodiscard]] constexpr std::string_view Name(
		FrameGenerationMethod a_method) noexcept
	{
		switch (a_method) {
		case FrameGenerationMethod::kOff:
			return "Off";
		case FrameGenerationMethod::kFSR3:
			return "FSR 3";
		case FrameGenerationMethod::kDLSSG:
			return "DLSS";
		case FrameGenerationMethod::kFSR4:
			return "FSR 4";
		case FrameGenerationMethod::kCount:
			break;
		}
		return "Unknown";
	}

	[[nodiscard]] inline std::string FidelityFXReason(
		std::uint32_t a_reason)
	{
		switch (a_reason) {
		case 1:
			return "The provider module is unavailable.";
		case 2:
			return "The operating system is unsupported.";
		case 3:
			return "The D3D12 runtime or shader model is unsupported.";
		case 4:
			return "The provider rejected the current hardware.";
		case 5:
			return "The required provider version is unavailable.";
		default:
			return "The provider reported that this method is unavailable.";
		}
	}

	[[nodiscard]] inline Availability Describe(
		SuperResolutionMethod a_method,
		const TemporalPipelineStatus& a_status,
		const FidelityFXCapabilities& a_fidelityFx)
	{
		if (a_method == SuperResolutionMethod::kNone ||
			a_method == SuperResolutionMethod::kTAA) {
			return {};
		}
		if (!a_status.requestFrozen || !a_status.d3d11Ready ||
			!a_status.session.valid) {
			return {
				.kind = AvailabilityKind::kChecking,
				.reason = "Checking availability"
			};
		}
		const auto index = static_cast<std::size_t>(a_method);
		if (index < a_status.session.admittedSr.size() &&
			a_status.session.admittedSr[index]) {
			return {};
		}
		if (a_method == SuperResolutionMethod::kFSR4) {
			const auto& capability = a_fidelityFx.fsr4SuperResolution;
			if (capability.availability == CapabilityAvailability::kUnknown) {
				return {
					.kind = AvailabilityKind::kUnavailable,
					.reason = "FSR 4 was not admitted for this session."
				};
			}
			if (!capability.IsAvailable()) {
				return {
					.kind = AvailabilityKind::kUnavailable,
					.reason = FidelityFXReason(capability.unavailableReason)
				};
			}
		}
		return {
			.kind = AvailabilityKind::kUnavailable,
			.reason = std::format(
				"{} was not admitted for this session.",
				Name(a_method))
		};
	}

	[[nodiscard]] inline Availability Describe(
		FrameGenerationMethod a_method,
		const TemporalPipelineStatus& a_status,
		const FrameGenerationCapabilities& a_dlss,
		const FidelityFXCapabilities& a_fidelityFx)
	{
		if (a_method == FrameGenerationMethod::kOff) {
			return {};
		}
		if (!a_status.requestFrozen || !a_status.d3d11Ready ||
			!a_status.session.valid) {
			return {
				.kind = AvailabilityKind::kChecking,
				.reason = "Checking availability"
			};
		}
		const auto index = static_cast<std::size_t>(a_method);
		if (index < a_status.session.admittedFg.size() &&
			a_status.session.admittedFg[index]) {
			return {};
		}
		if (a_method == FrameGenerationMethod::kDLSSG) {
			if (a_dlss.configurationQueryFailed) {
				return {
					.kind = AvailabilityKind::kUnavailable,
					.reason = "The runtime capability check failed."
				};
			}
			if (a_dlss.availability == CapabilityAvailability::kUnknown) {
				return {
					.kind = AvailabilityKind::kUnavailable,
					.reason = "DLSS was not admitted for this session."
				};
			}
			if (a_dlss.availability == CapabilityAvailability::kUnsupported) {
				return {
					.kind = AvailabilityKind::kUnavailable,
					.reason = "The runtime reported DLSS frame generation unsupported."
				};
			}
		}
		if (a_method == FrameGenerationMethod::kFSR4) {
			const auto& capability = a_fidelityFx.fsr4FrameGeneration;
			if (capability.availability != CapabilityAvailability::kUnknown &&
				!capability.IsAvailable()) {
				return {
					.kind = AvailabilityKind::kUnavailable,
					.reason = FidelityFXReason(capability.unavailableReason)
				};
			}
		}
		return {
			.kind = AvailabilityKind::kUnavailable,
			.reason = std::format(
				"{} was not admitted for this session.",
				Name(a_method))
		};
	}

	[[nodiscard]] inline std::string OptionLabel(
		std::string_view a_name,
		const Availability& a_availability)
	{
		if (a_availability.kind == AvailabilityKind::kAvailable) {
			return std::string(a_name);
		}
		return std::format("{} — {}", a_name, a_availability.reason);
	}
}
