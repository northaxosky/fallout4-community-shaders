#pragma once

#include <cstdint>
#include <utility>

#include <sl_dlss_g.h>

#include "Render/FrameGenerationOrchestration.h"

namespace cs::features::streamline_fg
{
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

	template <class GetState>
	[[nodiscard]] sl::Result PollState(
		sl::ViewportHandle a_viewport,
		render::temporal::PresentedFrameAccumulator& a_presentedFrames,
		sl::DLSSGStatus& a_status,
		GetState&& a_getState)
	{
		sl::DLSSGState state{};
		const auto result = std::forward<GetState>(a_getState)(
			a_viewport, state, nullptr);
		if (result == sl::Result::eOk) {
			a_presentedFrames.Add(state.numFramesActuallyPresented);
			a_status = state.status;
		}
		return result;
	}
}
