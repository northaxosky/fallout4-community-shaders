#pragma once

#include <cstdint>
#include <cmath>
#include <utility>

#include <sl_dlss_g.h>

#include "Render/FrameGenerationOrchestration.h"

namespace cs::features::streamline_fg
{
	enum class ConfigurationSupport : std::uint8_t
	{
		kSupported,
		kPendingCapabilities,
		kUnsupported
	};

	[[nodiscard]] inline ConfigurationSupport ValidateConfiguration(
		const render::temporal::FrameGenerationConfiguration& a_configuration,
		const render::temporal::FrameGenerationCapabilities& a_capabilities)
		noexcept
	{
		if (!std::isfinite(
				a_configuration.dynamicTargetFrameRate) ||
			a_configuration.dynamicTargetFrameRate < 0.0f ||
			(a_configuration.mode ==
					render::temporal::FrameGenerationMode::kFixed &&
				a_configuration.fixedMultiplier < 2)) {
			return ConfigurationSupport::kUnsupported;
		}
		if (a_capabilities.availability ==
				render::temporal::CapabilityAvailability::kUnsupported ||
			a_capabilities.configurationQueryFailed) {
			return ConfigurationSupport::kUnsupported;
		}
		if (a_capabilities.availability !=
				render::temporal::CapabilityAvailability::kSupported ||
			!a_capabilities.IsCurrent()) {
			return ConfigurationSupport::kPendingCapabilities;
		}
		if (a_configuration.mode ==
			render::temporal::FrameGenerationMode::kDynamic) {
			return a_capabilities.dynamicModeSupported
				? ConfigurationSupport::kSupported
				: ConfigurationSupport::kUnsupported;
		}
		return a_capabilities.maxGeneratedFrames > 0 &&
				a_configuration.fixedMultiplier - 1 <=
					a_capabilities.maxGeneratedFrames
			? ConfigurationSupport::kSupported
			: ConfigurationSupport::kUnsupported;
	}

	[[nodiscard]] inline sl::DLSSGOptions BuildOptions(
		bool a_enabled,
		const render::temporal::FrameGenerationConfiguration& a_configuration,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight,
		std::uint32_t a_backBufferCount,
		bool a_retainResources) noexcept
	{
		sl::DLSSGOptions options{};
		options.mode = !a_enabled
			? sl::DLSSGMode::eOff
			: a_configuration.mode ==
					  render::temporal::FrameGenerationMode::kDynamic
				? sl::DLSSGMode::eDynamic
				: sl::DLSSGMode::eOn;
		options.numFramesToGenerate =
			a_configuration.mode ==
					render::temporal::FrameGenerationMode::kFixed
				? a_configuration.fixedMultiplier - 1
				: 1;
		options.dynamicTargetFrameRate =
			a_configuration.dynamicTargetFrameRate;
		options.mvecDepthWidth = a_renderWidth;
		options.mvecDepthHeight = a_renderHeight;
		options.colorWidth = a_outputWidth;
		options.colorHeight = a_outputHeight;
		options.numBackBuffers = a_backBufferCount;
		options.colorBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		options.mvecBufferFormat = DXGI_FORMAT_R16G16_FLOAT;
		options.depthBufferFormat = DXGI_FORMAT_R32_FLOAT;
		options.hudLessBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		options.enableUserInterfaceRecomposition = sl::Boolean::eFalse;
		if (a_retainResources) {
			options.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
		}
		return options;
	}

	struct CleanupResult
	{
		bool succeeded = true;
		sl::Result sdkResult = sl::Result::eOk;
		const char* operation = "";
	};

	template <class ClearTags, class SetOptions, class FreeResources>
	[[nodiscard]] CleanupResult DestroyResources(
		bool a_resourcesConfigured,
		sl::ViewportHandle a_viewport,
		ClearTags&& a_clearTags,
		SetOptions&& a_setOptions,
		FreeResources&& a_freeResources)
	{
		if (!a_resourcesConfigured) {
			return {};
		}
		auto result = std::forward<ClearTags>(a_clearTags)();
		if (result != sl::Result::eOk) {
			return {
				.succeeded = false,
				.sdkResult = result,
				.operation = "clear frame tags"
			};
		}
		sl::DLSSGOptions options{};
		options.mode = sl::DLSSGMode::eOff;
		result = std::forward<SetOptions>(a_setOptions)(
			a_viewport, options);
		if (result != sl::Result::eOk) {
			return {
				.succeeded = false,
				.sdkResult = result,
				.operation = "disable DLSS-G"
			};
		}
		result = std::forward<FreeResources>(a_freeResources)(
			sl::kFeatureDLSS_G, a_viewport);
		if (result != sl::Result::eOk) {
			return {
				.succeeded = false,
				.sdkResult = result,
				.operation = "free DLSS-G resources"
			};
		}
		return {};
	}

	template <class GetState, class ObserveState>
	[[nodiscard]] sl::Result PollState(
		sl::ViewportHandle a_viewport,
		render::temporal::PresentedFrameAccumulator& a_generatedFrames,
		render::temporal::PresentedFrameAccumulator& a_presentedFrames,
		sl::DLSSGStatus& a_status,
		GetState&& a_getState,
		ObserveState&& a_observeState)
	{
		sl::DLSSGState state{};
		const auto result = std::forward<GetState>(a_getState)(
			a_viewport, state, nullptr);
		if (result == sl::Result::eOk) {
			a_generatedFrames.Add(state.numFramesActuallyPresented);
			a_presentedFrames.Add(state.numFramesActuallyPresented);
			a_status = state.status;
			std::forward<ObserveState>(a_observeState)(state);
		}
		return result;
	}
}
