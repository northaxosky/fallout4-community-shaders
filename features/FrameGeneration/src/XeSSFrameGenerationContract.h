#pragma once

#include <cstdint>
#include <utility>

#include <xefg_swapchain.h>
#include <xefg_swapchain_d3d12.h>
#include <xell.h>

#include "Render/TemporalProvider.h"

namespace cs::features::xess_fg
{
	struct FrameBeginResult
	{
		xefg_swapchain_result_t result =
			XEFG_SWAPCHAIN_RESULT_SUCCESS;
		bool enablementApplied = false;
	};

	template <class SetPresentId, class SetEnabled>
	[[nodiscard]] FrameBeginResult BeginFrame(
		xefg_swapchain_handle_t a_context,
		std::uint32_t a_presentId,
		bool a_enabled,
		bool a_previouslyEnabled,
		SetPresentId&& a_setPresentId,
		SetEnabled&& a_setEnabled)
	{
		const bool changed = a_enabled != a_previouslyEnabled;
		if (changed) {
			// Enabling clears pending history; it must precede the new input tags.
			const auto result = std::forward<SetEnabled>(a_setEnabled)(
				a_context, a_enabled ? 1u : 0u);
			if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
				return { .result = result };
			}
		}
		return {
			.result = std::forward<SetPresentId>(a_setPresentId)(
				a_context, a_presentId),
			.enablementApplied = changed
		};
	}

	template <class TagResource>
	[[nodiscard]] xefg_swapchain_result_t TagFrameResources(
		xefg_swapchain_handle_t a_context,
		const render::temporal::FrameGenerationRequest& a_request,
		TagResource&& a_tagResource)
	{
		if (!a_request.recording.commandList) {
			return XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT;
		}
		const auto presentId =
			static_cast<std::uint32_t>(a_request.realFrame);
		const auto tag =
			[&](xefg_swapchain_resource_type_t a_type,
				const render::temporal::D3D12GpuView& a_view,
				std::uint32_t a_width,
				std::uint32_t a_height,
				bool a_copyNow) {
				const xefg_swapchain_d3d12_resource_data_t resource{
					.type = a_type,
					.validity = a_copyNow
						? XEFG_SWAPCHAIN_RV_ONLY_NOW
						: XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT,
					.resourceBase = { 0, 0 },
					.resourceSize = { a_width, a_height },
					.pResource = a_view.resource,
					.incomingState = a_view.state
				};
				return a_tagResource(
					a_context,
					a_copyNow ? a_request.recording.commandList : nullptr,
					presentId,
					&resource);
			};
		auto result = tag(
			XEFG_SWAPCHAIN_RES_DEPTH,
			a_request.depth,
			a_request.renderWidth,
			a_request.renderHeight,
			true);
		if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return result;
		}
		result = tag(
			XEFG_SWAPCHAIN_RES_MOTION_VECTOR,
			a_request.motionVectors,
			a_request.renderWidth,
			a_request.renderHeight,
			true);
		if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return result;
		}
		result = tag(
			XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,
			a_request.hudlessColor,
			a_request.outputWidth,
			a_request.outputHeight,
			true);
		if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return result;
		}
		result = tag(
			XEFG_SWAPCHAIN_RES_BACKBUFFER,
			a_request.finalColor,
			a_request.outputWidth,
			a_request.outputHeight,
			false);
		if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return result;
		}
		return XEFG_SWAPCHAIN_RESULT_SUCCESS;
	}

	enum class PresentStatusClass : std::uint8_t
	{
		kSuccess,
		kUnavailable,
		kWarning,
		kError
	};

	[[nodiscard]] constexpr PresentStatusClass ClassifyPresentStatus(
		xefg_swapchain_result_t a_queryResult,
		xefg_swapchain_result_t a_frameResult) noexcept
	{
		if (static_cast<int>(a_queryResult) < 0 ||
			static_cast<int>(a_frameResult) < 0) {
			return PresentStatusClass::kError;
		}
		if (a_queryResult ==
			XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS) {
			return PresentStatusClass::kUnavailable;
		}
		if (a_queryResult != XEFG_SWAPCHAIN_RESULT_SUCCESS ||
			a_frameResult != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return PresentStatusClass::kWarning;
		}
		return PresentStatusClass::kSuccess;
	}

	struct PresentStatusObservation
	{
		PresentStatusClass classification = PresentStatusClass::kUnavailable;
		xefg_swapchain_result_t queryResult =
			XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS;
		xefg_swapchain_result_t frameResult =
			XEFG_SWAPCHAIN_RESULT_SUCCESS;
		std::uint32_t presentedFrames = 0;
		std::uint32_t generatedFrames = 0;
		bool countAvailable = false;
		bool enablementMatches = true;
		bool reportedEnabled = false;
	};

	template <class GetPresentStatus>
	[[nodiscard]] PresentStatusObservation ObservePresentStatus(
		xefg_swapchain_handle_t a_context,
		bool a_expectedEnabled,
		GetPresentStatus&& a_getPresentStatus)
	{
		xefg_swapchain_present_status_t status{};
		const auto queryResult =
			std::forward<GetPresentStatus>(a_getPresentStatus)(
				a_context, &status);
		const auto classification =
			ClassifyPresentStatus(queryResult, status.frameGenResult);
		const bool statusAvailable =
			classification != PresentStatusClass::kUnavailable &&
			classification != PresentStatusClass::kError;
		return {
			.classification = classification,
			.queryResult = queryResult,
			.frameResult = status.frameGenResult,
			.presentedFrames =
				statusAvailable ? status.framesPresented : 0,
			.generatedFrames =
				statusAvailable && status.framesPresented > 1
				? status.framesPresented - 1
				: 0,
			.countAvailable = statusAvailable,
			.enablementMatches =
				classification != PresentStatusClass::kSuccess ||
				static_cast<bool>(status.isFrameGenEnabled) ==
					a_expectedEnabled,
			.reportedEnabled =
				static_cast<bool>(status.isFrameGenEnabled)
		};
	}

	struct ContextDestroyResult
	{
		bool succeeded = true;
		std::int64_t sdkResult = 0;
		const char* operation = "";
	};

	template <class DestroyFrameGeneration, class DestroyLatency>
	[[nodiscard]] ContextDestroyResult DestroyContexts(
		xefg_swapchain_handle_t& a_frameGeneration,
		xell_context_handle_t& a_latency,
		DestroyFrameGeneration&& a_destroyFrameGeneration,
		DestroyLatency&& a_destroyLatency)
	{
		if (a_frameGeneration) {
			const auto result =
				std::forward<DestroyFrameGeneration>(
					a_destroyFrameGeneration)(a_frameGeneration);
			if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
				return {
					.succeeded = false,
					.sdkResult = static_cast<std::int64_t>(result),
					.operation = "XeSS-FG context destruction"
				};
			}
			a_frameGeneration = nullptr;
		}
		if (a_latency) {
			const auto result = std::forward<DestroyLatency>(
				a_destroyLatency)(a_latency);
			if (result != XELL_RESULT_SUCCESS) {
				return {
					.succeeded = false,
					.sdkResult = static_cast<std::int64_t>(result),
					.operation = "XeLL context destruction"
				};
			}
			a_latency = nullptr;
		}
		return {};
	}
}
